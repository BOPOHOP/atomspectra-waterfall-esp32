// #GZ-T (1.2.37-rc90, ТЕСТ): сжатие gzip тел /api/spectrum.json и /api/spectrum ОТДЕЛЬНОЙ задачей "spec_gz".
// Почему не в httpd: в стеке httpd свободно 4,5-4,8 КБ, цепочка кадров ROM tdefl ~6,4 КБ (consilium-rest-tcp/02-embedded.md §3).
// Задача: ядро 0, приоритет 3 (ниже httpd 5 и USB 6/7), стек 12 КБ в PSRAM (CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM),
// TCB во внутренней RAM (требование FreeRTOS). Создаётся при первом включении и не удаляется.
// Компрессор tdefl (ROM, 167 744 Б, PSRAM) трогает ТОЛЬКО задача: выделяет при включении, освобождает при выключении.
// Слоты (0 — spectrum.json, 1 — bins): метаданные под s_mtx, который НИКОГДА не держится во время сжатия или отправки.
// Буфер слота пишет только задача и только при valid == false и readers == 0; httpd берёт буфер только при valid == true,
// увеличив readers под s_mtx, и уменьшает readers после отправки. Буфер освобождается только при readers == 0
// (иначе повтор через GZ_RETRY_MS). SPEC_LOCK не берётся: тело даёт кэш через spectrum_http_cache_pin()/unpin().
#include "spectrum_gz.h"
#include "spectrum_gz_plan.h"
#include "spectrum_http_cache.h"
#include "boot_config.h"
#include "miniz.h"
#include "cJSON.h"
#include "sdkconfig.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
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

#define GZ_STACK_BYTES 12288   // кадры ROM tdefl ~6,4 КБ + свои; запас видно по gz_stack_hwm
#define GZ_TASK_PRIO   3       // ниже httpd (5), usb_rxw (6), usb_lib (7)
#define GZ_TASK_CORE   0
#define GZ_RETRY_MS    50      // слот ещё отправляют — повтор

static const char *TAG = "spec_gz";

typedef struct {
    uint8_t *buf;       // PSRAM
    size_t   cap;
    size_t   len;       // 0 — тело поколения gen не сжимается (короче порога или сжатое не короче несжатого)
    uint32_t gen;       // поколение кэша спектра (render count), из которого сжато тело
    bool     valid;
    int      readers;   // отправок этого буфера прямо сейчас (httpd)
} gz_slot_t;

static SemaphoreHandle_t s_mtx;
static gz_slot_t         s_slot[2];
static TaskHandle_t      s_task;
static tdefl_compressor *s_comp;
static volatile uint8_t  s_level_req;    // запрошенный уровень; пишет httpd (spectrum_gz_sync)
static volatile uint8_t  s_level_cur;    // применённый уровень; пишет только задача; 0 — сжатое не отдаётся
static volatile bool     s_kick[2];
static volatile uint32_t s_ok, s_hit, s_miss, s_nomem, s_fail;
static volatile uint32_t s_last_us, s_max_us, s_last_in, s_last_out;

// Задача: освободить буферы слотов, которые никто не отправляет. true — освобождены оба.
static bool gz_slots_release(void)
{
    uint8_t *drop[2] = { NULL, NULL };
    bool all = true;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    for (int k = 0; k < 2; k++) {
        s_slot[k].valid = false;
        if (s_slot[k].readers > 0) {
            all = false;
            continue;
        }
        drop[k] = s_slot[k].buf;
        s_slot[k].buf = NULL;
        s_slot[k].cap = 0;
        s_slot[k].len = 0;
    }
    xSemaphoreGive(s_mtx);
    heap_caps_free(drop[0]);
    heap_caps_free(drop[1]);
    return all;
}

// Задача: привести компрессор к запрошенному уровню. true — повторить позже (буфер слота ещё отправляют).
static bool gz_apply_level(void)
{
    uint8_t want = s_level_req;
    if (want == 0) {
        s_level_cur = 0;
        if (s_comp) {
            heap_caps_free(s_comp);
            s_comp = NULL;
            ESP_LOGI(TAG, "gzip off: compressor freed");
        }
        return !gz_slots_release();
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
            return false;
        }
        ESP_LOGI(TAG, "gzip level %u: compressor %u B in PSRAM", (unsigned)want, (unsigned)need);
    }
    if (s_level_cur != want) {
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        s_slot[0].valid = false;
        s_slot[1].valid = false;
        xSemaphoreGive(s_mtx);
        s_level_cur = want;
    }
    return false;
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

// Задача: сжать тело вида k текущего поколения в слот. true — повторить позже (прежнее тело слота ещё отправляют).
static bool gz_compress(int k)
{
    const void *src = NULL;
    size_t len = 0;
    uint32_t gen = 0;
    if (!spectrum_http_cache_pin(k, &src, &len, &gen)) return false;   // тело текущего поколения ещё не собрано
    gz_slot_t *sl = &s_slot[k];
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    bool done = sl->valid && sl->gen == gen;
    bool busy = !done && sl->readers > 0;
    if (!done && !busy) sl->valid = false;   // новых читателей не будет, старых нет: буфер только у задачи
    xSemaphoreGive(s_mtx);
    if (done || busy) {
        spectrum_http_cache_unpin(k);
        return busy;
    }
    size_t out_len = 0;
    int r = 0;
    if (spec_gz_worth(len)) {
        size_t cap = spec_gz_slot_cap(len);
        if (sl->cap < cap) {
            uint8_t *nb = NULL;
            if (spec_gz_mem_ok(cap, heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                               heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM))) {
                nb = heap_caps_realloc(sl->buf, cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            }
            if (!nb) {
                spectrum_http_cache_unpin(k);
                s_nomem++;
                return false;
            }
            sl->buf = nb;
            sl->cap = cap;
        }
        int64_t t0 = esp_timer_get_time();
        r = gz_deflate(sl->buf, sl->cap, src, len, s_level_cur, &out_len);
        uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
        if (r > 0) {
            s_last_us = us;
            if (us > s_max_us) s_max_us = us;
            s_last_in = (uint32_t)len;
            s_last_out = (uint32_t)out_len;
            s_ok++;
        }
    }
    spectrum_http_cache_unpin(k);
    if (r < 0) {
        s_fail++;
        return false;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    sl->len = (r > 0) ? out_len : 0;
    sl->gen = gen;
    sl->valid = true;
    xSemaphoreGive(s_mtx);
    return false;
}

static void gz_task(void *arg)
{
    (void)arg;
    for (;;) {
        bool again = gz_apply_level();
        if (s_level_cur != 0) {
            for (int k = 0; k < 2; k++) {
                if (!s_kick[k]) continue;
                s_kick[k] = false;
                if (gz_compress(k)) {
                    s_kick[k] = true;
                    again = true;
                }
            }
        }
        ulTaskNotifyTake(pdTRUE, again ? pdMS_TO_TICKS(GZ_RETRY_MS) : portMAX_DELAY);
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

// TCP_NODELAY на сокете запроса — как spectrum_http_nodelay() (1.2.37, spectrum_http_cache.c): иначе хвост сжатого тела ждёт
// ACK клиента (Nagle, +40 мс) и сравнение задержек со сжатием и без нечестно. Своя копия: проводка 1.2.37 требует ровно три
// вызова хелпера в spectrum_http_cache.c.
static void gz_nodelay(httpd_req_t *req)
{
    int one = 1;
    int fd = httpd_req_to_sockfd(req);
    if (fd >= 0) (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
}

// Крючок обработчика (задача httpd). true — отправлено готовое сжатое тело; false — ответить как раньше.
static bool gz_try_send(httpd_req_t *req, int kind, esp_err_t *out)
{
    spectrum_gz_sync();
    uint8_t lvl = s_level_cur;
    if (lvl == 0 || kind < 0 || kind > 1) return false;
    char ae[128];
    ae[0] = '\0';
    esp_err_t hr = httpd_req_get_hdr_value_str(req, "Accept-Encoding", ae, sizeof ae);
    bool want = (hr == ESP_OK || hr == ESP_ERR_HTTPD_RESULT_TRUNC) && spec_gz_accepts(ae);
    uint32_t cur = spectrum_http_cache_render_count();
    const uint8_t *p = NULL;
    size_t n = 0;
    uint32_t gen = 0;
    gz_slot_t *sl = &s_slot[kind];
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    int pick = spec_gz_pick(lvl, want, sl->valid, sl->gen, sl->len, cur);
    if (pick == SPEC_GZ_HIT) {
        sl->readers++;
        p = sl->buf;
        n = sl->len;
        gen = sl->gen;
    }
    xSemaphoreGive(s_mtx);
    if (pick == SPEC_GZ_MISS) {
        s_miss++;
        s_kick[kind] = true;
        xTaskNotifyGive(s_task);
        return false;
    }
    if (pick != SPEC_GZ_HIT) return false;
    char rc[16];   // живёт до httpd_resp_send (httpd хранит указатель)
    snprintf(rc, sizeof rc, "%" PRIu32, gen);
    httpd_resp_set_hdr(req, "X-Spectrum-Render-Count", rc);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Vary", "Accept-Encoding");
    if (kind == SPECTRUM_HTTP_KIND_BINS) {
        httpd_resp_set_type(req, "application/octet-stream");
        httpd_resp_set_hdr(req, "Content-Disposition", "inline");
    } else {
        httpd_resp_set_type(req, "application/json");
    }
    gz_nodelay(req);
    esp_err_t e = httpd_resp_send(req, (const char *)p, (ssize_t)n);
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (sl->readers > 0) sl->readers--;
    xSemaphoreGive(s_mtx);
    s_hit++;
    *out = e;
    return true;
}

// Крючок кэша: тело вида kind текущего поколения собрано (зовётся под мьютексом кэша — только уведомить, не ждать).
static void gz_note(int kind)
{
    if (s_level_cur == 0 || !s_task || kind < 0 || kind > 1) return;
    s_kick[kind] = true;
    xTaskNotifyGive(s_task);
}

void spectrum_gz_init(void)
{
    spectrum_http_cache_set_gz_hooks(gz_try_send, gz_note);
}

void spectrum_gz_add_metrics(struct cJSON *root)
{
    if (!root) return;
    cJSON_AddNumberToObject(root, "gz_level", (double)s_level_cur);
    cJSON_AddNumberToObject(root, "gz_comp_bytes", s_comp ? (double)sizeof(tdefl_compressor) : 0.0);
    cJSON_AddNumberToObject(root, "gz_ok", (double)s_ok);
    cJSON_AddNumberToObject(root, "gz_hit", (double)s_hit);
    cJSON_AddNumberToObject(root, "gz_miss", (double)s_miss);
    cJSON_AddNumberToObject(root, "gz_nomem", (double)s_nomem);
    cJSON_AddNumberToObject(root, "gz_fail", (double)s_fail);
    cJSON_AddNumberToObject(root, "gz_last_us", (double)s_last_us);
    cJSON_AddNumberToObject(root, "gz_max_us", (double)s_max_us);
    cJSON_AddNumberToObject(root, "gz_last_in", (double)s_last_in);
    cJSON_AddNumberToObject(root, "gz_last_out", (double)s_last_out);
    cJSON_AddNumberToObject(root, "gz_stack_hwm", s_task ? (double)uxTaskGetStackHighWaterMark(s_task) : 0.0);
}
