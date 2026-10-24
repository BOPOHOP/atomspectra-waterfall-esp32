// #GZ-T (1.2.37-rc90, ТЕСТ): сжатие gzip ТОЛЬКО тел /api/spectrum.json (v2).
// Почему не в httpd: в стеке httpd свободно 4,5-4,8 КБ, цепочка кадров ROM tdefl ~6,4 КБ (consilium-rest-tcp/02-embedded.md §3).
// Задача: ядро 0, приоритет 3 (ниже httpd 5 и USB 6/7), стек 16 КБ в PSRAM (CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM),
// TCB во внутренней RAM (требование FreeRTOS). Создаётся при первом включении и не удаляется.
// Компрессор tdefl (ROM, 167 744 Б, PSRAM) трогает ТОЛЬКО задача: выделяет при включении, освобождает при выключении.
// Слоты удалены: gzip только для v2, задача владеет компрессором.
// Асинхронные запросы обрабатываются через очередь задач (глубина 2, дедлайн 1500 мс), чтобы не блокировать httpd.
#include "spectrum_gz.h"
#include "spectrum_gz_plan.h"
#include "spectrum_gz_job.h"
#include "spectrum_http_cache.h"
#include "spectrogram.h"
#include "atomspectra.h"
#include "boot_config.h"
#include "miniz.h"
#include "cJSON.h"
#include "sdkconfig.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#ifndef CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM
#error "#GZ-T: task stack in PSRAM needs CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=y"
#endif

_Static_assert(SPEC_GZ_TDEFL_GREEDY == TDEFL_GREEDY_PARSING_FLAG, "spectrum_gz_plan.h: SPEC_GZ_TDEFL_GREEDY != miniz");

#define GZ_STACK_BYTES 16384   // кадры ROM tdefl ~6,4 КБ + свои; запас видно по gz_stack_hwm
#define GZ_TASK_PRIO   3       // ниже httpd (5), usb_rxw (6), usb_lib (7)
#define GZ_TASK_CORE   0

static const char *TAG = "spec_gz";

static SemaphoreHandle_t s_mtx;
static TaskHandle_t      s_task;
static tdefl_compressor *s_comp;
static volatile uint8_t  s_level_req;    // запрошенный уровень; пишет httpd (spectrum_gz_sync)
static volatile uint8_t  s_level_cur;    // применённый уровень; пишет только задача; 0 — сжатое не отдаётся
static volatile uint32_t s_ok, s_miss, s_nomem, s_h_nomem;   // s_h_nomem пишет только httpd
static volatile uint32_t s_last_us, s_max_us, s_last_in, s_last_out;

// Новое состояние для асинхронных задач
static spec_gz_q_t s_q;
static spec_gz_job_t s_jobs[SPEC_GZ_QUEUE_DEPTH];
static bool s_job_ready[SPEC_GZ_QUEUE_DEPTH];
static spec_gz_ring_t s_ring;
static spec_gz_cnt_t s_cnt;
static volatile uint32_t s_h_fallback, s_queue_full;

// Скретч-переменные только для задачи
static char *s_v2buf;
static uint32_t s_cur_gen;

// Задача: привести компрессор к запрошенному уровню.
static void gz_apply_level(void)
{
    uint8_t want = s_level_req;
    if (want == 0) {
        s_level_cur = 0;
        if (s_comp) {
            heap_caps_free(s_comp);
            s_comp = NULL;
            ESP_LOGI(TAG, "gzip off: compressor freed");
        }
        return;
    }
    if (!s_comp) {
        size_t need = sizeof(tdefl_compressor);
        if (spec_gz_mem_ok(need, heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                           heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM))) {
            s_comp = heap_caps_malloc(need, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }
        if (!s_comp) {
            s_nomem++;
            ESP_LOGW(TAG, "gzip level %u: no PSRAM for compressor", (unsigned)want);
            return;
        }
        ESP_LOGI(TAG, "gzip level %u: compressor %u B in PSRAM", (unsigned)want, (unsigned)need);
    }
    if (s_level_cur != want) {
        s_level_cur = want;
    }
}

// Задача: сырой deflate src в buf + SPEC_GZ_HDR_BYTES и рамка gzip. 1 — готово (*out_len), 0 — сжатое не короче
// несжатого, -1 — сбой tdefl.
static int gz_deflate(uint8_t *buf, size_t cap, const void *src, size_t len, uint8_t level, size_t *out_len)
{
    size_t room = spec_gz_deflate_room(len);
    if (room == 0 || cap < len) return 0;
    if (tdefl_init(s_comp, NULL, NULL, spec_gz_tdefl_flags(level)) != TDEFL_STATUS_OKAY) return -1;
    size_t in_sz = len, out_sz = room;
    tdefl_status st = tdefl_compress(s_comp, src, &in_sz, buf + SPEC_GZ_HDR_BYTES, &out_sz, TDEFL_FINISH);
    if (st == TDEFL_STATUS_OKAY) return 0;   // не уместилось в room
    if (st != TDEFL_STATUS_DONE || in_sz != len) return -1;
    uint32_t crc = (uint32_t)mz_crc32(MZ_CRC32_INIT, (const unsigned char *)src, len);
    *out_len = spec_gz_frame(buf, cap, out_sz, crc, (uint32_t)len);
    return *out_len ? 1 : -1;
}

// TCP_NODELAY на сокете запроса — как spectrum_http_nodelay() (1.2.37, spectrum_http_cache.c): иначе хвост сжатого тела ждёт
// ACK клиента (Nagle, +40 мс) и сравнение задержек со сжатием и без нечестно. Своя копия: проводка 1.2.37 требует ровно три
// вызова хелпера в spectrum_http_cache.c.
static void gz_nodelay(httpd_req_t *req)
{
    int one = 1;
    int fd = httpd_req_to_sockfd(req);
    if (fd >= 0) (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
}

// Вспомогательная функция: получить текущее время в мс
static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// Операции для spec_gz_job_run
static uint32_t gz_ops_now_ms(void *c) { (void)c; return now_ms(); }
static uint8_t gz_ops_level(void *c) { (void)c; return s_comp ? s_level_cur : 0; }

static bool gz_ops_acquire(void *c, const spec_gz_job_t *j, const void **body, size_t *len)
{
    (void)c;
    if (j->kind == 2) {
        if (!spectrum_http_cache_v2_acquire(j->channels, &s_v2buf, len, &s_cur_gen)) return false;
        *body = s_v2buf;
        return true;
    }
    return false;
}

static void gz_ops_release(void *c, const spec_gz_job_t *j)
{
    (void)c;
    if (j->kind == 2) {
        spectrum_http_cache_v2_release(s_v2buf);
        s_v2buf = NULL;
    }
}

static uint8_t *gz_ops_out_alloc(void *c, size_t cap)
{
    (void)c;
    if (spec_gz_mem_ok(cap, heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                       heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM))) {
        return heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    return NULL;
}

static void gz_ops_out_free(void *c, uint8_t *out)
{
    (void)c;
    heap_caps_free(out);
}

static int gz_ops_compress(void *c, uint8_t level, const void *body, size_t len, uint8_t *out, size_t cap, size_t *out_len)
{
    (void)c;
    int64_t t0 = esp_timer_get_time();
    int r = gz_deflate(out, cap, body, len, level, out_len);
    uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
    if (r > 0) {
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        spec_gz_ring_put(&s_ring, us);
        s_last_us = us;
        if (us > s_max_us) s_max_us = us;
        s_last_in = len;
        s_last_out = *out_len;
        s_ok++;
        xSemaphoreGive(s_mtx);
    }
    return r;
}

static bool gz_ops_send_gz(void *c, const spec_gz_job_t *j, const uint8_t *out, size_t out_len)
{
    (void)c;
    httpd_req_t *req = (httpd_req_t *)j->req;
    char rc[16];
    snprintf(rc, sizeof rc, "%" PRIu32, s_cur_gen);
    
    httpd_resp_set_hdr(req, "X-Spectrum-Render-Count", rc);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    
    char age[16];
    snprintf(age, sizeof age, "%" PRId32, usb_host_cdc_hist_age_ms());
    httpd_resp_set_hdr(req, "X-Hist-Age-Ms", age);
    httpd_resp_set_type(req, "application/json");
    
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Vary", "Accept-Encoding");
    httpd_resp_set_hdr(req, "X-Spectrum-Gz", "1");
    
    gz_nodelay(req);
    esp_err_t e = httpd_resp_send(req, (const char *)out, (ssize_t)out_len);
    return e == ESP_OK;
}

static bool gz_ops_send_plain(void *c, const spec_gz_job_t *j)
{
    (void)c;
    httpd_req_t *req = (httpd_req_t *)j->req;
    return spectrum_http_send_json_plain(req) == ESP_OK;   // F2: код ошибки не теряем
}

// F2: ошибка отправки из задачи - закрыть сессию, как при возврате ошибки из синхронного обработчика; ДО complete
static void gz_ops_close_session(void *c, const spec_gz_job_t *j)
{
    (void)c;
    httpd_req_t *req = (httpd_req_t *)j->req;
    httpd_sess_trigger_close(req->handle, httpd_req_to_sockfd(req));
}

static void gz_ops_complete(void *c, const spec_gz_job_t *j)
{
    (void)c;
    httpd_req_async_handler_complete((httpd_req_t *)j->req);
}

static spec_gz_ops_t s_ops = {
    .now_ms = gz_ops_now_ms,
    .level = gz_ops_level,
    .acquire = gz_ops_acquire,
    .release = gz_ops_release,
    .out_alloc = gz_ops_out_alloc,
    .out_free = gz_ops_out_free,
    .compress = gz_ops_compress,
    .send_gz = gz_ops_send_gz,
    .send_plain = gz_ops_send_plain,
    .close_session = gz_ops_close_session,
    .complete = gz_ops_complete
};

// Вспомогательная функция: извлечь задачу из очереди
static bool gz_job_pop(spec_gz_job_t *out)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (s_q.count == 0 || !s_job_ready[s_q.head]) {
        xSemaphoreGive(s_mtx);
        return false;
    }
    int idx = spec_gz_q_pop(&s_q);
    *out = s_jobs[idx];
    s_job_ready[idx] = false;
    xSemaphoreGive(s_mtx);
    return true;
}

// Вспомогательная функция: добавить задачу в очередь
static bool gz_enqueue(httpd_req_t *req, int channels)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    int idx = spec_gz_q_reserve(&s_q);
    if (idx < 0) {
        s_queue_full++;
        s_h_fallback++;
        xSemaphoreGive(s_mtx);
        return false;
    }
    xSemaphoreGive(s_mtx);
    
    httpd_req_t *ar = NULL;
    esp_err_t be = httpd_req_async_handler_begin(req, &ar);
    if (be != ESP_OK) {
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        spec_gz_q_unreserve(&s_q);
        s_h_fallback++;
        xSemaphoreGive(s_mtx);
        return false;
    }
    
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_jobs[idx] = (spec_gz_job_t){2, channels, now_ms(), ar};
    s_job_ready[idx] = true;
    xSemaphoreGive(s_mtx);
    
    xTaskNotifyGive(s_task);
    return true;
}

static void gz_task(void *arg)
{
    (void)arg;
    for (;;) {
        gz_apply_level();
        
        // Обработка асинхронных задач (даже если уровень 0, чтобы не зависали запросы)
        spec_gz_job_t j;
        while (gz_job_pop(&j)) {
            spec_gz_job_run(&s_ops, NULL, &j, &s_cnt);
        }
        
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
}

static bool gz_task_start(void)
{
    if (!s_mtx) s_mtx = xSemaphoreCreateMutex();
    if (!s_mtx) return false;
    StaticTask_t *tcb = heap_caps_calloc(1, sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    StackType_t *stack = heap_caps_malloc(GZ_STACK_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (tcb && stack) {
        s_task = xTaskCreateStaticPinnedToCore(gz_task, "spec_gz", GZ_STACK_BYTES, NULL, GZ_TASK_PRIO, stack, tcb,
                                               GZ_TASK_CORE);
    }
    if (!s_task) {
        heap_caps_free(tcb);
        heap_caps_free(stack);
        ESP_LOGW(TAG, "task start failed");
        return false;
    }
    ESP_LOGI(TAG, "task started: stack %d B in PSRAM, core %d, prio %d", GZ_STACK_BYTES, GZ_TASK_CORE, GZ_TASK_PRIO);
    return true;
}

void spectrum_gz_sync(void)
{
    uint8_t want = boot_config_gzip_level();
    if (want == s_level_req) return;
    s_level_req = want;
    if (want != 0 && !s_task && !gz_task_start()) {
        s_nomem++;
        return;
    }
    if (s_task) xTaskNotifyGive(s_task);
}

// Крючок обработчика (задача httpd). true — отправлено готовое сжатое тело; false — ответить как раньше.
static bool gz_try_send(httpd_req_t *req, int channels, esp_err_t *out)
{
    spectrum_gz_sync();
    uint8_t lvl = s_level_cur;
    if (lvl == 0 || !s_task) return false;
    
    char ae[128];
    ae[0] = '\0';
    esp_err_t hr = httpd_req_get_hdr_value_str(req, "Accept-Encoding", ae, sizeof ae);
    bool want = (hr == ESP_OK || hr == ESP_ERR_HTTPD_RESULT_TRUNC) && spec_gz_accepts(ae);
    
    int pick = spec_gz_pick(lvl, want, false, 0, 0, spectrum_http_cache_render_count());
    
    if (pick != SPEC_GZ_MISS) {
        return false;
    }
    
    s_miss++;
    bool recording = spectrogram_is_recording();
    bool mem_ok = s_comp != NULL && spec_gz_mem_ok(SPEC_GZ_ASYNC_MEM_PROBE, 
                                                   heap_caps_get_free_size(MALLOC_CAP_SPIRAM), 
                                                   heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    bool qfull = spec_gz_q_full(&s_q);
    xSemaphoreGive(s_mtx);
    
    int ok = spec_gz_decide(want, recording, lvl, mem_ok, qfull);
    int route = spec_gz_route(pick, ok);
    
    if (route != SPEC_GZ_ASYNC) {
        if (want && qfull && !recording && mem_ok) {
            s_queue_full++;
            s_h_fallback++;
        } else if (want && !recording && !mem_ok) {
            s_h_nomem++;   // F8: причина «нет PSRAM» видна в gz_nomem
            s_h_fallback++;
        }
        return false;
    }
    
    if (gz_enqueue(req, channels)) {
        *out = ESP_OK;
        return true;
    }
    return false;
}

void spectrum_gz_init(void)
{
    spectrum_http_cache_set_gz_hook(gz_try_send);
}

void spectrum_gz_add_metrics(struct cJSON *root)
{
    if (!root) return;
    cJSON_AddNumberToObject(root, "gz_level", (double)s_level_cur);
    cJSON_AddNumberToObject(root, "gz_comp_bytes", s_comp ? (double)sizeof(tdefl_compressor) : 0.0);
    cJSON_AddNumberToObject(root, "gz_ok", (double)s_ok);
    cJSON_AddNumberToObject(root, "gz_miss", (double)s_miss);
    cJSON_AddNumberToObject(root, "gz_nomem", (double)(s_nomem + s_h_nomem + s_cnt.nomem));
    cJSON_AddNumberToObject(root, "gz_fail", (double)s_cnt.fail);
    cJSON_AddNumberToObject(root, "gz_last_us", (double)s_last_us);
    cJSON_AddNumberToObject(root, "gz_max_us", (double)s_max_us);
    cJSON_AddNumberToObject(root, "gz_last_in", (double)s_last_in);
    cJSON_AddNumberToObject(root, "gz_last_out", (double)s_last_out);
    cJSON_AddNumberToObject(root, "gz_stack_hwm", s_task ? (double)uxTaskGetStackHighWaterMark(s_task) : 0.0);
    
    // Новые метрики
    cJSON_AddNumberToObject(root, "gz_async_ok", (double)s_cnt.ok);
    cJSON_AddNumberToObject(root, "gz_fallback", (double)(s_cnt.fallback + s_h_fallback));
    cJSON_AddNumberToObject(root, "gz_async_timeout", (double)s_cnt.timeout);
    cJSON_AddNumberToObject(root, "gz_queue_full", (double)s_queue_full);
    cJSON_AddNumberToObject(root, "gz_send_err", (double)s_cnt.send_err);
    
    // Массив gz_ring_us
    cJSON *a = cJSON_AddArrayToObject(root, "gz_ring_us");
    if (s_mtx) {
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        uint32_t tmp[SPEC_GZ_RING_N];
        size_t count = spec_gz_ring_get(&s_ring, tmp);
        xSemaphoreGive(s_mtx);
        for (size_t i = 0; i < count; i++) {
            cJSON_AddItemToArray(a, cJSON_CreateNumber(tmp[i]));
        }
    }
}
