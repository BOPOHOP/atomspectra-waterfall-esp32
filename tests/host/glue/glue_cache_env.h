#pragma once
/* Host glue env (release-gate 1.2.36 pass1 С-2): spectrum.c/usb/tcp externs of spectrum_http_cache.c, answers = live stand capture 00000081 (fixtures/stand_capture.h). */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "fixtures/stand_capture.h"
#include "acq_watch.h"

spectrum_data_t g_spec;
bool g_spec_ok = true;

/* D2 (K2): эпоха, снятая вместе со снимком, и boot_id. g_live_ep - "живые" счётчики прошивки: заглушка снимка копирует их в пару ep
   в момент сборки кэша (как spectrum_get_snapshot_epoch_seq в одном окне seqlock); тест меняет g_live_ep между сборками. */
spec_epoch_t g_live_ep;
uint32_t g_boot_id = 0xB007B007u;

bool spectrum_get_snapshot_epoch_seq(spectrum_data_t *out, spec_epoch_t *ep) {
    if (!g_spec_ok) return false;
    memcpy(out, &g_spec, sizeof *out);
    *ep = g_live_ep;
    return out->valid;
}

uint32_t spectrum_boot_id(void) {
    return g_boot_id;
}

/* ЖИВЫЕ геттеры прошивки (каждый со своим замком, неатомарно со снимком) - в склейке кэша их вызывать НЕЛЬЗЯ; заглушки нужны мутанту KE6 */
void spectrum_get_base_info(uint32_t *base_time, uint32_t *base_counts, uint32_t *dev_resets) {
    if (base_time) *base_time = 0;
    if (base_counts) *base_counts = 0;
    if (dev_resets) *dev_resets = g_live_ep.dev_resets;
}

uint32_t spectrum_reset_gen(void) {
    return g_live_ep.spec_resets;
}

/* D2: живые счётчики meta (по умолчанию - стенд 00000081); замороженный тест v1_bytes ставит значения из снимка .146 */
uint32_t g_live_hist_ok = STAND_HIST_OK, g_live_hist_drop = STAND_HIST_DROP, g_live_bridge_drop = STAND_BRIDGE_DROP;
uint32_t g_live_usb_rx_err = STAND_USB_RX_ERR, g_live_rx_ring_drops = STAND_RX_RING_DROPS, g_live_reset_unconfirmed = STAND_RESET_UNCONFIRMED;
int g_live_dead = STAND_DEAD, g_live_as_age = (int)STAND_AUTOSAVE_AGE_SEC, g_live_as_streak = (int)STAND_AUTOSAVE_FAIL_STREAK;
float g_live_live = (float)STAND_LIVE;

void spectrum_get_hist_stats(uint32_t *commits, uint32_t *drops) {
    *commits = g_live_hist_ok;
    *drops = g_live_hist_drop;
}

bool usb_host_cdc_spectrometer_dead(void) {
    return g_live_dead != 0;
}

/* 1.2.37 X-Hist-Age-Ms: the stamp of the last histogram is set by the test (0 - none since boot); "now" is the glue clock (glue_now_us),
   the age comes from the SAME pure acq_hist_age_ms() that usb_host_cdc_hist_age_ms() of the firmware uses */
uint32_t glue_hist_ts_ms = 0;
int32_t usb_host_cdc_hist_age_ms(void) {
    return acq_hist_age_ms((uint32_t)(glue_now_us / 1000), glue_hist_ts_ms);
}

int spectrum_autosave_age_sec(void) {
    return g_live_as_age;
}

int spectrum_autosave_fail_streak(void) {
    return g_live_as_streak;
}

uint32_t tcp_bridge_dropped_bytes(void) {
    return g_live_bridge_drop;
}

uint32_t usb_host_cdc_rx_errors(void) {
    return g_live_usb_rx_err;
}

uint32_t usb_host_cdc_rx_ring_drops(void) {
    return g_live_rx_ring_drops;
}

uint32_t spectrum_reset_unconfirmed_count(void) {
    return g_live_reset_unconfirmed;
}

float spectrum_compute_live_time(const spectrum_data_t *sp) {
    (void)sp;
    return g_live_live;
}

static void __attribute__((unused)) glue_fill_stand(spectrum_data_t *s, uint32_t scale) {
    memset(s, 0, sizeof *s);
    for (int i = 0; i < SPECTRUM_CHANNELS; i++) s->bins[i] = STAND_BINS[i] * scale;
    memcpy(s->calibration, STAND_CALIB, sizeof s->calibration);
    s->calib_order = 4;
    s->calib_valid = true;
    strncpy(s->serial_number, STAND_SERIAL, sizeof s->serial_number - 1);
    s->total_counts = STAND_TOTAL;
    s->total_time_sec = STAND_TIME;
    s->cpu_load = (uint16_t)STAND_CPU;
    s->cps = STAND_CPS;
    s->lost_impulses = STAND_LOST;
    s->temperature[0] = (float)STAND_T1;
    s->temperature[1] = NAN;
    s->temperature[2] = NAN;
    s->valid = true;
}

static uint64_t __attribute__((unused)) glue_bins_sum(const spectrum_data_t *s) {
    uint64_t sum = 0;
    for (int i = 0; i < SPECTRUM_CHANNELS; i++) sum += s->bins[i];
    return sum;
}

__attribute__((unused)) static char *glue_expected_json(const char *m, size_t ml, const uint32_t *bins, size_t *out_len) {
    const char *needle = ",\"channels\":";
    size_t needle_len = strlen(needle);
    size_t a = (size_t)-1;
    for (size_t i = 0; i + needle_len <= ml; i++) {
        if (memcmp(m + i, needle, needle_len) == 0) {
            a = i;
            break;
        }
    }
    if (a == (size_t)-1) return NULL;

    size_t b = (size_t)-1;
    for (size_t i = a + 1; i < ml; i++) {
        if (m[i] == ',' || m[i] == '}') {
            b = i;
            break;
        }
    }
    if (b == (size_t)-1) return NULL;

    size_t cap = 131072;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;

    size_t pos = 0;
    pos += (size_t)snprintf(buf + pos, cap - pos, "{\"bins\":[");
    for (int i = 0; i < SPECTRUM_CHANNELS; i++) {
        if (i > 0) {
            buf[pos++] = ',';
        }
        char tmp[16];
        int n = snprintf(tmp, sizeof tmp, "%u", bins[i]);
        if (n < 0) { free(buf); return NULL; }
        size_t len = (size_t)n;
        if (pos + len >= cap) {
            size_t new_cap = cap * 2;
            char *nb = (char *)realloc(buf, new_cap);
            if (!nb) { free(buf); return NULL; }
            buf = nb;
            cap = new_cap;
        }
        memcpy(buf + pos, tmp, len);
        pos += len;
    }
    pos += (size_t)snprintf(buf + pos, cap - pos, "],");

    size_t mid_len = a - 1;   /* m[1 .. a): meta without its '{' up to ,"channels" */
    if (pos + mid_len + 1 >= cap) {
        size_t new_cap = cap * 2;
        while (pos + mid_len + 1 >= new_cap) new_cap *= 2;
        char *nb = (char *)realloc(buf, new_cap);
        if (!nb) { free(buf); return NULL; }
        buf = nb;
        cap = new_cap;
    }
    memcpy(buf + pos, m + 1, mid_len);
    pos += mid_len;

    size_t tail_len = ml - b;
    if (pos + tail_len + 1 >= cap) {
        size_t new_cap = cap * 2;
        while (pos + tail_len + 1 >= new_cap) new_cap *= 2;
        char *nb = (char *)realloc(buf, new_cap);
        if (!nb) { free(buf); return NULL; }
        buf = nb;
        cap = new_cap;
    }
    memcpy(buf + pos, m + b, tail_len);
    pos += tail_len;

    buf[pos] = '\0';
    if (out_len) *out_len = pos;
    return buf;
}
