#include "spectrum_http_cache.h"
#include "atomspectra.h"
#include "calib_autoread.h"       // #AWF-12b F2: calib_is_missing — единый признак "задана"
#include "json_uint_fmt.h"        // UI-P1: быстрый forматтер bins[] (не vsnprintf)
#include "spectrum_t1.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "spec_cache";

static SemaphoreHandle_t s_mtx;
static spectrum_data_t   *s_sp;          // PSRAM
static char              *s_json;        // PSRAM full JSON incl. bins
static size_t             s_json_len;
static char              *s_meta;        // DRAM/PSRAM small meta JSON
static size_t             s_meta_len;
static int64_t            s_built_us;
static uint32_t           s_render_count;
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

static float compute_live_time(const spectrum_data_t *sp)
{
    const device_info_t *di = spectrum_get_device_info();
    float total = (float)sp->total_time_sec;
    if (!di->valid || di->freq <= 0.0f) return total;
    float tau = ((float)di->rise + (float)di->fall + 1.0f) / di->freq;
    float dead = (float)(sp->total_counts + sp->lost_impulses) * tau;
    if (dead < 0.0f) dead = 0.0f;
    if (dead > total) dead = total;
    return total - dead;
}

static bool append_fmt(char **buf, size_t *len, size_t *cap, const char *fmt, ...)
{
    va_list ap;
    for (;;) {
        size_t avail = (*cap > *len) ? (*cap - *len) : 0;
        va_start(ap, fmt);
        int n = vsnprintf(*buf + *len, avail, fmt, ap);
        va_end(ap);
        if (n < 0) return false;
        if ((size_t)n < avail) {
            *len += (size_t)n;
            return true;
        }
        size_t need = *len + (size_t)n + 1;
        size_t ncap = *cap ? *cap * 2 : 65536;
        while (ncap < need) ncap *= 2;
        char *nb = heap_caps_realloc(*buf, ncap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!nb) nb = realloc(*buf, ncap);
        if (!nb) return false;
        *buf = nb;
        *cap = ncap;
    }
}

// UI-P1 (ui-perf-2026-09-27.md P1): та же семантика роста буфера, что и
// append_fmt (доубливание при нехватке места), но ЗАПИСЬ элемента —
// json_append_uint32_csv (main/json_uint_fmt.h, host-тест
// tests/host/test_json_uint_fmt.c: побайтовое совпадение со старым
// vsnprintf-путём) вместо vsnprintf на КАЖДЫЙ из 8192 бинов.
static bool append_uint32_csv_grow(char **buf, size_t *len, size_t *cap,
                                    uint32_t v, int need_comma)
{
    for (;;) {
        size_t w = json_append_uint32_csv(*buf, *cap, *len, v, need_comma);
        if (w > 0) { *len += w; return true; }
        size_t need = *len + 12;   // max 1 запись ("," + 10 цифр)
        size_t ncap = *cap ? *cap * 2 : 65536;
        while (ncap < need) ncap *= 2;
        char *nb = heap_caps_realloc(*buf, ncap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!nb) nb = realloc(*buf, ncap);
        if (!nb) return false;
        *buf = nb; *cap = ncap;
    }
}

static bool build_json_full(const spectrum_data_t *sp, char **out, size_t *out_len)
{
    char *buf = NULL;
    size_t len = 0, cap = 0;
    if (!append_fmt(&buf, &len, &cap, "{\"bins\":[")) goto fail;
    for (int i = 0; i < SPECTRUM_CHANNELS; i++) {
        if (!append_uint32_csv_grow(&buf, &len, &cap, sp->bins[i], i != 0)) goto fail;
    }
    uint32_t hist_ok = 0, hist_drop = 0;
    spectrum_get_hist_stats(&hist_ok, &hist_drop);
    int dead = usb_host_cdc_spectrometer_dead() ? 1 : 0;
    int as_age = spectrum_autosave_age_sec();
    int as_streak = spectrum_autosave_fail_streak();
    char jt1[16], jt2[16], jt3[16];
    spectrum_temp_json(jt1, sizeof jt1, sp->temperature[0]);
    spectrum_temp_json(jt2, sizeof jt2, sp->temperature[1]);
    spectrum_temp_json(jt3, sizeof jt3, sp->temperature[2]);
    if (!append_fmt(&buf, &len, &cap,
        "],\"total\":%" PRIu32 ",\"cpu\":%u,\"cps\":%" PRIu32 ",\"lost\":%" PRIu32
        ",\"time\":%" PRIu32 ",\"live\":%.1f,"
        "\"bridge_drop\":%" PRIu32 ",\"usb_rx_err\":%" PRIu32 ",\"rx_ring_drops\":%" PRIu32 ","
        "\"hist_ok\":%" PRIu32 ",\"hist_drop\":%" PRIu32 ","
        "\"autosave_age_sec\":%d,\"autosave_fail_streak\":%d,"
        "\"t1\":%s,\"t2\":%s,\"t3\":%s,\"serial\":\"%s\",\"dead\":%d",
        sp->total_counts, (unsigned)sp->cpu_load, sp->cps, sp->lost_impulses,
        sp->total_time_sec, compute_live_time(sp),
        tcp_bridge_dropped_bytes(), usb_host_cdc_rx_errors(), usb_host_cdc_rx_ring_drops(),
        hist_ok, hist_drop, as_age, as_streak,
        jt1, jt2, jt3,
        sp->serial_number[0] ? sp->serial_number : "", dead)) goto fail;
    if (sp->calib_valid) {
        if (!append_fmt(&buf, &len, &cap, ",\"calib\":[")) goto fail;
        for (int i = 0; i <= sp->calib_order; i++) {
            if (!append_fmt(&buf, &len, &cap, "%s%.15g", i ? "," : "", sp->calibration[i])) goto fail;
        }
        if (!append_fmt(&buf, &len, &cap, "]")) goto fail;
    }
    // #AWF-12b F2 (release-gate-1.2.28-code.md): тот же признак, что
    // usb_host_cdc.c-гейт и /api/device (web_server.c) — index.html читает
    // именно этот эндпоинт (/api/spectrum/meta.json) на каждом кадре и
    // включает кнопку идентификации нуклидов по нему (nucBtnEnable).
    if (!append_fmt(&buf, &len, &cap, ",\"calib_set\":%s",
        calib_is_missing(sp->calibration, CALIB_COEFFS, sp->calib_valid) ? "false" : "true")) goto fail;
    if (!append_fmt(&buf, &len, &cap, "}")) goto fail;
    *out = buf;
    *out_len = len;
    return true;
fail:
    free(buf);
    return false;
}

static bool build_json_meta(const spectrum_data_t *sp, char **out, size_t *out_len)
{
    char *buf = NULL;
    size_t len = 0, cap = 0;
    uint32_t hist_ok = 0, hist_drop = 0;
    spectrum_get_hist_stats(&hist_ok, &hist_drop);
    int dead = usb_host_cdc_spectrometer_dead() ? 1 : 0;
    int as_age = spectrum_autosave_age_sec();
    int as_streak = spectrum_autosave_fail_streak();
    char jt1[16], jt2[16], jt3[16];
    spectrum_temp_json(jt1, sizeof jt1, sp->temperature[0]);
    spectrum_temp_json(jt2, sizeof jt2, sp->temperature[1]);
    spectrum_temp_json(jt3, sizeof jt3, sp->temperature[2]);
    if (!append_fmt(&buf, &len, &cap,
        "{\"total\":%" PRIu32 ",\"cpu\":%u,\"cps\":%" PRIu32 ",\"lost\":%" PRIu32
        ",\"time\":%" PRIu32 ",\"live\":%.1f,"
        "\"bridge_drop\":%" PRIu32 ",\"usb_rx_err\":%" PRIu32 ",\"rx_ring_drops\":%" PRIu32 ","
        "\"hist_ok\":%" PRIu32 ",\"hist_drop\":%" PRIu32 ","
        "\"autosave_age_sec\":%d,\"autosave_fail_streak\":%d,"
        "\"t1\":%s,\"t2\":%s,\"t3\":%s,\"serial\":\"%s\",\"dead\":%d,\"channels\":%d",
        sp->total_counts, (unsigned)sp->cpu_load, sp->cps, sp->lost_impulses,
        sp->total_time_sec, compute_live_time(sp),
        tcp_bridge_dropped_bytes(), usb_host_cdc_rx_errors(), usb_host_cdc_rx_ring_drops(),
        hist_ok, hist_drop, as_age, as_streak,
        jt1, jt2, jt3,
        sp->serial_number[0] ? sp->serial_number : "", dead, SPECTRUM_CHANNELS)) goto fail;
    if (sp->calib_valid) {
        if (!append_fmt(&buf, &len, &cap, ",\"calib\":[")) goto fail;
        for (int i = 0; i <= sp->calib_order; i++) {
            if (!append_fmt(&buf, &len, &cap, "%s%.15g", i ? "," : "", sp->calibration[i])) goto fail;
        }
        if (!append_fmt(&buf, &len, &cap, "]")) goto fail;
    }
    // #AWF-12b F2: тот же признак, что build_json_full выше и /api/device.
    if (!append_fmt(&buf, &len, &cap, ",\"calib_set\":%s",
        calib_is_missing(sp->calibration, CALIB_COEFFS, sp->calib_valid) ? "false" : "true")) goto fail;
    if (!append_fmt(&buf, &len, &cap, "}")) goto fail;
    *out = buf;
    *out_len = len;
    return true;
fail:
    free(buf);
    return false;
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
static bool spectrum_http_cache_finish_build(int64_t now)
{
    bool snap_ok = s_sp_build && spectrum_get_snapshot(s_sp_build);
    char *nj = NULL, *nm = NULL;
    size_t njl = 0, nml = 0;
    bool build_ok = snap_ok && build_json_full(s_sp_build, &nj, &njl) &&
                     build_json_meta(s_sp_build, &nm, &nml);
    if (!build_ok) { free(nj); nj = NULL; free(nm); nm = NULL; }

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
    if (s_readers > 0 && s_have && s_json && s_meta) {
        // Читатели ещё держат ТЕКУЩИЙ s_sp/s_json/s_meta (PAYLOAD_BINS шлёт
        // s_sp->bins напрямую) — свежесобранное в этот раз не применяем.
        xSemaphoreGive(s_mtx);
        free(nj);
        free(nm);
        return true;
    }
    free(s_json);
    free(s_meta);
    // Pointer-swap s_sp<->s_sp_build (без 32КБ memcpy под мьютексом).
    spectrum_data_t *tmp = s_sp;
    s_sp = s_sp_build;
    s_sp_build = tmp;
    s_json = nj;
    s_json_len = njl;
    s_meta = nm;
    s_meta_len = nml;
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
    if (s_have && (now - s_built_us) < SPECTRUM_HTTP_CACHE_TTL_US && s_json && s_meta) {
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
    xSemaphoreGive(s_mtx);
    return spectrum_http_cache_finish_build(now);
}

// #AWF-12b R5: s_have=false под s_mtx — следующий ensure() увидит "нет
// кэша" и пересоберёт, не дожидаясь TTL. До init() (s_mtx ещё NULL) —
// нечего инвалидировать, ensure() при первом вызове соберёт сам.
void spectrum_http_cache_invalidate(void)
{
    if (!s_mtx) return;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_have = false;
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

typedef enum { PAYLOAD_JSON, PAYLOAD_META, PAYLOAD_BINS } payload_kind_t;

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
    case PAYLOAD_JSON: *payload = s_json; *len = s_json_len; break;
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
    const char *payload = NULL;
    size_t len = 0;
    char hdr[16];   // живёт до конца функции — дольше, чем httpd_resp_send()
    snapshot_begin(req, PAYLOAD_JSON, &payload, &len, hdr, sizeof(hdr));
    httpd_resp_set_type(req, "application/json");
    esp_err_t e = httpd_resp_send(req, payload, len);
    snapshot_end();
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
