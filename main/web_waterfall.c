#include "atomspectra.h"
#include "calib_autoread.h"  // #AWF-12b F2: calib_is_missing — единый признак "задана"
#include "spectrogram.h"
#include "web_waterfall.h"
#include "web_util.h"
#include "wf_offload.h"   // #REC-11-A2: конфиг/статус автономной выгрузки
#include "http_io_gate.h" // #PERF-2: HEAVY lane for segment/window/export
#include "http_gate_budget.h" // P-009 (sweep-A): единый бюджет ожидания HEAVY-слота
#include "calib_export.h"     // R7 (sweep-A): признак «калибровка есть» в файловых выводах
#include "wf_export_plan.h"   // #FW-19 (sweep-A): экспорт n42 полной истории (host-pure)
#include "wf_seg_pin.h"       // #REC-12 (sweep-A): WF_PIN_NONE для pull-пина h_segment
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdint.h>   // F1: intptr_t в choke point reg()/wf_activity_trampoline
#include <assert.h>   // N4: рантайм-защёлка на переполнение таблицы трамплина
#include <unistd.h>      // close() для web_waterfall_on_close()
#include <dirent.h>      // #REC-11-A1: листинг /storage/wf для /segments
#include <sys/stat.h>    // #REC-11-A1: размер файла сегмента (stat)

static const char *TAG = "wf_web";

#define WF_WS_MAX        4
#define WS_INFLIGHT_MAX  4   // P2-3: лимит несброшенных кадров на клиента; LK-09 (1.2.30): 8 → 4 (меньше очереди у медленного клиента)
// #PERF-2 / P-009 (sweep-A): сколько ждать HEAVY-слот, прежде чем отдать 503.
// Прежние 250 мс исходили из «держатель — автосейв, десяток миллисекунд»; это неверно:
// разовый автосейв / снимок-бэкап / base.bin держат слот ~0,6–0,7 с (fwrite 33 КиБ
// ~0,45 с + fopen/unlink/rename), и GET сегмента получал 503 при неработавшем кольце.
// Другой HTTP-запрос держателем быть не может — httpd однозадачный. Значение и
// обоснование — http_gate_budget.h (2000 мс, ниже сокетного таймаута httpd 3 с).
#define WF_SEGMENT_GATE_WAIT_MS HTTP_GATE_WAIT_MS
/* #FW-65: Clear unlinks many ~1 MiB files under FSLOCK; тот же класс ожидания —
 * тот же бюджет (было 250 мс «длиннее, чем у GET», фактически равно ему). */
#define WF_CLEAR_GATE_WAIT_MS   HTTP_GATE_WAIT_MS

static httpd_handle_t s_server;
static int            s_ws_fds[WF_WS_MAX];
// P2-3/P3-3: s_ws_fds трогают httpd-таск (ws_add/ws_del) и таск-продьюсер
// (wf_broadcast/ws_async_send). Мьютекс закрывает гонку реестра; s_ws_inflight
// считает кадры, поставленные в очередь httpd_queue_work но ещё не отданные, —
// при переполнении кадр дропается (back-pressure вместо роста кучи).
static int               s_ws_inflight[WF_WS_MAX];
static SemaphoreHandle_t s_ws_mutex = NULL;
#define WS_LOCK()   do { if (s_ws_mutex) xSemaphoreTake(s_ws_mutex, portMAX_DELAY); } while (0)
#define WS_UNLOCK() do { if (s_ws_mutex) xSemaphoreGive(s_ws_mutex); } while (0)

/* ---- WS client registry + async broadcast ---- */

static void ws_del_locked(int fd)
{
    for (int i = 0; i < WF_WS_MAX; i++)
        if (s_ws_fds[i] == fd) { s_ws_fds[i] = 0; s_ws_inflight[i] = 0; }
}

static void ws_add(int fd)
{
    // #UI-15 P0a: при отсутствии свободного слота — выталкиваем слот 0 (LRU),
    // иначе зомби после F5 копились и водопад «глох» на новых клиентах.
    WS_LOCK();
    for (int i = 0; i < WF_WS_MAX; i++) if (s_ws_fds[i] == fd) { WS_UNLOCK(); return; }
    int slot = -1;
    for (int i = 0; i < WF_WS_MAX; i++) if (s_ws_fds[i] <= 0) { slot = i; break; }
    if (slot < 0) { ESP_LOGW(TAG, "ws_add: evict fd=%d for fd=%d", s_ws_fds[0], fd); slot = 0; }
    s_ws_fds[slot] = fd; s_ws_inflight[slot] = 0;
    WS_UNLOCK();
}

static void ws_del(int fd)
{
    WS_LOCK();
    ws_del_locked(fd);
    WS_UNLOCK();
}

/* #UI-15 P0: httpd close_fn — единственный надёжный сигнал «сокет закрыт».
   Без него s_ws_fds[] копит зомби fd (RST/FIN при F5 не доходят как
   HTTPD_WS_TYPE_CLOSE) и водопад глохнет после 4 F5. */
void web_waterfall_on_close(httpd_handle_t hd, int sockfd)
{
    (void)hd;
    ws_del(sockfd);
    close(sockfd);   // обязательно: при заданном close_fn httpd сам не закрывает
}

typedef struct { int fd; size_t len; uint8_t buf[]; } ws_send_t;

static void ws_async_send(void *arg)
{
    ws_send_t *a = arg;
    // LK-09 (1.2.30): после первого сбоя клиент удалён из реестра, но в очереди httpd могли остаться его кадры (до
    // WS_INFLIGHT_MAX); каждый такой кадр блокировал бы веб-сервер на сокетный таймаут (3 с). Отброшенному клиенту не шлём.
    bool alive = false;
    WS_LOCK();
    for (int i = 0; i < WF_WS_MAX; i++) if (s_ws_fds[i] == a->fd) { alive = true; break; }
    WS_UNLOCK();
    if (!alive) { free(a); return; }
    httpd_ws_frame_t fr = { 0 };
    fr.type    = HTTPD_WS_TYPE_BINARY;
    fr.payload = a->buf;
    fr.len     = a->len;
    esp_err_t r = httpd_ws_send_frame_async(s_server, a->fd, &fr);
    WS_LOCK();
    for (int i = 0; i < WF_WS_MAX; i++)
        if (s_ws_fds[i] == a->fd) { if (s_ws_inflight[i] > 0) s_ws_inflight[i]--; break; }
    if (r != ESP_OK) ws_del_locked(a->fd);
    WS_UNLOCK();
    free(a);
}

static void wf_broadcast(const uint16_t *row, size_t bytes, uint32_t idx)
{
    (void)idx;
    WS_LOCK();
    for (int i = 0; i < WF_WS_MAX; i++) {
        int fd = s_ws_fds[i];
        if (fd <= 0) continue;
        if (s_ws_inflight[i] >= WS_INFLIGHT_MAX) continue;   // back-pressure: дроп кадра
        ws_send_t *a = heap_caps_malloc(sizeof(ws_send_t) + bytes, MALLOC_CAP_SPIRAM);
        if (!a) continue;
        a->fd  = fd;
        a->len = bytes;
        memcpy(a->buf, row, bytes);
        if (httpd_queue_work(s_server, ws_async_send, a) != ESP_OK) free(a);
        else s_ws_inflight[i]++;
    }
    WS_UNLOCK();
}

static int append_calib_json(char *buf, int off, int cap)
{
    spectrum_data_t *sp = malloc(sizeof(*sp));
    if (!sp) return off;
    spectrum_get_snapshot(sp);
    if (sp->serial_number[0])
        off += snprintf(buf + off, cap - off, ",\"serial\":\"%s\"", sp->serial_number);
    if (sp->calib_valid) {
        off += snprintf(buf + off, cap - off, ",\"calibration\":[");
        for (int i = 0; i <= sp->calib_order; i++)
            off += snprintf(buf + off, cap - off, "%s%.15g", i ? "," : "", sp->calibration[i]);
        off += snprintf(buf + off, cap - off, "]");
    }
    // #AWF-12b F2 (release-gate-1.2.28-code.md): тот же признак, что
    // web_server.c /api/device и spectrum_http_cache.c — waterfall.html
    // решает по нему, рисовать ли ось в кэВ (drawAxis).
    off += snprintf(buf + off, cap - off, ",\"calib_set\":%s",
        calib_is_missing(sp->calibration, CALIB_COEFFS, sp->calib_valid) ? "false" : "true");
    free(sp);
    return off;
}

static esp_err_t h_status(httpd_req_t *req)
{
    wf_status_t s;
    spectrogram_get_status(&s);
    char buf[448];
    int n = snprintf(buf, sizeof(buf),
        "{\"recording\":%s,\"persist\":%s,\"flash_full\":%s,\"ready\":%s,"
        "\"interval_sec\":%" PRIu32 ",\"ring_capacity\":%" PRIu32 ",\"ring_count\":%" PRIu32 ","
        "\"total_rows\":%" PRIu32 ",\"flash_rows\":%" PRIu32 ","
        // #FW-57: seg_lost (реальная потеря) и seg_evicted (безопасное вытеснение кольцом)
        // разведены. seg_dropped СОХРАНЁН как deprecated-алиас seg_lost: поле публичное,
        // его читают внешний приёмник и наши скрипты — переименование без алиаса сломало бы
        // их молча. Алиас = seg_lost (тревожная метрика), НЕ сумма: клиент, следивший за
        // "что-то потерялось", должен и дальше видеть именно потери, а не штатную ротацию.
        "\"seg_count\":%" PRIu32 ",\"seg_lost\":%" PRIu32 ",\"seg_evicted\":%" PRIu32 ","
        "\"seg_dropped\":%" PRIu32 ","
        "\"started_at\":%ld,\"elapsed_sec\":%" PRIu32 ",\"channels\":%d}",
        s.recording ? "true" : "false", s.persist ? "true" : "false",
        s.flash_full ? "true" : "false", s.ready ? "true" : "false",
        s.interval_sec, s.ring_capacity, s.ring_count,
        s.total_rows, s.flash_rows,
        s.seg_count, s.seg_lost, s.seg_evicted,      /* #FW-57 */
        s.seg_lost,                                  /* deprecated alias */
        (long)s.started_at, s.elapsed_sec, WF_CHANNELS);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, n);
    return ESP_OK;
}

static esp_err_t h_start(httpd_req_t *req)
{
    if (!web_csrf_check(req)) return ESP_FAIL;
    int r = spectrogram_start();
    /* Водопад должен работать НЕЗАВИСИМО от набора спектра: по Старту сами
       запускаем набор MCA на приборе (-sta — прибор стримит спектр раз в
       секунду). Без этого гистограмма не обновляется и строки водопада
       нулевые (см. PROTOCOL.md, раздел «MCA — управление набором спектра»). */
    if (r == 0) {
        int tx = usb_host_send_text_command("-sta");
        ESP_LOGI(TAG, "waterfall start -> -sta sent to analyzer (rc=%d)", tx);
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, r == 0 ? "{\"ok\":true}" : "{\"ok\":false}");
    return ESP_OK;
}

static esp_err_t h_stop(httpd_req_t *req)
{
    if (!web_csrf_check(req)) return ESP_FAIL;
    spectrogram_stop();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t h_clear(httpd_req_t *req)
{
    if (!web_csrf_check(req)) return ESP_FAIL;
    if (!http_io_gate_enter_wait_or_503(req, WF_CLEAR_GATE_WAIT_MS)) return ESP_OK;
    int r = spectrogram_clear();
    http_io_gate_leave();
    httpd_resp_set_type(req, "application/json");
    if (r == 0)
        httpd_resp_sendstr(req, "{\"ok\":true}");
    else if (r == -1)
        httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"recording\"}");
    else
        httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"delete\"}");
    return ESP_OK;
}

static esp_err_t h_config(httpd_req_t *req)
{
    if (!web_csrf_check(req)) return ESP_FAIL;
    char body[256] = { 0 };
    int rl = httpd_req_recv(req, body, sizeof(body) - 1);
    if (rl <= 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty"); return ESP_FAIL; }
    body[rl] = 0;
    cJSON *root = cJSON_Parse(body);
    if (!root) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "json"); return ESP_FAIL; }
    cJSON *iv = cJSON_GetObjectItem(root, "interval");
    if (cJSON_IsNumber(iv)) spectrogram_set_interval((uint32_t)iv->valuedouble);
    cJSON *ps = cJSON_GetObjectItem(root, "persist");
    if (cJSON_IsBool(ps)) spectrogram_set_persist(cJSON_IsTrue(ps));
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

/* GET /api/waterfall/window -> binary:
   "ASWW"(4) channels(u32 LE) rows(u32) first_index(u32) interval(u32) payload */
/* emit-колбэк: отдаёт одну строку окна chunked-кусками <= 8 КБ. */
static bool wf_window_emit(void *ctx, const uint16_t *row, size_t bytes)
{
    httpd_req_t *req = (httpd_req_t *)ctx;
    const char *p = (const char *)row;
    size_t off = 0;
    while (off < bytes) {
        size_t c = bytes - off; if (c > 8192) c = 8192;
        if (httpd_resp_send_chunk(req, p + off, c) != ESP_OK) return false;
        off += c;
    }
    return true;
}

static esp_err_t h_window(httpd_req_t *req)
{
    if (!web_csrf_check(req)) return ESP_FAIL;   // S-01 (1.2.30): тяжёлый GET (до 4 МБ) — токен обязателен
    if (!http_io_gate_enter_or_503(req)) return ESP_OK;
    /* Потоковая отдача всего кольца (до 256 строк) через единственный 16-КБ
       bounce-буфер: НЕ держим второй 4-МБ буфер в PSRAM рядом с ring → нет
       OOM/HTTP 500. rows берём снимком ring_count; ring_count монотонно растёт
       до заполнения, поэтому стрим отдаст ровно столько строк, сколько в шапке. */
    wf_status_t s;
    spectrogram_get_status(&s);
    uint16_t *bounce = heap_caps_malloc(WF_ROW_BYTES, MALLOC_CAP_SPIRAM);
    if (!bounce) {
        http_io_gate_leave();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }

    uint32_t rows = s.ring_count;
    // P-01 (1.2.30): ?rows=N — только последние N строк (страница не качает всё кольцо, до 4 МБ, на каждый вход)
    char q[32], qv[12];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, "rows", qv, sizeof(qv)) == ESP_OK) {
        uint32_t want = (uint32_t)strtoul(qv, NULL, 10);
        if (want >= 1 && want < rows) rows = want;
    }
    uint32_t first = (rows <= s.total_rows) ? (s.total_rows - rows) : 0;
    uint8_t pre[20];
    memcpy(pre, "ASWW", 4);
    uint32_t ch = WF_CHANNELS, iv = s.interval_sec;
    memcpy(pre + 4,  &ch,    4);
    memcpy(pre + 8,  &rows,  4);
    memcpy(pre + 12, &first, 4);
    memcpy(pre + 16, &iv,    4);

    httpd_resp_set_type(req, "application/octet-stream");
    if (httpd_resp_send_chunk(req, (char *)pre, 20) != ESP_OK) {
        heap_caps_free(bounce);
        http_io_gate_leave();
        return ESP_FAIL;
    }
    spectrogram_stream_window(bounce, rows, NULL, wf_window_emit, req);
    httpd_resp_send_chunk(req, NULL, 0);
    heap_caps_free(bounce);
    http_io_gate_leave();
    return ESP_OK;
}

/* ---- v3: GET /api/waterfall/export.aswf — кольцо PSRAM в бинарном ASWF v3 ---- */

// #INT-1: аккумулятивный CRC32 (zlib-совместимый, poly 0xEDB88320), тот же алгоритм,
// что spectrogram.c crc32_upd (путь сегмента). Дублирован намеренно (стандарт, не
// расходится) чтобы не тянуть spectrogram-static наружу. Старт 0xFFFFFFFF, финал ^= 0xFFFFFFFF — у вызывающего.
static uint32_t wf_crc32_upd(uint32_t crc, const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int j = 0; j < 8; j++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
    }
    return crc;
}

typedef struct {
    httpd_req_t    *req;
    uint8_t        *tail;        /* 26-байт буфер: dur(2)+ts(4)+lat(4)+lon(4)+dose(4)+temp(4)+crc32(4) */
    const uint16_t *durs;
    const float    *temps;       /* #FW-41: t1 детектора по строкам (NaN если нет -inf) */
    uint32_t        r0;
    uint32_t        r;
    uint32_t        interval_sec;
    time_t          row_start;
} aswf_ctx_t;

static bool aswf_row_emit(void *vctx, const uint16_t *row, size_t bytes)
{
    aswf_ctx_t *c = (aswf_ctx_t *)vctx;
    if (httpd_resp_send_chunk(c->req, (const char *)row, (ssize_t)bytes) != ESP_OK) return false;

    uint32_t local = c->r - c->r0;
    uint32_t dur = c->durs ? (uint32_t)c->durs[local] : 0;
    if (dur == 0) dur = c->interval_sec;
    c->r++;

    time_t ts = c->row_start;
    c->row_start += (time_t)dur;

    float dose = spectrogram_compute_dose_rate(row, dur);
    uint32_t nan_bits = 0x7FC00000u;
    float lat_v, lon_v, temp_v;
    memcpy(&lat_v, &nan_bits, 4);
    memcpy(&lon_v, &nan_bits, 4);
    if (c->temps) temp_v = c->temps[local];       // #FW-41: t1 строки (может быть NaN)
    else          memcpy(&temp_v, &nan_bits, 4);

    uint32_t ts32 = (uint32_t)ts;
    uint8_t *t = c->tail;
    t[0] = dur & 0xFF;           t[1] = (dur >> 8) & 0xFF;
    t[2] = ts32 & 0xFF;          t[3] = (ts32 >> 8) & 0xFF;
    t[4] = (ts32 >> 16) & 0xFF;  t[5] = (ts32 >> 24) & 0xFF;
    memcpy(t + 6,  &lat_v,  4);
    memcpy(t + 10, &lon_v,  4);
    memcpy(t + 14, &dose,   4);
    memcpy(t + 18, &temp_v, 4);                    // #FW-41
    // #INT-1: per-row CRC32 (как у сегментов) — покрывает spectrum(16384)+tail(22)=16406,
    // тот же байт-порядок/алгоритм, что seg_finalize → единый v5 с контролем целостности.
    uint32_t crc = 0xFFFFFFFFu;
    crc = wf_crc32_upd(crc, (const uint8_t *)row, WF_ROW_BYTES);   // spectrum 16384
    crc = wf_crc32_upd(crc, t, 22);                                // dur..temperature
    crc ^= 0xFFFFFFFFu;
    t[22] = crc & 0xFF;         t[23] = (crc >> 8) & 0xFF;
    t[24] = (crc >> 16) & 0xFF; t[25] = (crc >> 24) & 0xFF;
    return httpd_resp_send_chunk(c->req, (const char *)t, 26) == ESP_OK;
}

/* GET /api/waterfall/export.aswf -> ASWF v5 (кольцо PSRAM, без baseline-секции, с per-row crc32).
   #INT-1: раньше stream-export шёл без crc (stride 16406); теперь несёт crc32 как сегменты
   (stride 16410) → контроль целостности доступен и на выгрузке кольца. Самоописываемо (row_fields). */
static esp_err_t h_export_aswf(httpd_req_t *req)
{
    if (!http_io_gate_enter_or_503(req)) return ESP_OK;
    wf_status_t s;
    spectrogram_get_status(&s);
    if (s.ring_count == 0) {
        http_io_gate_leave();
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no waterfall data");
        return ESP_FAIL;
    }

    spectrum_data_t *sp = malloc(sizeof(*sp));
    if (sp) spectrum_get_snapshot(sp);

    uint16_t *row  = heap_caps_malloc(WF_ROW_BYTES, MALLOC_CAP_SPIRAM);
    char     *hbuf = malloc(WF_HDR_RESERVE);
    uint8_t  *tail = malloc(26);   // #FW-41 +temp(4); #INT-1 +crc32(4)
    if (!row || !hbuf || !tail) {
        if (row)  heap_caps_free(row);
        if (hbuf) free(hbuf);
        if (tail) free(tail);
        if (sp)   free(sp);
        http_io_gate_leave();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }

    uint32_t r0 = (s.ring_count <= s.total_rows) ? (s.total_rows - s.ring_count) : 0;
    time_t first_ts = s.started_at + (time_t)r0 * (time_t)s.interval_sec;
    time_t now = time(NULL);

    int cap = WF_HDR_RESERVE;
    int n = snprintf(hbuf, cap,
        "{\"saved_rows\":%" PRIu32 ",\"saved_at\":%ld"
        ",\"format\":\"atomspectra-waterfall\",\"version\":5"
        ",\"channels\":%d,\"dtype\":\"uint16\",\"byte_order\":\"little\""
        ",\"row_stride\":%d"
        ",\"row_fields\":["
        "{\"name\":\"spectrum\",\"dtype\":\"uint16\",\"channels\":%d,\"offset\":0},"
        "{\"name\":\"duration\",\"dtype\":\"uint16\",\"unit\":\"sec\",\"offset\":%d},"
        "{\"name\":\"timestamp\",\"dtype\":\"uint32\",\"unit\":\"unix_sec\",\"offset\":%d},"
        "{\"name\":\"latitude\",\"dtype\":\"float32\",\"unit\":\"deg\",\"offset\":%d},"
        "{\"name\":\"longitude\",\"dtype\":\"float32\",\"unit\":\"deg\",\"offset\":%d},"
        "{\"name\":\"dose_rate\",\"dtype\":\"float32\",\"unit\":\"usv_h\",\"offset\":%d},"
        "{\"name\":\"temperature\",\"dtype\":\"float32\",\"unit\":\"celsius\",\"offset\":%d},"
        "{\"name\":\"crc32\",\"dtype\":\"uint32\",\"algo\":\"crc32\",\"covers\":%d,\"offset\":%d}"
        "]"
        ",\"compressed\":false"
        ",\"interval_sec\":%" PRIu32 ",\"started_at\":%ld",
        (uint32_t)s.ring_count, (long)now,
        // #INT-1: v5 stream-export ТЕПЕРЬ с crc32 (как сегмент) → stride=WF_ROW_STRIDE=16410.
        // Раньше 16406 без crc (#FW-41); контроль целостности теперь и на export.aswf.
        WF_CHANNELS, WF_ROW_STRIDE,
        WF_CHANNELS,
        WF_ROW_BYTES,
        WF_ROW_BYTES + WF_DUR_BYTES,
        WF_ROW_BYTES + WF_DUR_BYTES + WF_TS_BYTES,
        WF_ROW_BYTES + WF_DUR_BYTES + WF_TS_BYTES + 4,
        WF_ROW_BYTES + WF_DUR_BYTES + WF_TS_BYTES + 8,
        WF_ROW_BYTES + WF_DUR_BYTES + WF_TS_BYTES + 12,   // temperature.offset=16402
        WF_ROW_PRECRC, WF_ROW_PRECRC,                     // crc32 covers=offset=16406
        s.interval_sec, (long)first_ts);
    if (n > 0 && n < cap && sp && sp->serial_number[0])
        n += snprintf(hbuf + n, cap - n, ",\"serial\":\"%s\"", sp->serial_number);
    if (n > 0 && n < cap && calib_export_present(sp)) {   // R7: нули/NaN = «не задана»
        n += snprintf(hbuf + n, cap - n, ",\"calibration\":[");
        for (int i = 0; i <= sp->calib_order && n > 0 && n < cap; i++)
            n += snprintf(hbuf + n, cap - n, "%s%.15g", i ? "," : "", sp->calibration[i]);
        if (n > 0 && n < cap) n += snprintf(hbuf + n, cap - n, "]");
    }
    if (n > 0 && n < cap) n += snprintf(hbuf + n, cap - n, "}");
    if (n < 0) n = 0;
    if (n > cap) n = cap;
    memset(hbuf + n, ' ', cap - n);

    uint8_t magic[8] = {'A','S','W','F', 0,0,0,0};
    uint32_t hlen = (uint32_t)WF_HDR_RESERVE;
    memcpy(magic + 4, &hlen, 4);

    char wf_name[48], wf_disp[80];                           // #FW-42: префикс
    web_build_export_name(wf_name, sizeof(wf_name), "waterfall.aswf");
    snprintf(wf_disp, sizeof(wf_disp), "attachment; filename=\"%s\"", wf_name);
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Content-Disposition", wf_disp);

    httpd_resp_send_chunk(req, (const char *)magic, 8);
    httpd_resp_send_chunk(req, hbuf, WF_HDR_RESERVE);

    uint16_t *durs = calloc(s.ring_count, sizeof(uint16_t));
    if (durs) spectrogram_copy_window_durations(durs, s.ring_count);
    float *temps = calloc(s.ring_count, sizeof(float));   // #FW-41
    if (temps) spectrogram_copy_window_temps(temps, s.ring_count);
    aswf_ctx_t ctx = {
        .req          = req,
        .tail         = tail,
        .durs         = durs,
        .temps        = temps,
        .r0           = r0,
        .r            = r0,
        .interval_sec = s.interval_sec,
        .row_start    = first_ts,
    };
    spectrogram_stream_window(row, s.ring_count, NULL, aswf_row_emit, &ctx);
    if (durs)  free(durs);
    if (temps) free(temps);

    httpd_resp_send_chunk(req, NULL, 0);

    heap_caps_free(row);
    free(hbuf);
    free(tail);
    if (sp) free(sp);
    http_io_gate_leave();
    return ESP_OK;
}

#define WF_N42_ACC 8192
#define WF_N42_ROWBUF (WF_ROW_BYTES + 128)

/* Эмитирует одно измерение в формате N42 XML */
static bool n42_emit_meas(httpd_req_t *req, char *acc, uint32_t id, int64_t start,
                          uint32_t dur, bool have_cal, const uint16_t *row)
{
    char tbuf[40]; struct tm tmv; time_t ts = (time_t)start;
    gmtime_r(&ts, &tmv);
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%dT%H:%M:%SZ", &tmv);
    int n = snprintf(acc, WF_N42_ACC,
        "  <RadMeasurement id=\"m-%" PRIu32 "\">\n    <MeasurementClassCode>Foreground</MeasurementClassCode>\n    <StartDateTime>%s</StartDateTime>\n    <RealTimeDuration>PT%" PRIu32 "S</RealTimeDuration>\n    <Spectrum id=\"m-%" PRIu32 "-s-1\" radDetectorInformationReference=\"det-1\"%s>\n      <LiveTimeDuration>PT%" PRIu32 "S</LiveTimeDuration>\n      <ChannelData compressionCode=\"CountedZeroes\">",
        id, tbuf, dur, id, have_cal ? " energyCalibrationReference=\"ecal-1\"" : "", dur);
    if (httpd_resp_send_chunk(req, acc, n) != ESP_OK) return false;
    int off = 0; bool first = true; uint32_t i = 0;
    while (i < WF_CHANNELS) {
        int wrote;
        if (row[i] == 0) {
            uint32_t z = 0;
            while (i < WF_CHANNELS && row[i] == 0) { z++; i++; }
            wrote = snprintf(acc + off, WF_N42_ACC - off, "%s0 %" PRIu32, first ? "" : " ", z);
        } else {
            wrote = snprintf(acc + off, WF_N42_ACC - off, "%s%u", first ? "" : " ", (unsigned)row[i]); i++;
        }
        off += wrote; first = false;
        if (off > WF_N42_ACC - 32) { if (httpd_resp_send_chunk(req, acc, off) != ESP_OK) return false; off = 0; }
    }
    if (off > 0) { if (httpd_resp_send_chunk(req, acc, off) != ESP_OK) return false; }
    n = snprintf(acc, WF_N42_ACC, "</ChannelData>\n    </Spectrum>\n  </RadMeasurement>\n");
    return httpd_resp_send_chunk(req, acc, n) == ESP_OK;
}

/* Контекст экспорта N42 */
typedef struct {
    httpd_req_t *req;
    char        *acc;
    uint8_t     *rowbuf;      /* WF_N42_ROWBUF bytes, PSRAM */
    char        *hdr;         /* WF_HDR_RESERVE + 1 bytes, PSRAM */
    bool         have_cal;
    uint32_t     id;          /* running RadMeasurement counter */
    uint32_t     def_interval;/* s.interval_sec — fallback when a segment header has no interval_sec */
    uint32_t     skipped;     /* segments/rows that disappeared before being read (logged at the end) */
} n42_exp_t;

/* Поток одного сегмента с флеш-памяти */
static int n42_stream_segment(n42_exp_t *x, uint32_t idx)
{
    FILE *f = NULL;
    int res = 0; /* 0 = fatal, 1 = ok/skipped */
    bool gate = false;   /* HEAVY-слот держим мы — отпустить на любом выходе */

    if (!spectrogram_seg_pin_read(idx)) { x->skipped++; return 1; }

    char path[128]; snprintf(path, sizeof(path), "%.90s/seg_%05" PRIu32 ".aswf", spectrogram_seg_dir(), idx);

    if (!http_io_gate_enter_wait(HTTP_GATE_WAIT_MS)) {
        ESP_LOGW(TAG, "n42 export: HEAVY gate timeout, abort");
        res = 0; goto cleanup;
    }
    gate = true;

    struct stat sb;
    /* Файл исчез/нечитаем/не ASWF — пропуск сегмента (res=1, skipped++), не обрыв экспорта. */
    res = 1;
    if (stat(path, &sb) != 0) { x->skipped++; goto cleanup; }

    f = fopen(path, "rb");
    if (!f) { x->skipped++; goto cleanup; }

    uint8_t magic[4]; uint32_t hlen_le = 0;
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "ASWF", 4) != 0) { x->skipped++; goto cleanup_close; }
    uint8_t hl_bytes[4];
    if (fread(hl_bytes, 1, 4, f) != 4) { x->skipped++; goto cleanup_close; }
    hlen_le = (uint32_t)hl_bytes[0] | ((uint32_t)hl_bytes[1] << 8) | ((uint32_t)hl_bytes[2] << 16) | ((uint32_t)hl_bytes[3] << 24);

    if (hlen_le < 64 || hlen_le > WF_HDR_RESERVE) { x->skipped++; goto cleanup_close; }

    if (fread(x->hdr, 1, hlen_le, f) != hlen_le) { x->skipped++; goto cleanup_close; }
    x->hdr[hlen_le] = '\0';

    wf_seg_hdr_info_t info; wf_seg_hdr_parse(x->hdr, &info);
    long poff = wf_seg_payload_offset(info.version, hlen_le);
    uint32_t rows = wf_seg_rows_in((long)sb.st_size, poff, info.stride);

    if (fseek(f, poff, SEEK_SET) != 0) { x->skipped++; goto cleanup_close; }

    http_io_gate_leave();
    gate = false;   /* в сеть — только с отпущенным слотом */

    uint32_t iv = info.interval_sec ? info.interval_sec : x->def_interval;
    uint64_t cum = 0;

    for (uint32_t r = 0; r < rows; r++) {
        if (!http_io_gate_enter_wait(HTTP_GATE_WAIT_MS)) {
            ESP_LOGW(TAG, "n42 export: HEAVY gate timeout, abort");
            res = 0; goto cleanup_close;
        }

        size_t rd = fread(x->rowbuf, 1, info.stride, f);
        http_io_gate_leave();

        if (rd != info.stride) break;

        uint32_t dur = wf_exp_eff_dur(wf_row_dur(x->rowbuf, info.stride), iv);
        int64_t start = wf_exp_row_start(wf_row_ts(x->rowbuf, info.version, info.stride), info.started_at, cum);
        cum += dur;

        if (!n42_emit_meas(x->req, x->acc, x->id++, start, dur, x->have_cal, (const uint16_t *)x->rowbuf)) {
            res = 0; goto cleanup_close;
        }
    }

    res = 1;

cleanup_close:
    if (f) fclose(f);
cleanup:
    if (gate) http_io_gate_leave();
    spectrogram_seg_unpin_read(idx);
    return res;
}

/* Поток строк из кольцевого буфера */
static bool n42_stream_ring(n42_exp_t *x, uint32_t a, uint32_t b, uint32_t r0, time_t base,
                            const uint16_t *durs, uint32_t ndurs, uint32_t epoch,
                            uint32_t *rg, uint64_t *rcum)
{
    while (*rg < a) {
        uint32_t idx = *rg - r0;
        uint16_t d = (idx < ndurs) ? durs[idx] : 0;
        *rcum += wf_exp_eff_dur(d, x->def_interval);
        (*rg)++;
    }

    for (uint32_t g = a; g < b; g++) {
        uint32_t idx = g - r0;
        uint16_t d = (idx < ndurs) ? durs[idx] : 0;
        uint32_t dur = wf_exp_eff_dur(d, x->def_interval);
        int64_t start = (int64_t)base + (int64_t)*rcum;

        if (spectrogram_copy_ring_row(g, epoch, (uint16_t *)x->rowbuf)) {
            if (!n42_emit_meas(x->req, x->acc, x->id++, start, dur, x->have_cal, (const uint16_t *)x->rowbuf)) {
                return false;
            }
        } else {
            x->skipped++;
        }

        *rcum += dur;
        *rg = g + 1;
    }

    return true;
}

/* GET /api/waterfall/export.n42[?ring=1] -> ANSI N42.42-2011 XML.
   #FW-19 (sweep-A): ПОЛНАЯ история, а не только кольцо PSRAM (256 строк):
   завершённые сегменты с flash (старые — по idx; текущей сессии — вперемешку с участками
   кольца по глобальному индексу строки, план wf_exp_plan) + строки, которых на flash ещё
   нет (открытый сегмент, не сброшенные писателем, набранные при persist=off) — из кольца.
   ?ring=1 — прежнее поведение (только кольцо). Каждая строка = <RadMeasurement> со
   спектром-дельтой, ChannelData — CountedZeroes (формат строки не менялся).
   Потоково, без буфера в RAM на весь объём: одна строка (16,5 КБ PSRAM) + 8 КБ текста.
   HEAVY-слот берётся только вокруг fopen/fread строки и отпускается перед КАЖДОЙ отправкой
   в сеть — фоновые автосейв/бэкап не стоят весь экспорт. Сегмент на время чтения запинен
   (#REC-12). Калибровка — calib_export_present (R7). Обрыв посреди выдачи (сеть, гейт) —
   ESP_FAIL без закрывающего тега и без финального чанка: клиент видит разрыв, а не
   «целый» усечённый файл. Калибровка в файле одна — текущая прибора (как и раньше);
   смена калибровки между сегментами (#FW-62 calib_changed) в n42 не отражается. */
static esp_err_t h_export_n42(httpd_req_t *req)
{
    esp_err_t ret = ESP_FAIL;   /* ESP_OK — только после финального чанка */
    bool ring_only = false;
    char q[32];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        char v[8];
        if (httpd_query_key_value(q, "ring", v, sizeof(v)) == ESP_OK && v[0] == '1') ring_only = true;
    }

    wf_seg_reg_t *reg = NULL;
    wf_exp_step_t *plan = NULL;
    uint16_t *durs = NULL;
    uint8_t *rowbuf = NULL;
    char *hdr = NULL;
    char *acc = NULL;
    spectrum_data_t *sp = NULL;

    reg = heap_caps_malloc(sizeof(wf_seg_reg_t) * WF_SEG_REG_MAX, MALLOC_CAP_SPIRAM);
    if (!reg) goto fail_oom;

    const int plan_cap = 2 * WF_SEG_REG_MAX + 2;
    plan = heap_caps_malloc(sizeof(wf_exp_step_t) * plan_cap, MALLOC_CAP_SPIRAM);
    if (!plan) goto fail_oom;

    durs = heap_caps_calloc(WF_RING_ROWS_DEFAULT, sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (!durs) goto fail_oom;

    rowbuf = heap_caps_malloc(WF_N42_ROWBUF, MALLOC_CAP_SPIRAM);
    if (!rowbuf) goto fail_oom;

    hdr = heap_caps_malloc(WF_HDR_RESERVE + 1, MALLOC_CAP_SPIRAM);
    if (!hdr) goto fail_oom;

    acc = malloc(WF_N42_ACC);
    if (!acc) goto fail_oom;

    sp = heap_caps_malloc(sizeof(spectrum_data_t), MALLOC_CAP_SPIRAM);
    if (sp) {
        memset(sp, 0, sizeof(*sp));
        spectrum_get_meta(sp);
    }

    wf_status_t s; uint32_t epoch = 0;
    int nreg = spectrogram_export_snapshot(reg, WF_SEG_REG_MAX, durs, WF_RING_ROWS_DEFAULT, &s, &epoch);
    if (nreg < 0) nreg = 0;
    if (ring_only) nreg = 0;

    uint32_t r0 = (s.ring_count <= s.total_rows) ? (s.total_rows - s.ring_count) : 0;
    int nsteps = wf_exp_plan(reg, nreg, r0, s.total_rows, plan, plan_cap);

    if (nsteps <= 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no waterfall data");
        goto cleanup;
    }

    char wf_name[48], wf_disp[80];
    web_build_export_name(wf_name, sizeof(wf_name), "waterfall.n42");
    snprintf(wf_disp, sizeof(wf_disp), "attachment; filename=\"%s\"", wf_name);
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Content-Disposition", wf_disp);

    bool have_cal = sp ? calib_export_present(sp) : false;

    int n;
    n = snprintf(acc, WF_N42_ACC,
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<RadInstrumentData xmlns=\"http://physics.nist.gov/N42/2011/N42\">\n"
        "  <RadInstrumentInformation id=\"inst-1\">\n"
        "    <RadInstrumentManufacturerName>KB Radar</RadInstrumentManufacturerName>\n"
        "    <RadInstrumentModelName>Atom Spectra</RadInstrumentModelName>\n");
    if (httpd_resp_send_chunk(req, acc, n) != ESP_OK) goto cleanup;

    if (sp && sp->serial_number[0]) {
        char esc[512];
        web_xml_escape(sp->serial_number, esc, sizeof(esc));
        n = snprintf(acc, WF_N42_ACC, "    <RadInstrumentIdentifier>%s</RadInstrumentIdentifier>\n", esc);
        if (httpd_resp_send_chunk(req, acc, n) != ESP_OK) goto cleanup;
    }

    n = snprintf(acc, WF_N42_ACC,
        "    <RadInstrumentClassCode>Spectroscopic Personal Radiation Detector</RadInstrumentClassCode>\n"
        "  </RadInstrumentInformation>\n"
        "  <RadDetectorInformation id=\"det-1\">\n"
        "    <RadDetectorCategoryCode>Gamma</RadDetectorCategoryCode>\n"
        "    <RadDetectorKindCode>CsI</RadDetectorKindCode>\n"
        "  </RadDetectorInformation>\n");
    if (httpd_resp_send_chunk(req, acc, n) != ESP_OK) goto cleanup;

    if (have_cal) {
        n = snprintf(acc, WF_N42_ACC, "  <EnergyCalibration id=\"ecal-1\">\n    <CoefficientValues>");
        if (httpd_resp_send_chunk(req, acc, n) != ESP_OK) goto cleanup;
        for (int i = 0; i <= sp->calib_order; i++) {
            n = snprintf(acc, WF_N42_ACC, "%s%.9g", i ? " " : "", sp->calibration[i]);
            if (httpd_resp_send_chunk(req, acc, n) != ESP_OK) goto cleanup;
        }
        n = snprintf(acc, WF_N42_ACC, "</CoefficientValues>\n  </EnergyCalibration>\n");
        if (httpd_resp_send_chunk(req, acc, n) != ESP_OK) goto cleanup;
    }

    n42_exp_t x = { .req = req, .acc = acc, .rowbuf = rowbuf, .hdr = hdr, .have_cal = have_cal, .id = 0, .def_interval = s.interval_sec, .skipped = 0 };

    time_t base = s.started_at + (time_t)r0 * (time_t)s.interval_sec;
    uint32_t ndurs = s.ring_count < WF_RING_ROWS_DEFAULT ? s.ring_count : WF_RING_ROWS_DEFAULT;
    uint32_t rg = r0;
    uint64_t rcum = 0;

    for (int i = 0; i < nsteps; i++) {
        if (plan[i].kind == WF_EXP_STEP_SEG) {
            if (n42_stream_segment(&x, plan[i].a) == 0) goto cleanup;
        } else if (plan[i].kind == WF_EXP_STEP_RING) {
            if (!n42_stream_ring(&x, plan[i].a, plan[i].b, r0, base, durs, ndurs, epoch, &rg, &rcum)) goto cleanup;
        }
    }

    n = snprintf(acc, WF_N42_ACC, "</RadInstrumentData>\n");
    if (httpd_resp_send_chunk(req, acc, n) != ESP_OK) goto cleanup;
    if (httpd_resp_send_chunk(req, NULL, 0) != ESP_OK) goto cleanup;
    ret = ESP_OK;

    if (x.skipped) {
        ESP_LOGW(TAG, "n42 export: %" PRIu32 " segment(s)/row(s) vanished before read", x.skipped);
    }
    ESP_LOGI(TAG, "n42 export: %" PRIu32 " measurements, %d plan steps%s", x.id, nsteps, ring_only ? " (ring only)" : "");

cleanup:
    heap_caps_free(reg);
    heap_caps_free(plan);
    heap_caps_free(durs);
    heap_caps_free(rowbuf);
    heap_caps_free(hdr);
    free(acc);
    heap_caps_free(sp);
    return ret;

fail_oom:
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    goto cleanup;
}

/* ---- #REC-11-A1: листинг и отдача сегментов /storage/wf (СТРОГО read-only) ---- */

/* Строгая валидация имени сегмента — anti-traversal для /segment?name=.
   Допускается РОВНО "seg_" + ≥1 десятичная цифра + ".aswf". Любые '/', '\\',
   '.' (кроме расширения), пробелы — отвергаются. Длина 10..24
   ("seg_0.aswf" = 10; запас под многозначный монотонный индекс). */
static bool wf_seg_name_ok(const char *name)
{
    size_t len = name ? strlen(name) : 0;
    if (len < 10 || len > 24) return false;
    if (strncmp(name, "seg_", 4) != 0) return false;
    const char *suf = name + len - 5;              // позиция ".aswf"
    if (strcmp(suf, ".aswf") != 0) return false;
    if (name + 4 == suf) return false;             // должна быть ≥1 цифра
    for (const char *p = name + 4; p < suf; p++)
        if (*p < '0' || *p > '9') return false;    // между seg_ и .aswf — только цифры
    return true;
}

/* #FW-60: wf_seg_read_ids() удалена — чтение шапки при листинге больше не нужно.
   Идентификаторы сегмента (seg_seq, started_at) ведутся в RAM-реестре с момента
   создания файла; единственное чтение шапки с flash осталось в seg_reconcile()
   при старте (spectrogram.c, seg_read_ids_open), где файл и так уже открыт. */

/**
 * Обработчик /segments.
 * Возвращает список сегментов с полями name, idx, bytes, rows, finalized, seg_seq, started_at.
 * seg_seq и started_at читаются из шапки файла и равны null, если файл повреждён или не прочитан.
 * Использует PSRAM для буфера заголовка.
 */
static esp_err_t h_segments(httpd_req_t *req)
{
    /* #FW-60: листинг больше НЕ трогает flash — данные берутся из RAM-реестра, который
       ведётся в точках изменения seg_count. Поэтому HEAVY-гейт здесь не нужен: раньше он
       стоял из-за opendir/stat/fopen по каждому файлу (max 1550 мс при четырёх сегментах),
       теперь обработчик — чистая сериализация из памяти и относится к LIVE-классу.
       Снимок берётся под локом статуса и копируется на стек, дальше лок отпущен: сеть
       не удерживает состояние спектрограммы. */
    /* Снимок берём в PSRAM, а НЕ на стеке: 256 записей × 24 Б = 6 КБ, что превышает
       стек задачи httpd. Первая редакция объявляла массив локально — переполнение
       стека вместо ответа. */
    wf_seg_reg_t *reg = heap_caps_malloc(sizeof(wf_seg_reg_t) * WF_SEG_REG_MAX,
                                         MALLOC_CAP_SPIRAM);
    if (!reg) reg = malloc(sizeof(wf_seg_reg_t) * WF_SEG_REG_MAX);
    if (!reg) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }
    int nreg = spectrogram_seg_registry_snapshot(reg, WF_SEG_REG_MAX);
    /* #FW-61: spectrogram_seg_open_index() отсюда УБРАН — он берёт s_fs_lock без таймаута,
       то есть обработчик мог встать на секунды на ролловере сегмента (fsync ~1 МБ) и
       занять воркер LIVE-полосы. Это ровно то, от чего защищал снятый HEAVY-гейт, и делало
       комментарий «flash не трогаем» неверным. Признак открытого сегмента реестр знает сам:
       finalized==false. */
    httpd_resp_set_type(req, "application/json");

    if (nreg <= 0) {
        free(reg);
        httpd_resp_sendstr(req, "[]");
        return ESP_OK;
    }

    httpd_resp_send_chunk(req, "[", 1);
    for (int i = 0; i < nreg; i++) {
        /* bytes выводится из rows обратной формулой (та же геометрия, что у читателя
           файла). Раньше бралось из stat(), но ради одного поля возвращать обход
           каталога бессмысленно: размер однозначно определяется числом строк. */
        /* #FW-61: bytes — фактический размер из реестра (stat), а не формула по числу
           строк: у сегментов старых версий формата другой stride и нет baseline-секции,
           и вычисленное значение расходилось с файлом — приёмник отвергал такой сегмент
           каждый проход, пока кольцо его не стирало. */
        long rows  = (long)reg[i].rows;
        long bytes = (long)reg[i].bytes;
        uint32_t idx = reg[i].idx;
        bool finalized = reg[i].finalized;

        char seq_s[16], sat_s[24];
        if (reg[i].seg_seq) snprintf(seq_s, sizeof(seq_s), "%" PRIu32, reg[i].seg_seq);
        else                snprintf(seq_s, sizeof(seq_s), "null");
        if (reg[i].started_at) snprintf(sat_s, sizeof(sat_s), "%lld", (long long)reg[i].started_at);
        else                   snprintf(sat_s, sizeof(sat_s), "null");

        char line[256];
        int n = snprintf(line, sizeof(line),
                        "%s{\"name\":\"seg_%05" PRIu32 ".aswf\",\"idx\":%" PRIu32 ","
                        "\"bytes\":%ld,\"rows\":%ld,\"finalized\":%s,"
                        "\"seg_seq\":%s,\"started_at\":%s}",
                        i ? "," : "", idx, idx, bytes, rows,
                        finalized ? "true" : "false", seq_s, sat_s);
        /* snprintf возвращает ЖЕЛАЕМУЮ длину: без этого зажима при усечении в сеть ушло бы
           больше байт, чем есть в буфере (чтение за его границей). Сейчас максимум ~157 Б
           при предельных значениях, но буфер уменьшен с 512 до 256 — запас сократился. */
        if (n < 0) continue;
        if (n >= (int)sizeof(line)) n = (int)sizeof(line) - 1;
        if (httpd_resp_send_chunk(req, line, n) != ESP_OK) { free(reg); return ESP_FAIL; }
    }
    free(reg);
    httpd_resp_send_chunk(req, "]", 1);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* GET /api/waterfall/segment?name=seg_NNNNN.aswf -> сырой файл сегмента.
   ИНВАРИАНТ #REC-11-A1: СТРОГО read-only — fopen("rb"), НИКОГДА не удаляем файл
   при отдаче (удаление сегментов — только кольцо keep-last или фаза A2-аплоадер). */
static esp_err_t h_segment(httpd_req_t *req)
{
    /* #PERF-2: у этого эндпоинта есть клиенты, которые на 503 не ретраят в том
       же проходе (scripts/wf_pull_client.py, внешний wf-recorder), поэтому
       короткое ожидание слота предпочтительнее немедленного отказа: сегмент
       при этом не удаляется и заберётся следующим проходом. Ожидание намеренно
       короткое — см. WF_SEGMENT_GATE_WAIT_MS. */
    if (!http_io_gate_enter_wait_or_503(req, WF_SEGMENT_GATE_WAIT_MS)) return ESP_OK;
    char query[96], name[40];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "name", name, sizeof(name)) != ESP_OK) {
        http_io_gate_leave();
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "name required");
        return ESP_FAIL;
    }
    if (!wf_seg_name_ok(name)) {
        http_io_gate_leave();
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad name");
        return ESP_FAIL;
    }
    // #REC-12 (sweep-A): пин ДО fopen, зеркально push-пути (wf_offload/s_seg_pinned) —
    // кольцо make_room и прочие удаления не сотрут файл, пока он уходит в сеть. Снимается
    // на КАЖДОМ выходе ниже: успех, 404, oom, обрыв клиента. Отказ пина = файл прямо
    // сейчас удаляется: 503 + Retry-After — повтор получит либо файл, либо честный 404.
    uint32_t idx = (uint32_t)strtoul(name + 4, NULL, 10);
    if (idx == WF_PIN_NONE) {                 // 4294967295 — такого сегмента быть не может
        http_io_gate_leave();
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
        return ESP_FAIL;
    }
    if (!spectrogram_seg_pin_read(idx)) {
        http_io_gate_leave();
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Retry-After", "1");
        httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"segment-busy\"}");
        return ESP_OK;
    }
    char path[128];
    // %.90s/%.30s: доказуемая граница для -Wformat-truncation (name[40] валидно ≤24).
    snprintf(path, sizeof(path), "%.90s/%.30s", spectrogram_seg_dir(), name);
    FILE *f = fopen(path, "rb");      // read-only: отдаём, не трогая файл
    if (!f) {
        spectrogram_seg_unpin_read(idx);
        http_io_gate_leave();
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
        return ESP_FAIL;
    }

    char *bufp = malloc(4096);
    if (!bufp) {
        fclose(f);
        spectrogram_seg_unpin_read(idx);
        http_io_gate_leave();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }

    char disp[80];
    snprintf(disp, sizeof(disp), "attachment; filename=\"%s\"", name);
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Content-Disposition", disp);

    size_t rd;
    while ((rd = fread(bufp, 1, 4096, f)) > 0) {
        if (httpd_resp_send_chunk(req, bufp, rd) != ESP_OK) {
            free(bufp);
            fclose(f);
            spectrogram_seg_unpin_read(idx);   // обрыв клиента
            http_io_gate_leave();
            return ESP_FAIL;
        }
    }
    free(bufp);
    fclose(f);
    spectrogram_seg_unpin_read(idx);
    httpd_resp_send_chunk(req, NULL, 0);
    http_io_gate_leave();
    return ESP_OK;
}

/* #REC-11 pull: POST /api/waterfall/segment/delete?name=seg_NNNNN.aswf
   PC-клиент подтверждает приём сегмента → удаляем его с Flash. CSRF-protected
   (мутирующий). Имя валидируется wf_seg_name_ok (anti-traversal); удаляется ТОЛЬКО
   завершённый сегмент (не открытый, не pinned). Pull-модель: ПК сам инициирует
   соединение и забирает сегмент через GET /segment, затем этим POST освобождает
   Flash — нулевая входящая поверхность на рабочем ПК (никаких открытых портов). */
static esp_err_t h_segment_delete(httpd_req_t *req)
{
    if (!web_csrf_check(req)) return ESP_FAIL;
    char query[96], name[40];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "name", name, sizeof(name)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "name required");
        return ESP_FAIL;
    }
    if (!wf_seg_name_ok(name)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad name");
        return ESP_FAIL;
    }
    uint32_t idx = (uint32_t)strtoul(name + 4, NULL, 10);
    // #HTTP-FS1: удаляет wf_fs_task; здесь только постановка в очередь — задача httpd
    // не ждёт ни http_io_gate, ни FSLOCK (стоп всех клиентов до 5 с). Открытый/неизвестный
    // сегмент отсекается сразу по RAM-реестру (прежний not-deletable); pinned и прочее
    // wf_fs_task пропускает с WARN — сегмент остаётся в листинге, клиент повторит ack.
    httpd_resp_set_type(req, "application/json");
    int q = spectrogram_seg_delete_async(idx);
    if (q == 0) {
        httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"not-deletable\"}");
        return ESP_OK;
    }
    if (q < 0) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_hdr(req, "Retry-After", "5");
        httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"busy\"}");
        return ESP_OK;
    }
    httpd_resp_sendstr(req, "{\"ok\":true,\"queued\":true}");
    return ESP_OK;
}

static esp_err_t h_page(httpd_req_t *req)
{
    extern const uint8_t waterfall_html_start[] asm("_binary_waterfall_html_start");
    extern const uint8_t waterfall_html_end[]   asm("_binary_waterfall_html_end");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)waterfall_html_start,
                    waterfall_html_end - waterfall_html_start);
    return ESP_OK;
}

static esp_err_t h_ws(httpd_req_t *req)
{
    // N1 (ревью-2): h_ws — ЕДИНСТВЕННЫЙ обработчик и рукопожатия, и КАЖДОГО
    // входящего кадра клиента (второй if ниже) — метим тут, безусловно,
    // чтобы клиентские кадры продолжали считаться активностью, не только
    // сам факт подключения (choke point для этого одного обработчика).
    web_server_note_request_activity(req->uri);
    if (req->method == HTTP_GET) {
        int fd = httpd_req_to_sockfd(req);
        ws_add(fd);
        wf_status_t s;
        spectrogram_get_status(&s);
        char hdr[512];
        int hn = snprintf(hdr, sizeof(hdr),
            "{\"type\":\"header\",\"channels\":%d,\"interval_sec\":%" PRIu32 ",\"total_rows\":%" PRIu32,
            WF_CHANNELS, s.interval_sec, s.total_rows);
        hn = append_calib_json(hdr, hn, sizeof(hdr));
        hn += snprintf(hdr + hn, sizeof(hdr) - hn, "}");
        httpd_ws_frame_t fr = { 0 };
        fr.type    = HTTPD_WS_TYPE_TEXT;
        fr.payload = (uint8_t *)hdr;
        fr.len     = hn;
        httpd_ws_send_frame(req, &fr);
        ESP_LOGI(TAG, "ws client %d connected", fd);
        return ESP_OK;
    }
    httpd_ws_frame_t fr = { 0 };
    esp_err_t r = httpd_ws_recv_frame(req, &fr, 0);
    if (r != ESP_OK) return r;
    if (fr.type == HTTPD_WS_TYPE_CLOSE) {
        ws_del(httpd_req_to_sockfd(req));
    } else if (fr.len) {
        // P3-6: вычитать кадр ЦЕЛИКОМ. httpd_ws_recv_frame с max_len < длины
        // кадра вернёт ESP_ERR_INVALID_SIZE и НЕ извлечёт payload — остаток
        // повредил бы разбор следующего кадра. UI шлёт только мелкие
        // контрол-кадры; аномально большой — повод закрыть соединение.
        if (fr.len > 512) {
            ws_del(httpd_req_to_sockfd(req));
            return ESP_OK;
        }
        uint8_t tmp[512];
        fr.payload = tmp;
        httpd_ws_recv_frame(req, &fr, fr.len);
    }
    return ESP_OK;
}

/* #REC-11-A2: GET /api/waterfall/offload — конфиг + рантайм-статистика выгрузки.
   Пароль НИКОГДА не отдаётся наружу — только флаг has_pass. */
static esp_err_t h_offload_get(httpd_req_t *req)
{
    wf_offload_cfg_t  c; wf_offload_get_cfg(&c);
    wf_offload_stat_t s; wf_offload_get_stat(&s);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject  (root, "enabled",     c.enabled);
    cJSON_AddStringToObject(root, "url",         c.url);
    cJSON_AddStringToObject(root, "user",        c.user);
    cJSON_AddBoolToObject  (root, "has_pass",    c.pass[0] != 0);
    cJSON_AddNumberToObject(root, "sent_ok",     s.sent_ok);
    cJSON_AddNumberToObject(root, "failed",      s.failed);
    cJSON_AddNumberToObject(root, "last_status", s.last_status);
    cJSON_AddNumberToObject(root, "last_ok_at",  (double)s.last_ok_at);
    cJSON_AddBoolToObject  (root, "busy",        s.busy);
    char *out = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    if (!out) httpd_resp_set_status(req, "503 Service Unavailable");   // F-09 (разбор pass3): нет памяти на JSON
    httpd_resp_sendstr(req, out ? out : "{\"ok\":false,\"err\":\"oom\"}");
    if (out) free(out);
    cJSON_Delete(root);
    return ESP_OK;
}

/* #REC-11-A2: POST /api/waterfall/offload — задать конфиг выгрузки.
   Стартуем от текущего конфига: ключ "pass" отсутствует → прежний пароль
   сохраняется (UI не пересылает пароль обратно). */
static esp_err_t h_offload_set(httpd_req_t *req)
{
    if (!web_csrf_check(req)) return ESP_FAIL;
    char body[512] = { 0 };
    int rl = httpd_req_recv(req, body, sizeof(body) - 1);
    if (rl <= 0) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty"); return ESP_FAIL; }
    body[rl] = 0;
    cJSON *root = cJSON_Parse(body);
    if (!root) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "json"); return ESP_FAIL; }

    wf_offload_cfg_t c;
    wf_offload_get_cfg(&c);                              // сохранить pass при отсутствии ключа
    cJSON *en   = cJSON_GetObjectItem(root, "enabled");
    if (cJSON_IsBool(en))     c.enabled = cJSON_IsTrue(en);
    cJSON *url  = cJSON_GetObjectItem(root, "url");
    if (cJSON_IsString(url))  snprintf(c.url,  sizeof(c.url),  "%s", url->valuestring);
    cJSON *user = cJSON_GetObjectItem(root, "user");
    if (cJSON_IsString(user)) snprintf(c.user, sizeof(c.user), "%s", user->valuestring);
    cJSON *pass = cJSON_GetObjectItem(root, "pass");
    if (cJSON_IsString(pass)) snprintf(c.pass, sizeof(c.pass), "%s", pass->valuestring);
    cJSON_Delete(root);

    int r = wf_offload_set_cfg(&c);
    httpd_resp_set_type(req, "application/json");
    if (r == WF_OFFLOAD_OK) { httpd_resp_sendstr(req, "{\"ok\":true}"); return ESP_OK; }
    const char *e = (r == WF_OFFLOAD_ERR_BLOCKED) ? "narodmon-blocked"
                  : (r == WF_OFFLOAD_ERR_INVALID) ? "invalid-url"
                  : (r == WF_OFFLOAD_ERR_NVS)     ? "nvs" : "error";
    char msg[64];
    snprintf(msg, sizeof(msg), "{\"ok\":false,\"err\":\"%s\"}", e);
    httpd_resp_sendstr(req, msg);
    return ESP_OK;
}

/* v3: GET /api/waterfall/dose_curve — статус загруженной кривой */
static esp_err_t h_dose_curve_get(httpd_req_t *req)
{
    int n = spectrogram_get_dose_curve_n();
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"n\":%d,\"mode\":\"%s\"}",
             n, n > 0 ? "curve" : "scalar");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

/* v3: POST /api/waterfall/dose_curve — загрузить CSV-файл кривой */
static esp_err_t h_dose_curve_set(httpd_req_t *req)
{
    if (!web_csrf_check(req)) return ESP_FAIL;
    int len = req->content_len;
    if (len <= 0 || len > 64 * 1024) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad size");
        return ESP_FAIL;
    }
    FILE *f = fopen("/storage/dose_curve.csv", "w");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "FS error");
        return ESP_FAIL;
    }
    char buf[512];
    int remaining = len;
    while (remaining > 0) {
        int to_read = remaining < (int)sizeof(buf) ? remaining : (int)sizeof(buf);
        int rd = httpd_req_recv(req, buf, to_read);
        if (rd <= 0) { fclose(f); unlink("/storage/dose_curve.csv"); return ESP_FAIL; }
        fwrite(buf, 1, rd, f);
        remaining -= rd;
    }
    fclose(f);
    spectrogram_load_dose_curve();
    int n = spectrogram_get_dose_curve_n();
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"n\":%d}", n);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

/* v3: POST /api/waterfall/dose_curve/clear — удалить кривую */
static esp_err_t h_dose_curve_clear(httpd_req_t *req)
{
    if (!web_csrf_check(req)) return ESP_FAIL;
    unlink("/storage/dose_curve.csv");
    spectrogram_load_dose_curve();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"n\":0}");
    return ESP_OK;
}

/* v3: GET /api/waterfall/dose_k */
static esp_err_t h_dose_k_get(httpd_req_t *req)
{
    float k = spectrogram_get_dose_k();
    char resp[48];
    snprintf(resp, sizeof(resp), "{\"dose_k\":%.8g}", (double)k);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

/* v3: POST /api/waterfall/dose_k  body: {"dose_k": <float>} */
static esp_err_t h_dose_k_set(httpd_req_t *req)
{
    if (!web_csrf_check(req)) return ESP_FAIL;
    char body[64] = {0};
    int recv_len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (recv_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    body[recv_len] = '\0';
    cJSON *root = cJSON_Parse(body);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad JSON");
        return ESP_FAIL;
    }
    cJSON *it = cJSON_GetObjectItem(root, "dose_k");
    if (it && cJSON_IsNumber(it))
        spectrogram_set_dose_k((float)cJSON_GetNumberValue(it));
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

// F1 (итоговое ревью 25.09): reg() — choke point регистрации REST водопада,
// тот же приём, что трамплин над uris[] в web_server.c (индекс в user_ctx,
// простой массив функций) — отмечает активность ДО вызова настоящего
// обработчика, без правки тел ~19 обработчиков этого файла.
#define WF_REG_MAX 24
// N4 (ревью-2): reg() раньше не проверяла границу — лишний вызов при
// доработке молча писал бы за пределы s_wf_wrap_handlers[]. Число ниже —
// ФАКТИЧЕСКОЕ количество вызовов reg() в web_waterfall_register() (посчитано
// grep'ом на момент фикса); _Static_assert ловит на СБОРКЕ, если кто-то
// поднимет счётчик без пересмотра WF_REG_MAX, runtime-guard в reg() (ниже) —
// если само число регистраций разойдётся сборка/рантайм.
#define WF_REG_COUNT_KNOWN 19
_Static_assert(WF_REG_COUNT_KNOWN <= WF_REG_MAX,
               "WF_REG_MAX меньше известного числа reg()-регистраций");
static esp_err_t (*s_wf_wrap_handlers[WF_REG_MAX])(httpd_req_t *);
static int s_wf_wrap_count;

static esp_err_t wf_activity_trampoline(httpd_req_t *req)
{
    size_t idx = (size_t)(intptr_t)req->user_ctx;
    web_server_note_request_activity(req->uri);
    return s_wf_wrap_handlers[idx](req);
}

static void reg(httpd_handle_t srv, const char *uri, httpd_method_t m,
                esp_err_t (*h)(httpd_req_t *))
{
    // N4: рантайм-защёлка — _Static_assert выше проверяет ИЗВЕСТНОЕ число
    // вызовов, но не спасает, если оно разойдётся со счётчиком в рантайме;
    // громкий abort лучше тихой порчи памяти соседних статических данных.
    assert(s_wf_wrap_count < WF_REG_MAX && "WF_REG_MAX overflow (reg())");
    int idx = s_wf_wrap_count++;
    s_wf_wrap_handlers[idx] = h;
    httpd_uri_t u = { .uri = uri, .method = m, .handler = wf_activity_trampoline,
                       .user_ctx = (void *)(intptr_t)idx };
    // issue #52b (sweep-B задача 7): громкий отказ вместо тихого 404.
    esp_err_t rerr = httpd_register_uri_handler(srv, &u);
    if (rerr != ESP_OK)
        ESP_LOGE(TAG, "issue#52b: register '%s' failed: %s", uri, esp_err_to_name(rerr));
}

// LK-08 (1.2.30): долгие выдачи (окно, экспорт, сегмент) идут в отдельной задаче через
// httpd_req_async_handler_begin — поток httpd свободен на время передачи (опросы UI, WS-кадры).
#define WF_DL_MAX         1      // одновременно одна выдача: внутренняя RAM под стек задачи ограничена
#define WF_DL_STACK       6144   // 7168 был на пределе крупнейшего внутреннего блока (7168 Б на гейте 1.2.30); запас смотреть по логу «wf_dl: stack min free»
static volatile int s_dl_active;
typedef struct { httpd_req_t *req; esp_err_t (*h)(httpd_req_t *); volatile int *cnt; } wf_dl_job_t;

static void wf_dl_task(void *arg)
{
    wf_dl_job_t j = *(wf_dl_job_t *)arg;
    free(arg);
    esp_err_t rc = j.h(j.req);
    ESP_LOGI(TAG, "wf_dl: stack min free %u B (of %d)", (unsigned)uxTaskGetStackHighWaterMark(NULL), WF_DL_STACK);
    __atomic_fetch_sub(j.cnt, 1, __ATOMIC_SEQ_CST);   // до complete: следующий запрос того же клиента не получит ложный 503
    // ESP_FAIL = обрыв посреди выдачи (контракт синхронных обработчиков): закрыть сессию, клиент увидит разрыв
    if (rc != ESP_OK) httpd_sess_trigger_close(j.req->handle, httpd_req_to_sockfd(j.req));
    httpd_req_async_handler_complete(j.req);
    vTaskDelete(NULL);
}
static esp_err_t wf_dl_busy(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Retry-After", "2");
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_sendstr(req, "busy");
    return ESP_OK;
}

static esp_err_t wf_dl_async(httpd_req_t *req, esp_err_t (*h)(httpd_req_t *), volatile int *cnt, int cmax)
{
    if (__atomic_add_fetch(cnt, 1, __ATOMIC_SEQ_CST) > cmax) {
        __atomic_fetch_sub(cnt, 1, __ATOMIC_SEQ_CST);
        return wf_dl_busy(req);
    }
    httpd_req_t *cp = NULL;
    wf_dl_job_t *j = malloc(sizeof(*j));
    if (!j || httpd_req_async_handler_begin(req, &cp) != ESP_OK) {
        free(j);
        __atomic_fetch_sub(cnt, 1, __ATOMIC_SEQ_CST);
        return wf_dl_busy(req);
    }
    j->req = cp; j->h = h; j->cnt = cnt;
    if (xTaskCreatePinnedToCore(wf_dl_task, "wf_dl", WF_DL_STACK, j, 5, NULL, 1) != pdPASS) {
        // Живой гейт 1.2.30: крупнейший свободный блок внутренней RAM ~7 КБ — стек может не поместиться. Не отказ, а прежнее
        // поведение: обработчик выполняется здесь же (поток httpd занят, как до 1.2.30), копия запроса закрывается как обычно.
        free(j);
        ESP_LOGW(TAG, "wf_dl: task create failed (int largest %u) -> synchronous fallback",
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        esp_err_t rc = h(cp);
        __atomic_fetch_sub(cnt, 1, __ATOMIC_SEQ_CST);
        if (rc != ESP_OK) httpd_sess_trigger_close(cp->handle, httpd_req_to_sockfd(cp));
        httpd_req_async_handler_complete(cp);
    }
    return ESP_OK;
}

static esp_err_t h_window_async(httpd_req_t *req)      { return wf_dl_async(req, h_window, &s_dl_active, WF_DL_MAX); }
static esp_err_t h_export_aswf_async(httpd_req_t *req) { return wf_dl_async(req, h_export_aswf, &s_dl_active, WF_DL_MAX); }
static esp_err_t h_export_n42_async(httpd_req_t *req)  { return wf_dl_async(req, h_export_n42, &s_dl_active, WF_DL_MAX); }
static esp_err_t h_segment_async(httpd_req_t *req)     { return wf_dl_async(req, h_segment, &s_dl_active, WF_DL_MAX); }

// LK-02/03/04 (1.2.30): старт/стоп/очистка/удаление сегмента ждут FSLOCK и окна flash секунды —
// тоже в отдельной задаче, со своим счётчиком (долгая выдача не блокирует Стоп).
#define WF_CTL_MAX        1
static volatile int s_ctl_active;
static esp_err_t h_start_async(httpd_req_t *req)   { return wf_dl_async(req, h_start, &s_ctl_active, WF_CTL_MAX); }
static esp_err_t h_stop_async(httpd_req_t *req)    { return wf_dl_async(req, h_stop, &s_ctl_active, WF_CTL_MAX); }
// Очистка не идёт поверх выдачи окна/экспорта/сегмента (раньше их сериализовал один поток httpd): иначе unlink открытого файла → «delete»
static esp_err_t h_clear_async(httpd_req_t *req)
{
    if (s_dl_active) return wf_dl_busy(req);
    return wf_dl_async(req, h_clear, &s_ctl_active, WF_CTL_MAX);
}
static esp_err_t h_segdel_async(httpd_req_t *req)  { return wf_dl_async(req, h_segment_delete, &s_ctl_active, WF_CTL_MAX); }

void web_waterfall_register(httpd_handle_t server)
{
    s_server = server;
    if (!s_ws_mutex) s_ws_mutex = xSemaphoreCreateMutex();
    for (int i = 0; i < WF_WS_MAX; i++) { s_ws_fds[i] = 0; s_ws_inflight[i] = 0; }
    s_wf_wrap_count = 0;   // F1: защита от повторного вызова этой функции

    reg(server, "/waterfall",            HTTP_GET,  h_page);
    reg(server, "/api/waterfall/status", HTTP_GET,  h_status);
    reg(server, "/api/waterfall/start",  HTTP_POST, h_start_async);
    reg(server, "/api/waterfall/stop",   HTTP_POST, h_stop_async);
    reg(server, "/api/waterfall/clear",  HTTP_POST, h_clear_async);
    reg(server, "/api/waterfall/config", HTTP_POST, h_config);
    reg(server, "/api/waterfall/window", HTTP_GET,  h_window_async);
    reg(server, "/api/waterfall/export.aswf", HTTP_GET, h_export_aswf_async);
    reg(server, "/api/waterfall/export.n42",  HTTP_GET, h_export_n42_async);
    // #REC-11-A1: листинг и отдача сегментов (СТРОГО read-only).
    reg(server, "/api/waterfall/segments", HTTP_GET, h_segments);
    reg(server, "/api/waterfall/segment",  HTTP_GET, h_segment_async);
    // #REC-11 pull: удаление сегмента по ack от PC-клиента (CSRF, только завершённый).
    reg(server, "/api/waterfall/segment/delete", HTTP_POST, h_segdel_async);
    // #REC-11-A2: конфиг/статус автономной выгрузки сегментов.
    reg(server, "/api/waterfall/offload",  HTTP_GET,  h_offload_get);
    reg(server, "/api/waterfall/offload",  HTTP_POST, h_offload_set);
    // v3: дозовый коэффициент и кривая
    reg(server, "/api/waterfall/dose_k",            HTTP_GET,  h_dose_k_get);
    reg(server, "/api/waterfall/dose_k",            HTTP_POST, h_dose_k_set);
    reg(server, "/api/waterfall/dose_curve",        HTTP_GET,  h_dose_curve_get);
    reg(server, "/api/waterfall/dose_curve",        HTTP_POST, h_dose_curve_set);
    reg(server, "/api/waterfall/dose_curve/clear",  HTTP_POST, h_dose_curve_clear);

    httpd_uri_t ws = {
        .uri = "/ws/waterfall", .method = HTTP_GET,
        .handler = h_ws, .user_ctx = NULL, .is_websocket = true,
    };
    // issue #52b (sweep-B задача 7): та же громкая проверка, что в reg().
    esp_err_t ws_rerr = httpd_register_uri_handler(server, &ws);
    if (ws_rerr != ESP_OK)
        ESP_LOGE(TAG, "issue#52b: register '/ws/waterfall' failed: %s", esp_err_to_name(ws_rerr));

    spectrogram_set_row_cb(wf_broadcast);
    ESP_LOGI(TAG, "waterfall endpoints registered");
}
