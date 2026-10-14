#pragma once
// 1.2.36 H2/H3 (B): поля meta один раз; полный JSON собирается лениво из той же строки meta. Host-pure.
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "atomspectra.h"        // spectrum_data_t, SPECTRUM_CHANNELS, CALIB_COEFFS
#include "calib_autoread.h"     // calib_is_missing()
#include "json_uint_fmt.h"      // json_append_uint32_csv()
#include "spectrum_t1.h"        // spectrum_temp_json()

typedef struct {            /* живые счётчики, прочитанные ОДИН раз за пересборку */
    uint32_t hist_ok, hist_drop, bridge_drop, usb_rx_err, rx_ring_drops, reset_unconfirmed;
    int      dead, as_age, as_streak;
    float    live;          /* spectrum_compute_live_time(sp) */
} spec_meta_live_t;

typedef struct {            /* ВСЕ значения, которые идут в meta */
    uint32_t total, cps, lost, time;
    unsigned cpu;
    float    live;
    uint32_t bridge_drop, usb_rx_err, rx_ring_drops, hist_ok, hist_drop;
    int      as_age, as_streak, dead;
    char     t1[16], t2[16], t3[16];
    char     serial[64];
    bool     calib_valid;
    int      calib_order;
    double   calibration[CALIB_COEFFS];
    bool     calib_set;
    uint32_t reset_unconfirmed;
} spec_meta_in_t;

static inline void spec_meta_in_fill(spec_meta_in_t *in, const spectrum_data_t *sp, const spec_meta_live_t *lv)
{
    memset(in, 0, sizeof *in);
    in->total = sp->total_counts;
    in->cps = sp->cps;
    in->lost = sp->lost_impulses;
    in->time = sp->total_time_sec;
    in->cpu = (unsigned)sp->cpu_load;
    in->live = lv->live;
    in->hist_ok = lv->hist_ok;
    in->hist_drop = lv->hist_drop;
    in->bridge_drop = lv->bridge_drop;
    in->usb_rx_err = lv->usb_rx_err;
    in->rx_ring_drops = lv->rx_ring_drops;
    in->as_age = lv->as_age;
    in->as_streak = lv->as_streak;
    in->dead = lv->dead;
    in->reset_unconfirmed = lv->reset_unconfirmed;

    spectrum_temp_json(in->t1, sizeof in->t1, sp->temperature[0]);
    spectrum_temp_json(in->t2, sizeof in->t2, sp->temperature[1]);
    spectrum_temp_json(in->t3, sizeof in->t3, sp->temperature[2]);

    size_t sn = 0;   // до первого нуля, не больше 63 символов (in->serial уже обнулён memset-ом)
    while (sn < sizeof in->serial - 1 && sp->serial_number[sn]) sn++;
    memcpy(in->serial, sp->serial_number, sn);

    in->calib_valid = sp->calib_valid;
    in->calib_order = sp->calib_order;
    memcpy(in->calibration, sp->calibration, sizeof in->calibration);
    in->calib_set = !calib_is_missing(sp->calibration, CALIB_COEFFS, sp->calib_valid);
}

typedef struct { char *buf; size_t cap; size_t len; } spec_w_t;

static inline void spec_w_fmt(spec_w_t *w, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static inline void spec_w_fmt(spec_w_t *w, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    size_t avail = (w->len < w->cap) ? (w->cap - w->len) : 0;
    int n;
    if (avail > 0) {
        n = vsnprintf(w->buf + w->len, avail, fmt, ap);
    } else {
        n = vsnprintf(NULL, 0, fmt, ap);
    }
    va_end(ap);
    if (n >= 0) {
        w->len += (size_t)n;
    }
}

static inline void spec_w_bytes(spec_w_t *w, const char *s, size_t n)
{
    size_t avail = (w->len < w->cap) ? (w->cap - w->len) : 0;
    if (avail > 0 && n > 0) {
        size_t copy = (n < avail) ? n : avail;
        memcpy(w->buf + w->len, s, copy);
    }
    w->len += n;
}

static inline size_t spec_meta_fmt(char *buf, size_t cap, const spec_meta_in_t *in, size_t *a_end, size_t *b_start)
{
    spec_w_t w = { buf, cap, 0 };

    spec_w_fmt(&w,
        "{\"total\":%" PRIu32 ",\"cpu\":%u,\"cps\":%" PRIu32 ",\"lost\":%" PRIu32
        ",\"time\":%" PRIu32 ",\"live\":%.1f,"
        "\"bridge_drop\":%" PRIu32 ",\"usb_rx_err\":%" PRIu32 ",\"rx_ring_drops\":%" PRIu32 ","
        "\"hist_ok\":%" PRIu32 ",\"hist_drop\":%" PRIu32 ","
        "\"autosave_age_sec\":%d,\"autosave_fail_streak\":%d,"
        "\"t1\":%s,\"t2\":%s,\"t3\":%s,\"serial\":\"%s\",\"dead\":%d",
        in->total, in->cpu, in->cps, in->lost,
        in->time, (double)in->live,
        in->bridge_drop, in->usb_rx_err, in->rx_ring_drops,
        in->hist_ok, in->hist_drop,
        in->as_age, in->as_streak,
        in->t1, in->t2, in->t3, in->serial, in->dead);

    if (a_end) *a_end = w.len;

    spec_w_fmt(&w, ",\"channels\":%d", SPECTRUM_CHANNELS);

    if (b_start) *b_start = w.len;

    if (in->calib_valid) {
        spec_w_fmt(&w, ",\"calib\":[");
        for (int i = 0; i <= in->calib_order && i < CALIB_COEFFS; i++) {
            spec_w_fmt(&w, "%s%.15g", i ? "," : "", in->calibration[i]);
        }
        spec_w_fmt(&w, "]");
    }

    spec_w_fmt(&w, ",\"calib_set\":%s", in->calib_set ? "true" : "false");
    spec_w_fmt(&w, ",\"reset_unconfirmed\":%" PRIu32, in->reset_unconfirmed);
    spec_w_fmt(&w, "}");

    if (cap > 0) {
        size_t term = (w.len < cap) ? w.len : cap - 1;
        buf[term] = '\0';
    }
    return w.len;
}

static inline size_t spec_json_full_fmt(char *buf, size_t cap, const uint32_t *bins, const char *meta, size_t meta_len, size_t a_end, size_t b_start)
{
    if (a_end < 1 || b_start < a_end || b_start > meta_len) {
        if (cap > 0) buf[0] = '\0';
        return 0;
    }

    spec_w_t w = { buf, cap, 0 };

    spec_w_bytes(&w, "{\"bins\":[", 9);

    for (int i = 0; i < SPECTRUM_CHANNELS; i++) {
        char tmp[12];
        size_t n = json_append_uint32_csv(tmp, sizeof tmp, 0, bins[i], i != 0);
        spec_w_bytes(&w, tmp, n);
    }

    spec_w_bytes(&w, "],", 2);
    spec_w_bytes(&w, meta + 1, a_end - 1);
    spec_w_bytes(&w, meta + b_start, meta_len - b_start);

    if (cap > 0) {
        size_t term = (w.len < cap) ? w.len : cap - 1;
        buf[term] = '\0';
    }
    return w.len;
}

/* 1.2.36 #MX-14 (G): хвост meta.json?w=W - SMA окна «отношение сумм» (Σcounts/Σdur, monitor_window.h). Заменяет закрывающую "}"
   тела meta: body[0 .. len-1) + хвост. sd==0 (кольцо пусто) - "sma":null. Возвращает длину хвоста (>= cap - не поместилось). */
static inline size_t spec_meta_sma_tail(char *buf, size_t cap, uint64_t sc, uint32_t sd, uint32_t n)
{
    int k;
    if (sd == 0) k = snprintf(buf, cap, ",\"sma\":null,\"sma_w\":%" PRIu32 ",\"sma_n\":%" PRIu32 "}", sd, n);
    else k = snprintf(buf, cap, ",\"sma\":%.4f,\"sma_w\":%" PRIu32 ",\"sma_n\":%" PRIu32 "}", (double)sc / (double)sd, sd, n);
    return (k < 0) ? 0 : (size_t)k;
}
