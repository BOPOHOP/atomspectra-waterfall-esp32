// #AUD-DUP1 (P0): выбор опоры первой строки после перезагрузки (main/wf_ref_plan.h).
// Сгенерировано gen_code.py (qwen3.6:27b) по scripts/ollama/spec_test_wf_ref_plan.md, проверено вручную.
#include "wf_ref_plan.h"
#include "test_util.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#define N 4
#define OK_BYTES (sizeof(wf_ref_hdr_t) + N * sizeof(uint32_t))

static void make_valid(wf_ref_hdr_t *h, uint32_t *bins);
static void test_pick_accepts_valid(void);
static void test_pick_rejects(void);
static void test_row_delta(void);
static void test_first_row_no_double_count(void);
static void test_first_row_forced_resync(void);
static void test_first_row_device_reset_during_reboot(void);

void test_wf_ref_plan(void)
{
    test_pick_accepts_valid();
    test_pick_rejects();
    test_row_delta();
    test_first_row_no_double_count();
    test_first_row_forced_resync();
    test_first_row_device_reset_during_reboot();
}

static void make_valid(wf_ref_hdr_t *h, uint32_t *bins)
{
    bins[0] = 130;
    bins[1] = 260;
    bins[2] = 390;
    bins[3] = 520;

    h->magic = WF_REF_MAGIC;
    h->channels = N;
    h->boot_session = 41;
    h->prev_total = 1300;
    h->prev_time = 500;
    h->sum = wf_ref_checksum(h, bins, N);
}

/* pick_accepts_valid */
static void test_pick_accepts_valid(void)
{
    wf_ref_hdr_t h;
    uint32_t bins[N];
    make_valid(&h, bins);
    CHECK(wf_ref_pick(&h, bins, OK_BYTES, N, 42) == WF_REF_USE_FILE);
}

/* pick_rejects */
static void test_pick_rejects(void)
{
    wf_ref_hdr_t h;
    uint32_t bins[N];

    /* (a) header NULL */
    make_valid(&h, bins);
    CHECK(wf_ref_pick(NULL, bins, OK_BYTES, N, 42) == WF_REF_FORCE_RESYNC);

    /* (b) size short */
    make_valid(&h, bins);
    CHECK(wf_ref_pick(&h, bins, OK_BYTES - 1, N, 42) == WF_REF_FORCE_RESYNC);

    /* (c) size long */
    make_valid(&h, bins);
    CHECK(wf_ref_pick(&h, bins, OK_BYTES + 4, N, 42) == WF_REF_FORCE_RESYNC);

    /* (d) magic corrupted (resum) */
    make_valid(&h, bins);
    h.magic = WF_REF_MAGIC ^ 1u;
    h.sum = wf_ref_checksum(&h, bins, N);
    CHECK(wf_ref_pick(&h, bins, OK_BYTES, N, 42) == WF_REF_FORCE_RESYNC);

    /* (e) channels field mismatch (resum) */
    make_valid(&h, bins);
    h.channels = N + 1;
    h.sum = wf_ref_checksum(&h, bins, N);
    CHECK(wf_ref_pick(&h, bins, OK_BYTES, N, 42) == WF_REF_FORCE_RESYNC);

    /* (f) boot_session 0 with cur_session 1 (resum) */
    make_valid(&h, bins);
    h.boot_session = 0;
    h.sum = wf_ref_checksum(&h, bins, N);
    CHECK(wf_ref_pick(&h, bins, OK_BYTES, N, 1) == WF_REF_FORCE_RESYNC);

    /* (g) cur_session same as boot */
    make_valid(&h, bins);
    CHECK(wf_ref_pick(&h, bins, OK_BYTES, N, 41) == WF_REF_FORCE_RESYNC);

    /* (h) cur_session = S+2: ещё одна загрузка ИЛИ Сброс (1.2.31) до перезагрузки */
    make_valid(&h, bins);
    CHECK(wf_ref_pick(&h, bins, OK_BYTES, N, 43) == WF_REF_FORCE_RESYNC);

    /* (i) bins pointer NULL */
    make_valid(&h, bins);
    CHECK(wf_ref_pick(&h, NULL, OK_BYTES, N, 42) == WF_REF_FORCE_RESYNC);

    /* (j) corrupted bin without recomputing sum */
    make_valid(&h, bins);
    bins[2] += 1;
    CHECK(wf_ref_pick(&h, bins, OK_BYTES, N, 42) == WF_REF_FORCE_RESYNC);

    /* (k) corrupted header field without recomputing sum */
    make_valid(&h, bins);
    h.prev_total += 1;
    CHECK(wf_ref_pick(&h, bins, OK_BYTES, N, 42) == WF_REF_FORCE_RESYNC);

    /* Checksum changes when bin changes */
    make_valid(&h, bins);
    uint32_t sum_before = wf_ref_checksum(&h, bins, N);
    bins[0] ^= 1u;
    uint32_t sum_after = wf_ref_checksum(&h, bins, N);
    CHECK(sum_before != sum_after);
}

/* row_delta */
static void test_row_delta(void)
{
    CHECK(wf_row_delta(10, 3, false) == 7);
    CHECK(wf_row_delta(3, 10, false) == 0);
    CHECK(wf_row_delta(70000, 0, false) == 65535);
    CHECK(wf_row_delta(5, 100, true) == 5);
    CHECK(wf_row_delta(65535, 0, false) == 65535);
    CHECK(wf_row_delta(65536, 1, false) == 65535);
}

/* first_row_no_double_count */
static void test_first_row_no_double_count(void)
{
    static const uint32_t autosave[N] = {100, 200, 300, 400};
    static const uint32_t last_row[N] = {130, 260, 390, 520};
    static const uint32_t live[N] = {150, 290, 430, 570};

    wf_ref_hdr_t h;
    uint32_t bins[N];
    make_valid(&h, bins);

    wf_ref_choice_t c = wf_ref_pick(&h, bins, OK_BYTES, N, 42);
    CHECK(c == WF_REF_USE_FILE);
    CHECK(wf_ref_first_writes_row(c));

    const uint32_t *prev = wf_ref_first_prev(c, last_row, live);

    uint32_t sum = 0;
    for (int i = 0; i < N; i++) {
        sum += wf_row_delta(live[i], prev[i], false);
    }

    CHECK(sum == (150 - 130) + (290 - 260) + (430 - 390) + (570 - 520));
    uint32_t dup = 0;   // старый код: первая строка против автосейва — двойной счёт
    for (int i = 0; i < N; i++) dup += wf_row_delta(live[i], autosave[i], false);
    CHECK(sum != dup);

    for (int i = 0; i < N; i++) {
        CHECK(last_row[i] + wf_row_delta(live[i], prev[i], false) == live[i]);
    }
}

/* first_row_forced_resync */
static void test_first_row_forced_resync(void)
{
    static const uint32_t live[N] = {150, 290, 430, 570};
    static const uint32_t live2[N] = {160, 300, 445, 590};

    wf_ref_choice_t c = wf_ref_pick(NULL, NULL, 0, N, 42);
    CHECK(c == WF_REF_FORCE_RESYNC);
    CHECK(!wf_ref_first_writes_row(c));

    const uint32_t *prev = wf_ref_first_prev(c, NULL, live);
    CHECK(prev == live);

    uint32_t sum = 0;
    for (int i = 0; i < N; i++) {
        sum += wf_row_delta(live2[i], prev[i], false);
    }
    CHECK(sum == (160 - 150) + (300 - 290) + (445 - 430) + (590 - 570));
}

/* first_row_device_reset_during_reboot */
static void test_first_row_device_reset_during_reboot(void)
{
    wf_ref_hdr_t h;
    uint32_t bins[N];
    make_valid(&h, bins);

    wf_ref_choice_t c = wf_ref_pick(&h, bins, OK_BYTES, N, 42);
    CHECK(c == WF_REF_USE_FILE);

    static const uint32_t live_r[N] = {5, 6, 7, 8};
    const uint32_t *prev = wf_ref_first_prev(c, bins, live_r);

    uint32_t live_total = 5 + 6 + 7 + 8;
    uint32_t prev_total = h.prev_total;
    bool reset = live_total < prev_total;
    CHECK(reset);

    uint32_t sum = 0;
    for (int i = 0; i < N; i++) {
        if (wf_ref_first_writes_row(c)) {
            sum += wf_row_delta(live_r[i], prev[i], reset);
        }
    }
    CHECK(sum == 26);
}
