#pragma once
/* Host glue env (release-gate 1.2.36 pass1 С-2): spectrum.c/usb/tcp externs of spectrum_http_cache.c, answers = live stand capture 00000081 (fixtures/stand_capture.h). */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "fixtures/stand_capture.h"

spectrum_data_t g_spec;
bool g_spec_ok = true;

bool spectrum_get_snapshot_seq(spectrum_data_t *out) {
    if (!g_spec_ok) return false;
    memcpy(out, &g_spec, sizeof *out);
    return out->valid;
}

void spectrum_get_hist_stats(uint32_t *commits, uint32_t *drops) {
    *commits = STAND_HIST_OK;
    *drops = STAND_HIST_DROP;
}

bool usb_host_cdc_spectrometer_dead(void) {
    return STAND_DEAD != 0;
}

int spectrum_autosave_age_sec(void) {
    return (int)STAND_AUTOSAVE_AGE_SEC;
}

int spectrum_autosave_fail_streak(void) {
    return (int)STAND_AUTOSAVE_FAIL_STREAK;
}

uint32_t tcp_bridge_dropped_bytes(void) {
    return STAND_BRIDGE_DROP;
}

uint32_t usb_host_cdc_rx_errors(void) {
    return STAND_USB_RX_ERR;
}

uint32_t usb_host_cdc_rx_ring_drops(void) {
    return STAND_RX_RING_DROPS;
}

uint32_t spectrum_reset_unconfirmed_count(void) {
    return STAND_RESET_UNCONFIRMED;
}

float spectrum_compute_live_time(const spectrum_data_t *sp) {
    (void)sp;
    return (float)STAND_LIVE;
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
