// tests/host/test_monitor_window.c
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "monitor_window.h"
#include "spectrum_meta_fmt.h"
#include "fixtures/stand_capture.h"
#include "test_util.h"

#define NCHECK(name, cond) do { if (!(cond)) { printf("FAIL %s %s:%d: %s\n", name, __FILE__, __LINE__, #cond); g_failures++; } } while (0)

static int g_all_pairs = 0;
static int g_wrap_pairs = 0;
static int g_wrap_states = 0;
static int g_differ_mg1 = 0;
static int g_differ_mg2 = 0;
static int g_differ_mg0 = 0;
static int g_tail_null_inputs = 0;
static int g_differ_mg3 = 0;

static uint32_t g_rng = 0x5eed0081u;
static uint32_t rnd(void) {
    g_rng = g_rng * 1664525u + 1013904223u;
    return g_rng >> 8;
}

static uint32_t mon_window_sum_mg1(const monitor_sample_t *ring, uint32_t cap, uint32_t head, uint32_t count,
                                   uint32_t w_sec, uint64_t *sum_counts, uint32_t *sum_dur) {
    uint64_t sc = 0;
    uint32_t sd = 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < count && sd <= w_sec; i++) {
        const monitor_sample_t *s = &ring[(head + cap - 1 - i) % cap];
        sc += s->counts;
        sd += s->dur;
        n++;
    }
    *sum_counts = sc;
    *sum_dur = sd;
    return n;
}

static uint32_t mon_window_sum_mg2(const monitor_sample_t *ring, uint32_t cap, uint32_t head, uint32_t count,
                                   uint32_t w_sec, uint64_t *sum_counts, uint32_t *sum_dur) {
    uint64_t sc = 0;
    uint32_t sd = 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < count && sd < w_sec; i++) {
        const monitor_sample_t *s = &ring[(head + cap - i) % cap];
        sc += s->counts;
        sd += s->dur;
        n++;
    }
    *sum_counts = sc;
    *sum_dur = sd;
    return n;
}

static uint32_t mon_window_sum_mg0(const monitor_sample_t *ring, uint32_t cap, uint32_t head, uint32_t count,
                                   uint32_t w_sec, uint64_t *sum_counts, uint32_t *sum_dur) {
    (void)w_sec;   // дефект: остановка по сумме dur выключена
    uint64_t sc = 0;
    uint32_t sd = 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < count; i++) {
        const monitor_sample_t *s = &ring[(head + cap - 1 - i) % cap];
        sc += s->counts;
        sd += s->dur;
        n++;
    }
    *sum_counts = sc;
    *sum_dur = sd;
    return n;
}

static size_t tail_mg3(char *buf, size_t cap, uint64_t sc, uint32_t sd, uint32_t n) {
    return (size_t)snprintf(buf, cap, ",\"sma\":%.4f,\"sma_w\":%u,\"sma_n\":%u}", (double)sc / (double)sd, sd, n);
}

static void test_win_exact(void) {
    monitor_sample_t ring[8];
    for (uint32_t i = 0; i < 8; i++) {
        ring[i].end_sec = 0;
        ring[i].counts = STAND_CPS;
        ring[i].dur = 1;
        ring[i].t_dc = 0;
    }
    for (uint32_t head = 0; head < 8; head++) {
        for (uint32_t w = 1; w <= 8; w++) {
            uint64_t sc = 0;
            uint32_t sd = 0;
            uint32_t n = mon_window_sum(ring, 8, head, 8, w, &sc, &sd);
            NCHECK("win_exact", n == w);
            NCHECK("win_exact", sd == w);
            NCHECK("win_exact", sc == (uint64_t)w * STAND_CPS);
        }
    }

    for (uint32_t i = 0; i < 8; i++) {
        ring[i].counts = 2 * STAND_CPS;
        ring[i].dur = 2;
    }
    uint32_t ws[] = {2, 4, 6};
    for (uint32_t head = 0; head < 8; head++) {
        for (size_t i = 0; i < 3; i++) {
            uint32_t w = ws[i];
            uint64_t sc = 0;
            uint32_t sd = 0;
            uint32_t n = mon_window_sum(ring, 8, head, 8, w, &sc, &sd);
            NCHECK("win_exact", n == w / 2);
            NCHECK("win_exact", sd == w);
        }
    }

    for (uint32_t i = 0; i < 8; i++) {
        ring[i].counts = STAND_CPS;
        ring[i].dur = 1;
    }
    uint32_t big_ws[] = {16, 100};
    for (uint32_t head = 0; head < 8; head++) {
        for (size_t i = 0; i < 2; i++) {
            uint32_t w = big_ws[i];
            uint64_t sc = 0;
            uint32_t sd = 0;
            uint32_t n = mon_window_sum(ring, 8, head, 8, w, &sc, &sd);
            NCHECK("win_exact", n == 8);
            NCHECK("win_exact", sd == 8);
        }
    }
}

static void test_win_overshoot(void) {
    monitor_sample_t ring[8];
    for (uint32_t i = 0; i < 8; i++) {
        ring[i].end_sec = 0;
        ring[i].counts = 3 * STAND_CPS;
        ring[i].dur = 3;
        ring[i].t_dc = 0;
    }
    uint32_t ws[] = {1, 2, 4, 5, 7, 8, 10, 11};
    for (uint32_t head = 0; head < 8; head++) {
        for (size_t i = 0; i < 8; i++) {
            uint32_t w = ws[i];
            uint64_t sc = 0;
            uint32_t sd = 0;
            uint32_t n = mon_window_sum(ring, 8, head, 8, w, &sc, &sd);
            uint32_t exp_n = (w + 2) / 3;
            NCHECK("win_overshoot", n == exp_n);
            NCHECK("win_overshoot", sd == 3 * exp_n);
            NCHECK("win_overshoot", sc == (uint64_t)exp_n * 3 * STAND_CPS);
        }
    }
}

static void test_win_wrap(void) {
    g_rng = 0x5eed0081u;
    static monitor_sample_t hist[700];
    uint32_t running_time = STAND_TIME;
    for (int k = 0; k < 700; k++) {
        uint32_t r = rnd() % 100;
        uint32_t dur = (r < 90) ? 1 : ((r < 97) ? 2 : 3);
        int32_t c = (int32_t)dur * (int32_t)STAND_CPS + (int32_t)(rnd() % 33) - 16;
        hist[k].end_sec = running_time;
        hist[k].counts = (uint32_t)c;
        hist[k].dur = dur;
        hist[k].t_dc = 305;
        running_time += dur;
    }

    uint32_t cap = 250;
    static monitor_sample_t ring[250];
    uint32_t head = 0;
    uint32_t count = 0;

    uint32_t ws[] = {1, 2, 3, 5, 10, 60, 100, 250, 300};

    for (int k = 0; k < 700; k++) {
        ring[head] = hist[k];
        head = (head + 1) % cap;
        if (count < cap) count++;

        if ((uint32_t)k >= cap) {
            g_wrap_states++;
        }

        for (size_t wi = 0; wi < 9; wi++) {
            uint32_t w = ws[wi];
            g_all_pairs++;

            uint64_t sc = 0;
            uint32_t sd = 0;
            uint32_t n = mon_window_sum(ring, cap, head, count, w, &sc, &sd);

            uint64_t sc1 = 0;
            uint32_t sd1 = 0;
            uint32_t n1 = mon_window_sum_mg1(ring, cap, head, count, w, &sc1, &sd1);
            if (n1 != n || sd1 != sd || sc1 != sc) g_differ_mg1++;

            uint64_t sc2 = 0;
            uint32_t sd2 = 0;
            uint32_t n2 = mon_window_sum_mg2(ring, cap, head, count, w, &sc2, &sd2);
            if (n2 != n || sd2 != sd || sc2 != sc) g_differ_mg2++;

            uint64_t sc0 = 0;
            uint32_t sd0 = 0;
            uint32_t n0 = mon_window_sum_mg0(ring, cap, head, count, w, &sc0, &sd0);
            if (n0 != n || sd0 != sd || sc0 != sc) g_differ_mg0++;

            uint64_t ref_sc = 0;
            uint32_t ref_sd = 0;
            uint32_t ref_n = 0;
            for (uint32_t i = 0; i < count && ref_sd < w; i++) {
                uint32_t idx = (uint32_t)k - i;
                if (idx > (uint32_t)k) break;
                const monitor_sample_t *s = &hist[idx];
                ref_sc += s->counts;
                ref_sd += s->dur;
                ref_n++;
            }

            if (ref_sd == w) {
                continue;
            }

            g_wrap_pairs++;
            NCHECK("win_wrap", n == ref_n);
            NCHECK("win_wrap", sd == ref_sd);
            NCHECK("win_wrap", sc == ref_sc);
        }
    }
}

static void test_win_empty(void) {
    monitor_sample_t ring[8];
    memset(ring, 0xA5, sizeof(ring));
    for (uint32_t head = 0; head < 8; head++) {
        uint32_t ws[] = {0, 1, 100, UINT32_MAX};
        for (size_t i = 0; i < 4; i++) {
            uint32_t w = ws[i];
            uint64_t sc = 0xDEAD;
            uint32_t sd = 0xBEEF;
            uint32_t n = mon_window_sum(ring, 8, head, 0, w, &sc, &sd);
            NCHECK("win_empty", n == 0);
            NCHECK("win_empty", sc == 0);
            NCHECK("win_empty", sd == 0);
        }
    }
}

static void test_sma_tail_null(void) {
    char buf[160];
    char buf_mg3[160];

    size_t len = spec_meta_sma_tail(buf, sizeof(buf), 0, 0, 0);
    NCHECK("sma_tail_null", strcmp(buf, ",\"sma\":null,\"sma_w\":0,\"sma_n\":0}") == 0);
    NCHECK("sma_tail_null", len == strlen(buf));
    g_tail_null_inputs++;
    size_t len_mg3 = tail_mg3(buf_mg3, sizeof(buf_mg3), 0, 0, 0);
    if (len_mg3 != len || strcmp(buf, buf_mg3) != 0) g_differ_mg3++;

    len = spec_meta_sma_tail(buf, sizeof(buf), 5, 0, 0);
    NCHECK("sma_tail_null", strcmp(buf, ",\"sma\":null,\"sma_w\":0,\"sma_n\":0}") == 0);
    NCHECK("sma_tail_null", len == strlen(buf));
    g_tail_null_inputs++;
    len_mg3 = tail_mg3(buf_mg3, sizeof(buf_mg3), 5, 0, 0);
    if (len_mg3 != len || strcmp(buf, buf_mg3) != 0) g_differ_mg3++;

    len = spec_meta_sma_tail(buf, sizeof(buf), 0, 0, 3);
    NCHECK("sma_tail_null", strcmp(buf, ",\"sma\":null,\"sma_w\":0,\"sma_n\":3}") == 0);
    NCHECK("sma_tail_null", len == strlen(buf));
    g_tail_null_inputs++;
    len_mg3 = tail_mg3(buf_mg3, sizeof(buf_mg3), 0, 0, 3);
    if (len_mg3 != len || strcmp(buf, buf_mg3) != 0) g_differ_mg3++;

    size_t meta_len = strlen(STAND_META_JSON);
    char *composed = (char *)malloc(meta_len + 160);
    if (composed) {
        memcpy(composed, STAND_META_JSON, meta_len - 1);
        size_t tail_len = spec_meta_sma_tail(composed + meta_len - 1, 160, 0, 0, 0);
        NCHECK("sma_tail_null", strlen(composed) == meta_len - 1 + tail_len);
        NCHECK("sma_tail_null", composed[strlen(composed) - 1] == '}');
        NCHECK("sma_tail_null", strstr(composed, "\"serial\":\"" STAND_SERIAL "\"") != NULL);   // серийник боевого захвата стенда
        NCHECK("sma_tail_null", strstr(composed, ",\"sma\":null,\"sma_w\":0,\"sma_n\":0}") != NULL);
        free(composed);
    }
}

static void test_sma_tail_value(void) {
    char buf[160];
    size_t len = spec_meta_sma_tail(buf, sizeof(buf), 26200, 100, 100);
    NCHECK("sma_tail_value", strcmp(buf, ",\"sma\":262.0000,\"sma_w\":100,\"sma_n\":100}") == 0);
    NCHECK("sma_tail_value", len == strlen(buf));

    char expected[160];
    snprintf(expected, sizeof(expected), ",\"sma\":%.4f,\"sma_w\":%u,\"sma_n\":%u}", (double)18425683 / (double)72546, 72546, 1);
    len = spec_meta_sma_tail(buf, sizeof(buf), 18425683, 72546, 1);
    NCHECK("sma_tail_value", strcmp(buf, expected) == 0);
    NCHECK("sma_tail_value", len == strlen(buf));

    len = spec_meta_sma_tail(buf, sizeof(buf), 1, 3, 1);
    NCHECK("sma_tail_value", strcmp(buf, ",\"sma\":0.3333,\"sma_w\":3,\"sma_n\":1}") == 0);
    NCHECK("sma_tail_value", len == strlen(buf));

    char buf_trunc[64];
    memset(buf_trunc, 'x', sizeof(buf_trunc));
    volatile size_t trunc_cap = 8;   // усечённый вызов: ёмкость задаётся рантаймом (иначе gcc видит усечение и ломает -Werror), память за ней - 'x'
    len = spec_meta_sma_tail(buf_trunc, trunc_cap, 26200, 100, 100);
    NCHECK("sma_tail_value", buf_trunc[8] == 'x');   // за пределы cap не писали
    NCHECK("sma_tail_value", len >= 8);
    NCHECK("sma_tail_value", buf_trunc[7] == '\0');
}

void monitor_window_suite(void) {
    (void)STAND_CALIB;   // фикстура целиком из захвата стенда; здесь нужны STAND_CPS/STAND_TIME/STAND_SERIAL/STAND_META_JSON
    (void)STAND_BINS;
    test_win_exact();
    test_win_overshoot();
    test_win_wrap();
    test_win_empty();
    test_sma_tail_null();
    test_sma_tail_value();

    printf("INFO monitor_window: all_pairs=%d checked_pairs=%d wrap_states=%d differ_mg1=%d differ_mg2=%d differ_mg0=%d tail_null_inputs=%d differ_mg3=%d\n",
           g_all_pairs, g_wrap_pairs, g_wrap_states, g_differ_mg1, g_differ_mg2, g_differ_mg0, g_tail_null_inputs, g_differ_mg3);
}
