// 1.2.36 B (H2+H3, R-3): побайтовое равенство spec_meta_fmt()/spec_json_full_fmt() (main/spectrum_meta_fmt.h) со СТАРЫМИ
// build_json_meta/build_json_full. Эталон «до» — fixtures/old_spectrum_json_f97ad6b.inc: ДОСЛОВНО строки 59-205
// main/spectrum_http_cache.c из f97ad6b (получено fixtures/extract_old_json.sh).
// sha256 фрагмента (после удаления CR): 6df4e31cad6bc2a8eb7fe0d9d991f01b702051b1cd0791015f9709ef60cdd2b5 (сверяет mutate_spectrum_meta_fmt.sh).
// Образцы боевого вида (#SA-3a): bins/калибровка — fixtures/imp_real_bins.h и fixtures/stand_capture.h (свежий захват стенда 2026-10-10).
#include "spectrum_meta_fmt.h"
#include "spec_cache_gen.h"
#include "test_util.h"
#include "fixtures/imp_real_bins.h"
#include "fixtures/stand_capture.h"
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define NCHECK(name, cond) do { if (!(cond)) { printf("FAIL %s %s:%d: %s\n", name, __FILE__, __LINE__, #cond); g_failures++; } } while (0)

/* Живые значения, которые старый код брал вызовами: подставляются макросами в дословный фрагмент. */
static spec_meta_live_t g_lv;
static void old_hist_stats(uint32_t *a, uint32_t *b) { *a = g_lv.hist_ok; *b = g_lv.hist_drop; }
#define spectrum_get_hist_stats old_hist_stats
#define usb_host_cdc_spectrometer_dead() (g_lv.dead != 0)
#define spectrum_autosave_age_sec() (g_lv.as_age)
#define spectrum_autosave_fail_streak() (g_lv.as_streak)
#define tcp_bridge_dropped_bytes() (g_lv.bridge_drop)
#define usb_host_cdc_rx_errors() (g_lv.usb_rx_err)
#define usb_host_cdc_rx_ring_drops() (g_lv.rx_ring_drops)
#define spectrum_reset_unconfirmed_count() (g_lv.reset_unconfirmed)
#define spectrum_compute_live_time(sp) ((void)(sp), g_lv.live)
#define compute_live_time(sp) ((void)(sp), g_lv.live)
#define heap_caps_realloc(p, n, caps) realloc((p), (n))
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0
#include "fixtures/old_spectrum_json_f97ad6b.inc"

static int g_n_samples;
static int g_n_live_diff;
static int g_n_chan_in_meta;
static int g_n_close_in_full;
static int g_n_need_false;
static int g_n_need_true;

static spectrum_data_t *make_sp(const uint32_t *bins, const double *calib, int calib_valid, int order, const char *serial) {
    spectrum_data_t *sp = (spectrum_data_t *)calloc(1, sizeof(spectrum_data_t));
    if (!sp) exit(2);
    memcpy(sp->bins, bins, 8192 * sizeof(uint32_t));
    memcpy(sp->calibration, calib, 5 * sizeof(double));
    sp->calib_valid = calib_valid;
    sp->calib_order = order;
    if (serial) {
        strncpy(sp->serial_number, serial, 63);
        sp->serial_number[63] = '\0';
    }
    sp->total_counts = 141502;
    sp->cps = 262;
    sp->lost_impulses = 30151;
    sp->total_time_sec = 197;
    sp->cpu_load = 52;
    sp->temperature[0] = 30.5f;
    sp->temperature[1] = NAN;
    sp->temperature[2] = NAN;
    sp->valid = true;
    return sp;
}

static spec_meta_live_t make_lv(void) {
    spec_meta_live_t lv;
    lv.hist_ok = 59223;
    lv.hist_drop = 1;
    lv.bridge_drop = 0;
    lv.usb_rx_err = 1;
    lv.rx_ring_drops = 0;
    lv.reset_unconfirmed = 0;
    lv.dead = 0;
    lv.as_age = 29;
    lv.as_streak = 0;
    lv.live = 72494.6f;
    return lv;
}

static void compare_one(spectrum_data_t *sp, const spec_meta_live_t *lv) {
    g_lv = *lv;
    g_n_samples++;

    char *om = NULL, *of = NULL;
    size_t oml = 0, ofl = 0;
    bool ok1 = build_json_meta(sp, &om, &oml);
    bool ok2 = build_json_full(sp, &of, &ofl);
    NCHECK("meta_bytes_eq", ok1);
    NCHECK("full_bytes_eq", ok2);
    if (!ok1 || !ok2) {
        free(om);
        free(of);
        return;
    }

    spec_meta_in_t in;
    spec_meta_in_fill(&in, sp, lv);
    char *nm = (char *)malloc(4096);
    if (!nm) { free(om); free(of); return; }
    size_t a_end = 0, b_start = 0;
    size_t n = spec_meta_fmt(nm, 4096, &in, &a_end, &b_start);
    if (n >= 4096) {
        nm = (char *)realloc(nm, n + 1);
        if (!nm) { free(om); free(of); return; }
        n = spec_meta_fmt(nm, n + 1, &in, &a_end, &b_start);
    }
    NCHECK("meta_bytes_eq", n == oml);
    NCHECK("meta_bytes_eq", n == oml && memcmp(nm, om, oml) == 0);
    NCHECK("meta_bytes_eq", nm[n] == '\0');

    char small[16];
    size_t a2 = 0, b2 = 0;
    size_t n2 = spec_meta_fmt(small, sizeof small, &in, &a2, &b2);
    NCHECK("meta_bytes_eq", n2 == n && a2 == a_end && b2 == b_start && small[15] == '\0');
    NCHECK("meta_bytes_eq", memcmp(nm + a_end, ",\"channels\":8192", 16) == 0 && b_start == a_end + 16);

    char *nf = (char *)malloc(70000);
    if (!nf) { free(om); free(of); free(nm); return; }
    size_t fn = spec_json_full_fmt(nf, 70000, sp->bins, nm, n, a_end, b_start);
    if (fn >= 70000) {
        nf = (char *)realloc(nf, fn + 1);
        if (!nf) { free(om); free(of); free(nm); return; }
        fn = spec_json_full_fmt(nf, fn + 1, sp->bins, nm, n, a_end, b_start);
    }
    NCHECK("full_bytes_eq", fn == ofl);
    NCHECK("full_bytes_eq", fn == ofl && memcmp(nf, of, ofl) == 0);
    NCHECK("full_bytes_eq", nf[fn] == '\0');

    if (strstr(nm, ",\"channels\":8192")) g_n_chan_in_meta++;
    if (strstr(nf, "],\"total\":")) g_n_close_in_full++;

    char l1[40], l2[40];
    snprintf(l1, sizeof l1, "%.1f", (double)lv->live);
    snprintf(l2, sizeof l2, "%.2f", (double)lv->live);
    if (l2[strlen(l2) - 1] != '0') g_n_live_diff++;

    free(om);
    free(of);
    free(nm);
    free(nf);
}

static void run_battery(void) {
    int cv, order;
    for (cv = 0; cv <= 1; cv++) {
        for (order = 0; order <= 4; order++) {
            spectrum_data_t *sp = make_sp(IMP_REAL_BINS, IMP_REAL_CALIB, cv, order, STAND_SERIAL);
            spec_meta_live_t lv = make_lv();
            compare_one(sp, &lv);
            free(sp);

            sp = make_sp(STAND_BINS, STAND_CALIB, cv, order, STAND_SERIAL);
            compare_one(sp, &lv);
            free(sp);
        }
    }

    double calib_special[][5] = {
        {-0.0, 1e-300, 0, 1, -1},
        {NAN, 1, 1, 1, 1},
        {1e308, -1e308, 1e-308, 5e-324, 123456789.123456789},
        {0, 0, 0, 0, 0}
    };
    for (int i = 0; i < 4; i++) {
        spectrum_data_t *sp = make_sp(IMP_REAL_BINS, calib_special[i], 1, 4, STAND_SERIAL);
        spec_meta_live_t lv = make_lv();
        compare_one(sp, &lv);
        free(sp);
    }
    {
        double calib_neg0[5] = {IMP_REAL_CALIB[0], IMP_REAL_CALIB[1], -0.0, IMP_REAL_CALIB[3], IMP_REAL_CALIB[4]};
        spectrum_data_t *sp = make_sp(IMP_REAL_BINS, calib_neg0, 1, 4, STAND_SERIAL);
        spec_meta_live_t lv = make_lv();
        compare_one(sp, &lv);
        free(sp);
    }

    char serial63[64];
    memset(serial63, 0, sizeof(serial63));
    for (int i = 0; i < 63; i++) {
        serial63[i] = STAND_SERIAL[i % strlen(STAND_SERIAL)];
    }
    const char *serials[] = {"", serial63, STAND_SERIAL};
    for (int i = 0; i < 3; i++) {
        spectrum_data_t *sp = make_sp(IMP_REAL_BINS, IMP_REAL_CALIB, 1, 3, serials[i]);
        spec_meta_live_t lv = make_lv();
        compare_one(sp, &lv);
        free(sp);
    }

    float temps[][3] = {
        {NAN, NAN, NAN},
        {-0.04f, 0.04f, -0.05f},
        {30.5f, 0.0f, -273.15f},
        {0.25f, 0.35f, 99.95f}
    };
    for (int i = 0; i < 4; i++) {
        spectrum_data_t *sp = make_sp(IMP_REAL_BINS, IMP_REAL_CALIB, 1, 4, STAND_SERIAL);
        sp->temperature[0] = temps[i][0];
        sp->temperature[1] = temps[i][1];
        sp->temperature[2] = temps[i][2];
        spec_meta_live_t lv = make_lv();
        compare_one(sp, &lv);
        free(sp);
    }

    spec_meta_live_t lv_zero;
    memset(&lv_zero, 0, sizeof(lv_zero));
    lv_zero.live = 0.0f;
    spectrum_data_t *sp = make_sp(IMP_REAL_BINS, IMP_REAL_CALIB, 1, 4, STAND_SERIAL);
    compare_one(sp, &lv_zero);
    free(sp);

    spec_meta_live_t lv_max;
    lv_max.hist_ok = UINT32_MAX;
    lv_max.hist_drop = UINT32_MAX;
    lv_max.bridge_drop = UINT32_MAX;
    lv_max.usb_rx_err = UINT32_MAX;
    lv_max.rx_ring_drops = UINT32_MAX;
    lv_max.reset_unconfirmed = UINT32_MAX;
    lv_max.dead = 0;
    lv_max.as_age = -1;
    lv_max.as_streak = 1000000;
    lv_max.live = 0.0f;
    sp = make_sp(IMP_REAL_BINS, IMP_REAL_CALIB, 1, 4, STAND_SERIAL);
    compare_one(sp, &lv_max);
    free(sp);

    sp = make_sp(IMP_REAL_BINS, IMP_REAL_CALIB, 1, 4, STAND_SERIAL);
    sp->total_counts = UINT32_MAX;
    sp->lost_impulses = UINT32_MAX;
    sp->total_time_sec = UINT32_MAX;
    sp->cps = UINT32_MAX;
    sp->cpu_load = 100;
    compare_one(sp, &lv_zero);
    free(sp);

    sp = make_sp(IMP_REAL_BINS, IMP_REAL_CALIB, 1, 4, STAND_SERIAL);
    sp->total_counts = UINT32_MAX;
    sp->lost_impulses = UINT32_MAX;
    sp->total_time_sec = UINT32_MAX;
    sp->cps = UINT32_MAX;
    sp->cpu_load = 100;
    compare_one(sp, &lv_max);
    free(sp);

    float lives[] = {0.05f, 0.15f, 0.25f, 0.35f, 2.675f, 72494.55f, 72494.65f, 1e7f, 16777216.0f, (float)STAND_LIVE, 0.0f, 123456.75f};
    for (int i = 0; i < 12; i++) {
        spec_meta_live_t lv = make_lv();
        lv.live = lives[i];
        sp = make_sp(IMP_REAL_BINS, IMP_REAL_CALIB, 1, 4, STAND_SERIAL);
        compare_one(sp, &lv);
        free(sp);
    }

    uint32_t *bins_zero = (uint32_t *)calloc(8192, sizeof(uint32_t));
    if (bins_zero) {
        sp = make_sp(bins_zero, IMP_REAL_CALIB, 1, 4, STAND_SERIAL);
        spec_meta_live_t lv = make_lv();
        compare_one(sp, &lv);
        free(sp);
        free(bins_zero);
    }

    uint32_t *bins_max = (uint32_t *)malloc(8192 * sizeof(uint32_t));
    if (bins_max) {
        for (int i = 0; i < 8192; i++) bins_max[i] = 4294967295;
        sp = make_sp(bins_max, IMP_REAL_CALIB, 1, 4, STAND_SERIAL);
        spec_meta_live_t lv = make_lv();
        compare_one(sp, &lv);
        free(sp);
        free(bins_max);
    }
}

static void test_meta_stand_capture(void) {
    spectrum_data_t *sp = make_sp(STAND_BINS, STAND_CALIB, 1, 4, STAND_SERIAL);
    sp->total_counts = STAND_TOTAL;
    sp->total_time_sec = STAND_TIME;
    sp->lost_impulses = STAND_LOST;
    sp->cps = STAND_CPS;
    sp->cpu_load = STAND_CPU;
    sp->temperature[0] = (float)STAND_T1;
    sp->temperature[1] = NAN;
    sp->temperature[2] = NAN;

    spec_meta_live_t lv;
    lv.hist_ok = STAND_HIST_OK;
    lv.hist_drop = STAND_HIST_DROP;
    lv.bridge_drop = STAND_BRIDGE_DROP;
    lv.usb_rx_err = STAND_USB_RX_ERR;
    lv.rx_ring_drops = STAND_RX_RING_DROPS;
    lv.reset_unconfirmed = STAND_RESET_UNCONFIRMED;
    lv.dead = STAND_DEAD;
    lv.as_age = STAND_AUTOSAVE_AGE_SEC;
    lv.as_streak = STAND_AUTOSAVE_FAIL_STREAK;
    lv.live = (float)STAND_LIVE;

    spec_meta_in_t in;
    spec_meta_in_fill(&in, sp, &lv);
    char buf[4096];
    size_t a_end = 0, b_start = 0;
    size_t n = spec_meta_fmt(buf, sizeof(buf), &in, &a_end, &b_start);
    NCHECK("meta_bytes_eq", n < sizeof(buf));
    NCHECK("meta_bytes_eq", strcmp(buf, STAND_META_JSON) == 0);

    compare_one(sp, &lv);
    free(sp);
}

static void test_json_need_build(void) {
    struct { bool have; uint32_t jr; uint32_t r; bool expected; } rows[] = {
        {false, 0, 0, true},
        {false, 5, 5, true},
        {true, 5, 5, false},
        {true, 5, 6, true},
        {true, 6, 5, true},
        {true, 0, 0, false},
        {true, UINT32_MAX, UINT32_MAX, false},
        {true, UINT32_MAX, 0, true},
        {true, 0, UINT32_MAX, true},
        {false, UINT32_MAX, 0, true}
    };
    for (int i = 0; i < 10; i++) {
        bool res = spec_json_need_build(rows[i].have, rows[i].jr, rows[i].r);
        NCHECK("json_need_build", (res ? 1 : 0) == (rows[i].expected ? 1 : 0));
        if (rows[i].expected) g_n_need_true++;
        else g_n_need_false++;
    }
}

void spectrum_meta_fmt_suite(void) {
    g_n_samples = 0;
    g_n_live_diff = 0;
    g_n_chan_in_meta = 0;
    g_n_close_in_full = 0;
    g_n_need_false = 0;
    g_n_need_true = 0;

    run_battery();
    test_meta_stand_capture();
    test_json_need_build();

    printf("INFO spectrum_meta_fmt: samples=%d differ_mb1_channels=%d differ_mb2_close=%d differ_mb0_live=%d differ_mb3_need_build_false=%d\n",
           g_n_samples, g_n_chan_in_meta, g_n_close_in_full, g_n_live_diff, g_n_need_true);
}
