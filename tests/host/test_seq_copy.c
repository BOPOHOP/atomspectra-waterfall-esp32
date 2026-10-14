#include "seq_copy.h"
#include "atomspectra.h"
#include "test_util.h"
#include "fixtures/imp_real_bins.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#define NCHECK(name, cond) do { if (!(cond)) { printf("FAIL %s %s:%d: %s\n", name, __FILE__, __LINE__, #cond); g_failures++; } } while (0)

static volatile uint32_t g_seq;
static spectrum_data_t *g_src;
static int g_copy_calls;
static int g_inject_on;
static int g_wait_calls;

static spectrum_data_t *make_src(void) {
    spectrum_data_t *s = calloc(1, sizeof(spectrum_data_t));
    if (!s) exit(2);
    for (int i = 0; i < SPECTRUM_CHANNELS; i++) s->bins[i] = (uint32_t)IMP_REAL_BINS[i];
    s->total_counts = (uint32_t)IMP_REAL_SUM;
    s->total_time_sec = (uint32_t)IMP_REAL_TIME;
    s->cps = 718;
    s->cpu_load = 12;
    memcpy(s->calibration, IMP_REAL_CALIB, sizeof(s->calibration));
    s->calib_order = 3;
    s->calib_valid = true;
    s->valid = true;
    memcpy(s->serial_number, "AS-4B0C2E91", 11);
    return s;
}

static bool sum_ok(const spectrum_data_t *s) {
    uint32_t sum = 0;
    for (int i = 0; i < SPECTRUM_CHANNELS; i++) sum += s->bins[i];
    return sum == s->total_counts;
}

static void writer_apply(spectrum_data_t *s) {
    for (int i = 0; i < SPECTRUM_CHANNELS; i++) s->bins[i] += (uint32_t)(i % 3 + 1);
    uint32_t sum = 0;
    for (int i = 0; i < SPECTRUM_CHANNELS; i++) sum += s->bins[i];
    s->total_counts = sum;
    s->total_time_sec += 1;
}

static void copy_stub(void *d, const void *s, size_t n) {
    g_copy_calls++;
    if (g_copy_calls == g_inject_on) {
        memcpy(d, s, n / 2);
        writer_apply(g_src);
        g_seq += 2;
        memcpy((uint8_t *)d + n / 2, (const uint8_t *)s + n / 2, n - n / 2);
    } else {
        memcpy(d, s, n);
    }
}

static void wait_stub(void) {
    g_wait_calls++;
}

static void reset_state(uint32_t seq, int inject) {
    if (g_src) free(g_src);
    g_src = make_src();
    g_seq = seq;
    g_copy_calls = 0;
    g_inject_on = inject;
    g_wait_calls = 0;
}

static void test_seq_clean(void) {
    reset_state(4, 0);
    spectrum_data_t *dst = calloc(1, sizeof(spectrum_data_t));
    if (!dst) exit(2);
    uint32_t retries = 0;
    bool ok = seq_copy(&g_seq, dst, g_src, sizeof(*dst), 3, copy_stub, wait_stub, &retries);
    NCHECK("seq_clean", ok);
    NCHECK("seq_clean", retries == 0);
    NCHECK("seq_clean", g_copy_calls == 1);
    NCHECK("seq_clean", g_wait_calls == 0);
    NCHECK("seq_clean", memcmp(dst, g_src, sizeof(*dst)) == 0);
    NCHECK("seq_clean", sum_ok(dst));
    NCHECK("seq_clean", dst->total_time_sec == (uint32_t)IMP_REAL_TIME);
    free(dst);
}

static void test_seq_torn_once(void) {
    reset_state(10, 1);
    spectrum_data_t *dst = calloc(1, sizeof(spectrum_data_t));
    if (!dst) exit(2);
    uint32_t retries = 0;
    bool ok = seq_copy(&g_seq, dst, g_src, sizeof(*dst), 3, copy_stub, wait_stub, &retries);
    NCHECK("seq_torn_once", ok);
    NCHECK("seq_torn_once", retries == 1);
    NCHECK("seq_torn_once", g_copy_calls == 2);
    NCHECK("seq_torn_once", memcmp(dst, g_src, sizeof(*dst)) == 0);
    NCHECK("seq_torn_once", sum_ok(dst));
    NCHECK("seq_torn_once", dst->total_time_sec == (uint32_t)IMP_REAL_TIME + 1);
    NCHECK("seq_torn_once", g_seq == 12);
    free(dst);
}

static void test_seq_odd_stuck(void) {
    reset_state(7, 0);
    spectrum_data_t *dst = calloc(1, sizeof(spectrum_data_t));
    if (!dst) exit(2);
    uint32_t retries = 0;
    bool ok = seq_copy(&g_seq, dst, g_src, sizeof(*dst), 3, copy_stub, wait_stub, &retries);
    NCHECK("seq_odd_stuck", !ok);
    NCHECK("seq_odd_stuck", g_copy_calls == 0);
    NCHECK("seq_odd_stuck", g_wait_calls == 3);
    NCHECK("seq_odd_stuck", retries == 0);
    free(dst);
}

void seq_copy_suite(void) {
    test_seq_clean();
    test_seq_torn_once();
    test_seq_odd_stuck();
    if (g_src) {
        free(g_src);
        g_src = NULL;
    }
    printf("INFO seq_copy: src=imp_real_bins sum=%u bytes=%u\n", (unsigned)IMP_REAL_SUM, (unsigned)sizeof(spectrum_data_t));
}
