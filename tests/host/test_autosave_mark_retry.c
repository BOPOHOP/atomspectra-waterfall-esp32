// С-1 (release-gate-1.2.36-code-pass1): пропуск неизменного одноразового автосейва обязан повторять снятие reset.mark
// (autosave_skip_step, main/autosave_skip_plan.h). Модель spectrum_autosave()/reset_mark_clear_if_current() на боевом
// снимке стенда (fixtures/stand_capture.h, #SA-3a): unlink метки не удался N раз -> следующие автосейвы снимают её.
#include "autosave_skip_plan.h"
#include "spectrum_import_plan.h"
#include "test_util.h"
#include "fixtures/stand_capture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#define NCHECK(name, cond) do { if (!(cond)) { printf("FAIL %s %s:%d: %s\n", name, __FILE__, __LINE__, #cond); g_failures++; } } while (0)

static uint32_t m_reset_gen;
static bool m_mark_present;
static int m_unlink_fail_left;
static int m_unlink_calls;
static as_last_t m_last;
static int m_writes, m_skips;

static void m_mark_clear(uint32_t snap_gen) {
    if (snap_gen == m_reset_gen && m_mark_present) {
        m_unlink_calls++;
        if (m_unlink_fail_left > 0) {
            m_unlink_fail_left--;
        } else {
            m_mark_present = false;
        }
    }
}

static void m_noop(uint32_t g) { (void)g; }

static void m_reset(void) {
    m_reset_gen++;
    m_mark_present = true;
    m_last.have = false;
}

static void m_autosave(const spectrum_data_t *snap, as_mark_retry_fn retry) {
    uint32_t crc = imp_crc32((const uint8_t *)snap, sizeof(*snap), 0);
    uint32_t gen = m_reset_gen;
    if (autosave_skip_step(&m_last, crc, gen, retry)) {
        m_skips++;
        return;
    }
    m_writes++;
    m_mark_clear(gen);
    m_last.have = true;
    m_last.crc = crc;
    m_last.gen = gen;
}

static spectrum_data_t *make_snap(void) {
    spectrum_data_t *s = (spectrum_data_t *)calloc(1, sizeof(spectrum_data_t));
    if (!s) exit(2);
    memcpy(s->bins, STAND_BINS, sizeof(s->bins));
    memcpy(s->calibration, STAND_CALIB, sizeof(s->calibration));
    strncpy(s->serial_number, STAND_SERIAL, sizeof(s->serial_number) - 1);
    s->serial_number[sizeof(s->serial_number) - 1] = '\0';
    s->total_counts = STAND_TOTAL;
    s->total_time_sec = STAND_TIME;
    s->lost_impulses = STAND_LOST;
    s->calib_order = 4;
    s->calib_valid = true;
    s->valid = true;
    return s;
}

static void model_init(int fail_n) {
    m_reset_gen = 7;
    m_mark_present = false;
    m_unlink_fail_left = fail_n;
    m_unlink_calls = 0;
    memset(&m_last, 0, sizeof m_last);
    m_writes = 0;
    m_skips = 0;
}

static int g_scen, g_differ;

static bool run_scenario(int fail_n, int n_after, as_mark_retry_fn retry) {
    model_init(fail_n);
    spectrum_data_t *snap = make_snap();
    m_reset();
    m_autosave(snap, retry);
    for (int i = 0; i < n_after; i++) m_autosave(snap, retry);
    bool mark_left = m_mark_present;
    free(snap);
    return mark_left;
}

static void test_skip_mark_retry(void) {
    int fail_ns[] = {1, 2, 3};
    int n_afters[] = {1, 3, 10};
    for (int fi = 0; fi < 3; fi++) {
        for (int ni = 0; ni < 3; ni++) {
            int fail_n = fail_ns[fi];
            int n_after = n_afters[ni];
            bool left = run_scenario(fail_n, n_after, m_mark_clear);
            int writes = m_writes, skips = m_skips, calls = m_unlink_calls;
            bool want_left = (1 + n_after) <= fail_n;
            NCHECK("skip_mark_retry", left == want_left);
            NCHECK("skip_mark_retry", writes == 1);
            NCHECK("skip_mark_retry", skips == n_after);
            NCHECK("skip_mark_retry", calls == (want_left ? 1 + n_after : fail_n + 1));
            bool left_noop = run_scenario(fail_n, n_after, m_noop);
            g_scen++;
            if (left_noop != left) g_differ++;
        }
    }
}

static void test_skip_no_retry_other_gen(void) {
    model_init(0);
    spectrum_data_t *snap = make_snap();
    m_reset();
    m_autosave(snap, m_mark_clear);
    NCHECK("skip_mark_other_gen", !m_mark_present);
    uint32_t old_gen = m_reset_gen;
    as_last_t old_last = m_last;
    m_reset();
    uint32_t crc = imp_crc32((const uint8_t *)snap, sizeof(*snap), 0);
    bool skipped = autosave_skip_step(&old_last, crc, old_gen, m_mark_clear);
    NCHECK("skip_mark_other_gen", skipped);
    NCHECK("skip_mark_other_gen", m_mark_present);
    m_autosave(snap, m_mark_clear);
    NCHECK("skip_mark_other_gen", m_writes == 2);
    NCHECK("skip_mark_other_gen", !m_mark_present);
    free(snap);
}

void autosave_mark_retry_suite(void) {
    g_scen = 0;
    g_differ = 0;
    test_skip_mark_retry();
    test_skip_no_retry_other_gen();
    printf("INFO autosave_mark_retry: scenarios=%d differ_no_retry=%d\n", g_scen, g_differ);
}
