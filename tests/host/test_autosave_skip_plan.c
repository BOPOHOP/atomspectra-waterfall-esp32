// 1.2.36 P1 (E): пропуск записи неизменного current.bin (main/autosave_skip_plan.h). Образцы — боевой захват стенда (fixtures/stand_capture.h), #SA-3a.
#include "autosave_skip_plan.h"
#include "spectrum_import_plan.h"   /* imp_crc32(), spectrum_data_t via atomspectra.h */
#include "test_util.h"
#include "fixtures/stand_capture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define NCHECK(name, cond) do { if (!(cond)) { printf("FAIL %s %s:%d: %s\n", name, __FILE__, __LINE__, #cond); g_failures++; } } while (0)

static spectrum_data_t *make_snap(void) {
    spectrum_data_t *snap = (spectrum_data_t *)calloc(1, sizeof(spectrum_data_t));
    if (!snap) exit(2);
    
    NCHECK("skip_setup", sizeof(STAND_BINS) == sizeof(snap->bins));
    memcpy(snap->bins, STAND_BINS, sizeof(STAND_BINS));
    
    NCHECK("skip_setup", sizeof(STAND_CALIB) == sizeof(snap->calibration));
    memcpy(snap->calibration, STAND_CALIB, sizeof(STAND_CALIB));
    
    strncpy(snap->serial_number, STAND_SERIAL, sizeof(snap->serial_number) - 1);
    snap->serial_number[sizeof(snap->serial_number) - 1] = '\0';
    
    snap->total_counts = STAND_TOTAL;
    snap->total_time_sec = STAND_TIME;
    snap->lost_impulses = STAND_LOST;
    snap->calib_order = 4;
    snap->calib_valid = true;
    snap->valid = true;
    
    return snap;
}

static uint32_t snap_crc(const spectrum_data_t *s) {
    return imp_crc32((const uint8_t *)s, sizeof(*s), 0);
}

static bool mut_no_have(const as_last_t *l, uint32_t crc, uint32_t gen) {
    return l->crc == crc && l->gen == gen;
}

static bool mut_no_gen(const as_last_t *l, uint32_t crc, uint32_t gen) {
    (void)gen;
    return l->have && l->crc == crc;
}

static bool mut_no_crc(const as_last_t *l, uint32_t crc, uint32_t gen) {
    (void)crc;
    return l->have && l->gen == gen;
}

static bool mut_always_false(const as_last_t *l, uint32_t crc, uint32_t gen) {
    (void)l;
    (void)crc;
    (void)gen;
    return false;
}

static int g_n_first, g_n_reset, g_n_changed, g_n_same;

static void count_diff(const as_last_t *l, uint32_t crc, uint32_t gen) {
    if (autosave_unchanged(l, crc, gen) != mut_no_have(l, crc, gen)) g_n_first++;
    if (autosave_unchanged(l, crc, gen) != mut_no_gen(l, crc, gen)) g_n_reset++;
    if (autosave_unchanged(l, crc, gen) != mut_no_crc(l, crc, gen)) g_n_changed++;
    if (autosave_unchanged(l, crc, gen) != mut_always_false(l, crc, gen)) g_n_same++;
}

static void test_skip_same(void) {
    spectrum_data_t *snap = make_snap();
    uint32_t crc = snap_crc(snap);
    as_last_t l = {true, crc, 7};
    
    NCHECK("skip_same", autosave_unchanged(&l, snap_crc(snap), 7));
    count_diff(&l, snap_crc(snap), 7);
    
    spectrum_data_t *snap2 = make_snap();
    NCHECK("skip_same", snap_crc(snap2) == crc);
    NCHECK("skip_same", autosave_unchanged(&l, snap_crc(snap2), 7));
    count_diff(&l, snap_crc(snap2), 7);
    
    free(snap);
    free(snap2);
}

static void test_write_changed_calib(void) {
    spectrum_data_t *base = make_snap();
    uint32_t crc = snap_crc(base);
    as_last_t l = {true, crc, 7};
    
    for (int i = 0; i < 5; i++) {
        spectrum_data_t *copy = make_snap();
        if (copy->calibration[i] == 0.0) {
            copy->calibration[i] = 1e-300;
        } else {
            copy->calibration[i] *= (1.0 + 1e-12);
        }
        NCHECK("write_changed_calib", !autosave_unchanged(&l, snap_crc(copy), 7));
        count_diff(&l, snap_crc(copy), 7);
        free(copy);
    }
    
    for (int k = 0; k < 16; k++) {
        spectrum_data_t *copy = make_snap();
        int idx = 1000 + k * 397;
        copy->bins[idx] += 1;
        NCHECK("write_changed_calib", !autosave_unchanged(&l, snap_crc(copy), 7));
        count_diff(&l, snap_crc(copy), 7);
        free(copy);
    }
    
    spectrum_data_t *copy_serial = make_snap();
    copy_serial->serial_number[0] = (copy_serial->serial_number[0] == 'A') ? 'B' : 'A';
    NCHECK("write_changed_calib", !autosave_unchanged(&l, snap_crc(copy_serial), 7));
    count_diff(&l, snap_crc(copy_serial), 7);
    free(copy_serial);
    
    spectrum_data_t *copy_time = make_snap();
    copy_time->total_time_sec += 1;
    NCHECK("write_changed_calib", !autosave_unchanged(&l, snap_crc(copy_time), 7));
    count_diff(&l, snap_crc(copy_time), 7);
    free(copy_time);
    
    free(base);
}

static void test_write_after_reset(void) {
    spectrum_data_t *snap = make_snap();
    uint32_t crc = snap_crc(snap);
    as_last_t l = {true, crc, 7};
    
    for (int gen = 8; gen <= 15; gen++) {
        NCHECK("write_after_reset", !autosave_unchanged(&l, crc, gen));
        count_diff(&l, crc, gen);
    }
    
    NCHECK("write_after_reset", !autosave_unchanged(&l, crc, 0));
    count_diff(&l, crc, 0);
    
    free(snap);
}

static void test_write_first(void) {
    spectrum_data_t *snap = make_snap();
    uint32_t crc = snap_crc(snap);
    
    uint32_t gens[] = {0, 7, 100};
    for (int i = 0; i < 3; i++) {
        as_last_t l = {false, crc, gens[i]};
        NCHECK("write_first", !autosave_unchanged(&l, crc, gens[i]));
        count_diff(&l, crc, gens[i]);
    }
    
    as_last_t z;
    memset(&z, 0, sizeof(z));
    NCHECK("write_first", !autosave_unchanged(&z, 0, 0));
    count_diff(&z, 0, 0);
    
    free(snap);
}

void autosave_skip_plan_suite(void) {
    g_n_first = 0;
    g_n_reset = 0;
    g_n_changed = 0;
    g_n_same = 0;
    
    test_skip_same();
    test_write_changed_calib();
    test_write_after_reset();
    test_write_first();
    
    printf("INFO autosave_skip: samples differ_first=%d differ_reset=%d differ_changed=%d differ_same=%d\n", g_n_first, g_n_reset, g_n_changed, g_n_same);
}
