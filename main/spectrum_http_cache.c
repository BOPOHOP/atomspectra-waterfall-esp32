#include "spectrum_http_cache.h"
#include "atomspectra.h"
#include "calib_autoread.h"       // #AWF-12b F2: calib_is_missing — единый признак "задана"
#include "json_uint_fmt.h"        // UI-P1: быстрый forматтер bins[] (не vsnprintf)
#include "spectrum_t1.h"
#include "spec_cache_gen.h"       // М5: чистое решение "отбросить устаревший снимок" (host-pure)
#include "spectrum_meta_fmt.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "spec_cache";

static SemaphoreHandle_t s_mtx;
static spectrum_data_t   *s_sp;          // PSRAM
static char              *s_json;        // PSRAM full JSON incl. bins
static size_t             s_json_len;
static size_t             s_json_cap;
static uint32_t           s_json_render;
static bool               s_json_have;
static int                s_json_users;
static volatile uint32_t  s_json_builds;

static char              *s_meta;        // DRAM/PSRAM small meta JSON
static size_t             s_meta_len;
static size_t             s_meta_cap;
static size_t             s_meta_a_end;
static size_t             s_meta_b_start;

static char              *s_meta_build;
static size_t             s_meta_build_cap;

static int64_t            s_built_us;
static uint32_t           s_render_count;
static volatile uint32_t  s_build_us_last;   // 1.2.36 Commit 0: наблюдаемость (/api/system)
static volatile uint32_t  s_build_us_max;
static bool               s_have;
// Сколько отправок прямо сейчас читают s_json / s_meta / s_sp. Пока счётчик не
// ноль, пересборка кэша не освобождает буферы — иначе httpd_resp_send() уедет
// по освобождённому указателю.
static int                s_readers;
// UI-P1: сборка JSON идёт ВНЕ s_mtx (в scratch-снимок s_sp_build), под
// мьютексом только swap указателей. s_building — single-flight (не пускать
// вторую параллельную сборку с того же tick).
static spectrum_data_t   *s_sp_build;    // PSRAM, scratch для снимка при пересборке
static bool               s_building;
// М5 (release-gate-firmware-v1.2.28-code.md): счётчик поколений — снимок в
// finish_build() снимается ДО захвата s_mtx (:211), invalidate() может успеть
// вклиниться между снимком и commit'ом и пометить его устаревшим уже ПОСЛЕ
// того, как снимок сделан со старой калибровкой. gen меняется только в
// invalidate(); commit сверяет gen "до" и "после" сборки и не применяет
// результат, если он изменился, — иначе invalidate() молча перекрывался
// более свежим (на самом деле устаревшим) commit'ом.
static volatile uint32_t  s_gen;

// Рост буфера до need байт (степень двойки от start): сначала PSRAM, затем обычная куча. false при отказе (буфер не тронут).
static bool buf_grow(char **buf, size_t *cap, size_t need, size_t start)
{
    if (*cap >= need) return true;
    size_t ncap = *cap ? *cap : start;
    while (ncap < need) ncap *= 2;
    char *nb = heap_caps_realloc(*buf, ncap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!nb) nb = realloc(*buf, ncap);
    if (!nb) return false;
    *buf = nb;
    *cap = ncap;
    return true;
}

// Поля meta ОДИН раз (счётчики читаются один раз за пересборку): собрать spec_meta_in_t и отформатировать в *buf (рост при нехватке).
static bool build_meta(const spectrum_data_t *sp, char **buf, size_t *cap, size_t *len, size_t *a_end, size_t *b_start)
{
    spec_meta_live_t lv;
    memset(&lv, 0, sizeof(lv));
    spectrum_get_hist_stats(&lv.hist_ok, &lv.hist_drop);
    lv.dead = usb_host_cdc_spectrometer_dead() ? 1 : 0;
    lv.as_age = spectrum_autosave_age_sec();
    lv.as_streak = spectrum_autosave_fail_streak();
    lv.bridge_drop = tcp_bridge_dropped_bytes();
    lv.usb_rx_err = usb_host_cdc_rx_errors();
    lv.rx_ring_drops = usb_host_cdc_rx_ring_drops();
    lv.reset_unconfirmed = spectrum_reset_unconfirmed_count();
    lv.live = spectrum_compute_live_time(sp);

    spec_meta_in_t in;
    spec_meta_in_fill(&in, sp, &lv);

    if (*buf == NULL) {
        if (!buf_grow(buf, cap, 1, 1024)) return false;
    }
    size_t n = spec_meta_fmt(*buf, *cap, &in, a_end, b_start);
    if (n >= *cap) {
        if (!buf_grow(buf, cap, n + 1, 1024)) return false;
        n = spec_meta_fmt(*buf, *cap, &in, a_end, b_start);
    }
    *len = n;
    return true;
}

void spectrum_http_cache_init(void)
{
    if (!s_mtx) {
        s_mtx = xSemaphoreCreateMutex();
        s_sp = heap_caps_malloc(sizeof(*s_sp), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_sp) s_sp = malloc(sizeof(*s_sp));
        // UI-P1: второй буфер того же размера — сборка снимка ВНЕ мьютекса
        // пишет сюда, не трогая s_sp (который читатели могут держать через
        // PAYLOAD_BINS до s_readers==0).
        s_sp_build = heap_caps_malloc(sizeof(*s_sp_build), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_sp_build) s_sp_build = malloc(sizeof(*s_sp_build));
        ESP_LOGI(TAG, "spectrum HTTP cache init (TTL=2s, double-buffered build)");
    }
}

// UI-P1: снимок+сборка JSON ВНЕ s_mtx, применение результата — под мьютексом,
// вызывается уже с s_building=true и мьютексом ОТПУЩЕННЫМ.
static bool spectrum_http_cache_finish_build(int64_t now, uint32_t gen_before)
{
    int64_t t_build0 = esp_timer_get_time();   // 1.2.36 Commit 0: цена снимка+сборки (без ожидания s_mtx)
    bool snap_ok = s_sp_build && spectrum_get_snapshot_seq(s_sp_build);   // 1.2.36 H1: без SPEC_LOCK (seqlock)
    size_t nml = 0, na = 0, nb = 0;
    bool build_ok = snap_ok && build_meta(s_sp_build, &s_meta_build, &s_meta_build_cap, &nml, &na, &nb);
    {
        int64_t d = esp_timer_get_time() - t_build0;
        s_build_us_last = (uint32_t)d;
        if ((uint32_t)d > s_build_us_max) s_build_us_max = (uint32_t)d;
    }

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_building = false;
    if (!snap_ok) {
        s_have = false;
        xSemaphoreGive(s_mtx);
        return false;
    }
    if (!build_ok) {
        ESP_LOGE(TAG, "JSON build failed");
        bool have = s_have;
        xSemaphoreGive(s_mtx);
        return have;
    }
    if (s_readers > 0 && s_have && s_meta) {
        // Читатели ещё держат ТЕКУЩИЙ s_sp/s_json/s_meta (PAYLOAD_BINS шлёт
        // s_sp->bins напрямую) — свежесобранное в этот раз не применяем.
        xSemaphoreGive(s_mtx);
        return true;
    }
    if (spec_cache_gen_stale(gen_before, s_gen)) {
        // М5: invalidate() вклинился между снимком (:219, ВНЕ s_mtx) и этим
        // commit'ом — снимок мог быть снят СО СТАРОЙ калибровкой. Не
        // затираем s_have=false, выставленный invalidate(), устаревшим
        // "свежим" результатом; следующий ensure() пересоберёт заново.
        xSemaphoreGive(s_mtx);
        return false;
    }
    // Pointer-swap s_sp<->s_sp_build (без 32КБ memcpy под мьютексом).
    spectrum_data_t *tmp = s_sp;
    s_sp = s_sp_build;
    s_sp_build = tmp;

    // Swap meta buffers
    char *tmp_meta = s_meta;
    size_t tmp_meta_cap = s_meta_cap;
    s_meta = s_meta_build;
    s_meta_cap = s_meta_build_cap;
    s_meta_build = tmp_meta;
    s_meta_build_cap = tmp_meta_cap;

    s_meta_len = nml;
    s_meta_a_end = na;
    s_meta_b_start = nb;

    s_built_us = now;
    s_have = true;
    s_render_count++;
    xSemaphoreGive(s_mtx);
    return true;
}

bool spectrum_http_cache_ensure(void)
{
    if (!s_mtx) spectrum_http_cache_init();
    if (!s_sp) return false;

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    int64_t now = esp_timer_get_time();
    if (s_have && (now - s_built_us) < SPECTRUM_HTTP_CACHE_TTL_US && s_meta) {
        xSemaphoreGive(s_mtx);
        return true;
    }
    if (s_building) {
        // Кто-то уже пересобирает (single-flight) — отдать что есть, не ждать.
        bool have = s_have;
        xSemaphoreGive(s_mtx);
        return have;
    }
    s_building = true;
    uint32_t gen_before = s_gen;   // М5: под тем же s_mtx, что и bump в invalidate()
    xSemaphoreGive(s_mtx);
    return spectrum_http_cache_finish_build(now, gen_before);
}

// #AWF-12b R5: s_have=false под s_mtx — следующий ensure() увидит "нет
// кэша" и пересоберёт, не дожидаясь TTL. До init() (s_mtx ещё NULL) —
// нечего инвалидировать, ensure() при первом вызове соберёт сам.
void spectrum_http_cache_invalidate(void)
{
    if (!s_mtx) return;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_have = false;
    s_gen++;   // М5: сборка в полёте (если есть) сверит и не перезапишет s_have
    xSemaphoreGive(s_mtx);
}

const spectrum_data_t *spectrum_http_cache_data(void)
{
    return (s_have && s_sp) ? s_sp : NULL;
}

uint32_t spectrum_http_cache_render_count(void)
{
    return s_render_count;
}

uint32_t spectrum_http_cache_build_us_last(void) { return s_build_us_last; }
uint32_t spectrum_http_cache_build_us_max(void) { return s_build_us_max; }
uint32_t spectrum_http_cache_json_builds(void) { return s_json_builds; }

// esp_http_server хранит указатель, а не копию: значение обязано дожить до
// httpd_resp_send(). Поэтому буфер принадлежит кадру отправителя, а не этой
// функции и не snapshot_begin() — оба возвращаются раньше отправки.
static void set_render_hdr(httpd_req_t *req, uint32_t render_count,
                           char *hdr, size_t hdr_size)
{
    snprintf(hdr, hdr_size, "%" PRIu32, render_count);
    httpd_resp_set_hdr(req, "X-Spectrum-Render-Count", hdr);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
}

typedef enum { PAYLOAD_META, PAYLOAD_BINS } payload_kind_t;

// Зафиксировать снапшот кэша: заголовки и указатели берём под мьютексом, но сама
// отправка идёт уже без него — иначе медленный клиент держит мьютекс всё время
// передачи (до 32 KiB бинов) и блокирует остальные запросы к спектру.
// s_readers не даёт пересборке освободить буферы, пока их отправляют.
static void snapshot_begin(httpd_req_t *req, payload_kind_t kind,
                           const char **payload, size_t *len,
                           char *hdr, size_t hdr_size)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    switch (kind) {
    case PAYLOAD_META: *payload = s_meta; *len = s_meta_len; break;
    case PAYLOAD_BINS:
        *payload = (const char *)s_sp->bins;
        *len = SPECTRUM_CHANNELS * sizeof(uint32_t);
        break;
    }
    set_render_hdr(req, s_render_count, hdr, hdr_size);
    s_readers++;
    xSemaphoreGive(s_mtx);
}

static void snapshot_end(void)
{
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (s_readers > 0) s_readers--;
    xSemaphoreGive(s_mtx);
}

esp_err_t spectrum_http_send_json(httpd_req_t *req)
{
    if (!spectrum_http_cache_ensure()) {
        httpd_resp_set_hdr(req, "X-Spectrometer-Dead",
                           usb_host_cdc_spectrometer_dead() ? "1" : "0");
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "No spectrum data yet");
        return ESP_FAIL;
    }
    // 1.2.36 H3: полный JSON (8192 канала текстом) строится ЛЕНИВО, только если его кто-то запросил, и из ТОЙ ЖЕ строки meta,
    // что отдаёт meta.json этого поколения. Сборка — ВНЕ s_mtx; s_readers держит s_sp/s_meta от смены.
    
    const char *payload = NULL;
    size_t len = 0;
    char hdr[16];   // живёт до конца функции — дольше, чем httpd_resp_send()
    uint32_t render_for_hdr = 0;
    bool claimed = false;
    char *buf = NULL;
    size_t cap = 0;
    uint32_t rc = 0;
    const uint32_t *bins = NULL;
    const char *meta = NULL;
    size_t mlen = 0, a_end = 0, b_start = 0;

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    rc = s_render_count;
    if (!spec_json_need_build(s_json_have, s_json_render, rc)) {
        s_json_users++;
        payload = s_json;
        len = s_json_len;
        render_for_hdr = s_json_render;
        xSemaphoreGive(s_mtx);
        set_render_hdr(req, render_for_hdr, hdr, sizeof(hdr));
        httpd_resp_set_type(req, "application/json");
        esp_err_t e = httpd_resp_send(req, payload, len);
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        if (s_json_users > 0) s_json_users--;
        xSemaphoreGive(s_mtx);
        return e;
    } else {
        s_readers++;
        bins = s_sp->bins;
        meta = s_meta;
        mlen = s_meta_len;
        a_end = s_meta_a_end;
        b_start = s_meta_b_start;

        if (s_json_users == 0) {
            buf = s_json;
            cap = s_json_cap;
            claimed = true;
            s_json_users++;
            s_json_have = false;
        } else {
            buf = NULL;
            cap = 0;
            claimed = false;
        }
        xSemaphoreGive(s_mtx);
    }

    // Build outside mutex
    if (!buf && !buf_grow(&buf, &cap, 65536, 65536)) {
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        s_readers--;
        if (claimed) s_json_users--;
        xSemaphoreGive(s_mtx);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }

    size_t n = spec_json_full_fmt(buf, cap, bins, meta, mlen, a_end, b_start);
    if (n >= cap) {
        if (!buf_grow(&buf, &cap, n + 1, 65536)) {
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            s_readers--;
            if (claimed) s_json_users--;
            xSemaphoreGive(s_mtx);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
            return ESP_FAIL;
        }
        n = spec_json_full_fmt(buf, cap, bins, meta, mlen, a_end, b_start);
    }

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_readers--;
    s_json_builds++;
    if (claimed) {
        s_json = buf;
        s_json_cap = cap;
        if (s_render_count == rc) {
            s_json_len = n;
            s_json_render = rc;
            s_json_have = true;
        }
    }
    payload = buf;
    len = n;
    render_for_hdr = rc;
    xSemaphoreGive(s_mtx);

    set_render_hdr(req, render_for_hdr, hdr, sizeof(hdr));
    httpd_resp_set_type(req, "application/json");
    esp_err_t e = httpd_resp_send(req, payload, len);

    xSemaphoreTake(s_mtx, portMAX_DELAY);
    if (claimed) {
        if (s_json_users > 0) s_json_users--;
    } else {
        // Free private buffer after send
        free(buf);
    }
    xSemaphoreGive(s_mtx);

    return e;
}

esp_err_t spectrum_http_send_bins(httpd_req_t *req)
{
    if (!spectrum_http_cache_ensure()) {
        // Тот же заголовок, что у .json и meta.json: три ручки на одних данных
        // не должны отличаться диагностикой 404.
        httpd_resp_set_hdr(req, "X-Spectrometer-Dead",
                           usb_host_cdc_spectrometer_dead() ? "1" : "0");
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "No spectrum data yet");
        return ESP_FAIL;
    }
    const char *payload = NULL;
    size_t len = 0;
    char hdr[16];   // живёт до конца функции — дольше, чем httpd_resp_send()
    snapshot_begin(req, PAYLOAD_BINS, &payload, &len, hdr, sizeof(hdr));
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline");
    esp_err_t e = httpd_resp_send(req, payload, len);
    snapshot_end();
    return e;
}

esp_err_t spectrum_http_send_meta(httpd_req_t *req)
{
    if (!spectrum_http_cache_ensure()) {
        httpd_resp_set_hdr(req, "X-Spectrometer-Dead",
                           usb_host_cdc_spectrometer_dead() ? "1" : "0");
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "No spectrum data yet");
        return ESP_FAIL;
    }
    const char *payload = NULL;
    size_t len = 0;
    char hdr[16];   // живёт до конца функции — дольше, чем httpd_resp_send()
    snapshot_begin(req, PAYLOAD_META, &payload, &len, hdr, sizeof(hdr));
    httpd_resp_set_type(req, "application/json");
    esp_err_t e = httpd_resp_send(req, payload, len);
    snapshot_end();
    return e;
}
