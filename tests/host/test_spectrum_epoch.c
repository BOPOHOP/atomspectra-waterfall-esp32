#include "spectrum_fold.h"
#include "test_util.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NCHECK(name, cond) do { if (!(cond)) { printf("FAIL %s %s:%d: %s\n", name, __FILE__, __LINE__, #cond); g_failures++; } } while (0)

static char g_out[200000];
static char g_meta[2048];
static char *g_files[5];
static uint32_t g_all_bins[5][8192];

static int g_samples_fields = 0;
static int g_differ_fields = 0;
static int g_samples_cc = 0;
static int g_differ_cc = 0;
static int g_samples_events = 0;
static int g_differ_events = 0;
static int g_samples_mix = 0;
static int g_differ_mix = 0;

static char *load_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz) { free(buf); return NULL; }
    buf[sz] = '\0';
    return buf;
}

static bool parse_bins(const char *json, uint32_t *bins) {
    const char *prefix = "{\"bins\":[";
    size_t plen = strlen(prefix);
    if (strncmp(json, prefix, plen) != 0) return false;
    const char *p = json + plen;
    for (int i = 0; i < 8192; i++) {
        if (*p < '0' || *p > '9') return false;
        char *end = NULL;
        unsigned long v = strtoul(p, &end, 10);
        if (end == p) return false;
        bins[i] = (uint32_t)v;
        p = end;
        if (i < 8191) {
            if (*p != ',') return false;
            p++;
        }
    }
    if (*p != ']') return false;
    return true;
}

static int count_key(const char *s, const char *pattern) {
    int count = 0;
    size_t plen = strlen(pattern);
    const char *p = s;
    while ((p = strstr(p, pattern)) != NULL) {
        count++;
        p += plen;
    }
    return count;
}

static bool key_u32(const char *s, const char *pattern, uint32_t *v) {
    const char *p = strstr(s, pattern);
    if (!p) return false;
    p += strlen(pattern);
    if (*p < '0' || *p > '9') return false;
    char *end = NULL;
    unsigned long long val = strtoull(p, &end, 10);
    if (end == p) return false;
    if (val > UINT32_MAX) return false;
    if (*end != ',' && *end != '}') return false;
    *v = (uint32_t)val;
    return true;
}

static uint32_t ref_fmix(uint32_t h) {
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return h;
}

static uint32_t ref_mix(uint32_t r, uint32_t m, uint32_t t) {
    uint32_t x = ref_fmix(r ^ m ^ t);
    return x ? x : 1u;
}

void spectrum_epoch_suite(void) {
    g_samples_fields = 0;
    g_differ_fields = 0;
    g_samples_cc = 0;
    g_differ_cc = 0;
    g_samples_events = 0;
    g_differ_events = 0;
    g_samples_mix = 0;
    g_differ_mix = 0;

    for (int i = 0; i < 5; i++) {
        char path[256];
        snprintf(path, sizeof(path), "fixtures/frozen_146/spectrum_%d.json", i + 1);
        g_files[i] = load_file(path);
        if (!g_files[i]) {
            NCHECK("frozen_load", 0);
            for (int j = 0; j < i; j++) free(g_files[j]);
            return;
        }
        if (!parse_bins(g_files[i], g_all_bins[i])) {
            NCHECK("frozen_parse", 0);
            for (int j = 0; j <= i; j++) free(g_files[j]);
            return;
        }
    }

    spec_meta_in_t meta_in;
    memset(&meta_in, 0, sizeof(meta_in));
    meta_in.total = 13442130;
    meta_in.cpu = 52;
    meta_in.cps = 118;
    meta_in.lost = 7588;
    meta_in.time = 163682;
    meta_in.live = 163665.7f;
    meta_in.hist_ok = 119812;
    meta_in.hist_drop = 1;
    meta_in.as_age = 18;
    strcpy(meta_in.serial, "00000051");
    strcpy(meta_in.t1, "35.3");
    strcpy(meta_in.t2, "null");
    strcpy(meta_in.t3, "null");
    meta_in.calib_valid = true;
    meta_in.calib_order = 4;
    meta_in.calibration[0] = -2.88786942576384;
    meta_in.calibration[1] = 0.357685723440668;
    meta_in.calibration[2] = 4.06923195185187e-05;
    meta_in.calibration[3] = -3.86805049021517e-09;
    meta_in.calibration[4] = 4.88163459930891e-13;
    meta_in.calib_set = true;

    size_t meta_len = 0;
    size_t a_end = 0;
    size_t b_start = 0;
    meta_len = spec_meta_fmt(g_meta, sizeof(g_meta), &meta_in, &a_end, &b_start);
    NCHECK("epoch_meta", meta_len < sizeof(g_meta) && a_end > 0 && b_start >= a_end);

    struct { uint32_t boot, dev, spec; } cases[3] = {
        {0, 0, 0},
        {0xA1B2C3D4u, 7, 8},
        {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu}
    };
    int ns[4] = {8192, 4096, 2048, 1024};

    for (int i = 0; i < 5; i++) {
        for (int n_idx = 0; n_idx < 4; n_idx++) {
            int N = ns[n_idx];
            for (int c = 0; c < 3; c++) {
                spec_epoch_t ep = {cases[c].dev, cases[c].spec};
                size_t len = spec_json_full_fmt_v2(g_out, sizeof(g_out), g_all_bins[i], N, g_meta, meta_len, a_end, b_start, &ep, cases[c].boot);
                NCHECK("epoch_fields", len > 0 && len < sizeof(g_out) && g_out[len - 1] == '}');

                const char *keys[3] = {"\"boot_id\":", "\"dev_resets\":", "\"spec_resets\":"};
                uint32_t expected[3] = {cases[c].boot, cases[c].dev, cases[c].spec};
                for (int k = 0; k < 3; k++) {
                    NCHECK("epoch_fields", count_key(g_out, keys[k]) == 1);
                    uint32_t val = 0;
                    NCHECK("epoch_fields", key_u32(g_out, keys[k], &val));
                    NCHECK("epoch_fields", val == expected[k]);
                }

                char fragment[128];
                snprintf(fragment, sizeof(fragment), ",\"boot_id\":%" PRIu32 ",\"dev_resets\":%" PRIu32 ",\"spec_resets\":%" PRIu32, cases[c].boot, cases[c].dev, cases[c].spec);
                if (strstr(g_out, fragment) == NULL) {
                    g_differ_fields++;
                }

                const char *bins_end = strstr(g_out, "],");
                const char *boot_pos = strstr(g_out, "\"boot_id\":");
                NCHECK("epoch_fields", bins_end != NULL && boot_pos != NULL && bins_end < boot_pos);

                g_samples_fields++;
            }
        }
    }

    for (int i = 0; i < 5; i++) {
        for (int n_idx = 0; n_idx < 4; n_idx++) {
            int N = ns[n_idx];
            spec_epoch_t ep = {cases[1].dev, cases[1].spec};
            size_t len = spec_json_full_fmt_v2(g_out, sizeof(g_out), g_all_bins[i], N, g_meta, meta_len, a_end, b_start, &ep, cases[1].boot);
            NCHECK("calib_channels", len > 0 && len < sizeof(g_out));
            NCHECK("calib_channels", count_key(g_out, "\"calib_channels\":") == 1);
            uint32_t cc_val = 0;
            NCHECK("calib_channels", key_u32(g_out, "\"calib_channels\":", &cc_val));
            NCHECK("calib_channels", cc_val == 8192);
            if (strstr(g_out, ",\"calib_channels\":8192}") == NULL) {
                g_differ_cc++;
            }
            g_samples_cc++;
        }
    }

    uint32_t states[8][2] = {
        {0, 0},
        {5, 9},
        {0xFFFFFFFFu, 0},
        {0, 0xFFFFFFFFu},
        {1, 1},
        {100, 200},
        {0x80000000u, 0x7FFFFFFFu},
        {3, 0xFFFFFFFEu}
    };
    const char *seqs[6] = {"F", "R", "FF", "RR", "FFR", "RFRFF"};

    for (int s = 0; s < 8; s++) {
        for (int q = 0; q < 6; q++) {
            uint32_t d = states[s][0];
            uint32_t sp = states[s][1];
            uint32_t d_exp = states[s][0];
            uint32_t s_exp = states[s][1];
            for (const char *p = seqs[q]; *p; p++) {
                if (*p == 'F') {
                    spec_epoch_on_fold(&d, &sp);
                    d_exp++;
                } else if (*p == 'R') {
                    spec_epoch_on_user_reset(&d, &sp);
                    s_exp++;
                }
            }
            bool ok = (d == d_exp && sp == s_exp);
            NCHECK("epoch_events", ok);
            if (!ok) g_differ_events++;
            g_samples_events++;
        }
    }

    uint32_t triples[8][3] = {
        {0x12345678, 0x9ABCDEF0, 0x00001000},
        {0xDEADBEEF, 0x00112233, 0xFFFFFFF0},
        {1, 2, 3},
        {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
        {0x0F0F0F0F, 0xF0F0F0F0, 0x13579BDF},
        {0, 0, 0},
        {5, 5, 0},
        {0x1234, 0x1234, 0}
    };

    for (int t = 0; t < 8; t++) {
        uint32_t r = triples[t][0];
        uint32_t m = triples[t][1];
        uint32_t tm = triples[t][2];
        uint32_t id = spec_boot_id_mix(r, m, tm);
        uint32_t ref = ref_mix(r, m, tm);
        bool ok = (id == ref && id != 0);
        NCHECK("boot_id_mix", ok);
        if (!ok) g_differ_mix++;

        bool flip_ok = (spec_boot_id_mix(r ^ 1, m, tm) != id) &&
                       (spec_boot_id_mix(r, m ^ 1, tm) != id) &&
                       (spec_boot_id_mix(r, m, tm ^ 1) != id);
        NCHECK("boot_id_mix", flip_ok);
        if (!flip_ok) g_differ_mix++;

        g_samples_mix++;
    }

    NCHECK("boot_id_mix", spec_boot_id_mix(5, 5, 0) == 1);
    NCHECK("boot_id_mix", spec_boot_id_mix(0, 0, 0) == 1);

    char *src = load_file("../../main/spectrum.c");
    NCHECK("epoch_no_nvs", src != NULL);
    if (src) {
        int begin_count = count_key(src, "/* BOOT_ID_BEGIN */");
        int end_count = count_key(src, "/* BOOT_ID_END */");
        NCHECK("epoch_no_nvs", begin_count == 1);
        NCHECK("epoch_no_nvs", end_count == 1);
        const char *begin = strstr(src, "/* BOOT_ID_BEGIN */");
        const char *end = strstr(src, "/* BOOT_ID_END */");
        NCHECK("epoch_no_nvs", begin != NULL && end != NULL && begin < end);
        if (begin && end) {
            size_t seg_len = (size_t)(end - begin);
            char *seg = (char *)malloc(seg_len + 1);
            if (seg) {
                memcpy(seg, begin, seg_len);
                seg[seg_len] = '\0';
                NCHECK("epoch_no_nvs", strstr(seg, "nvs") == NULL);
                NCHECK("epoch_no_nvs", strstr(seg, "NVS") == NULL);
                NCHECK("epoch_no_nvs", strstr(seg, "nvs_") == NULL);
                NCHECK("epoch_no_nvs", strstr(seg, "esp_random(") != NULL);
                NCHECK("epoch_no_nvs", strstr(seg, "spec_boot_id_mix(") != NULL);
                free(seg);
            }
        }
        NCHECK("epoch_no_nvs", count_key(src, "s_dev_resets++") == 0);
        NCHECK("epoch_no_nvs", count_key(src, "s_reset_gen++") == 0);
        NCHECK("epoch_no_nvs", count_key(src, "s_dev_resets =") == 0);
        NCHECK("epoch_no_nvs", count_key(src, "s_reset_gen =") == 0);
        NCHECK("epoch_no_nvs", count_key(src, "spec_epoch_on_fold(&s_dev_resets, &s_reset_gen)") == 1);
        NCHECK("epoch_no_nvs", count_key(src, "spec_epoch_on_user_reset(&s_dev_resets, &s_reset_gen)") == 1);

        /* К2: сторож порядка строк запасного пути spectrum_get_snapshot_epoch_seq (на хосте реальная функция заменена заглушкой, прогнать её нельзя):
           после s_spec_seq_fallback++ строго SPEC_LOCK -> memcpy(бины) -> spec_read_epoch_live -> SPEC_UNLOCK; чтение эпохи вне замка = пара бины+эпоха разорвана */
        const char *fn = strstr(src, "bool spectrum_get_snapshot_epoch_seq(");
        NCHECK("epoch_fallback_order", fn != NULL && count_key(src, "bool spectrum_get_snapshot_epoch_seq(") == 1);
        if (fn) {
            const char *fn_end = strstr(fn, "\n}");   /* конец функции: '}' в первой колонке (файл может быть CRLF) */
            NCHECK("epoch_fallback_order", fn_end != NULL);
            if (fn_end) {
                size_t fl = (size_t)(fn_end - fn);
                char *body = (char *)malloc(fl + 1);
                if (body) {
                    memcpy(body, fn, fl);
                    body[fl] = '\0';
                    const char *fb = strstr(body, "s_spec_seq_fallback++;");
                    const char *lk = strstr(body, "SPEC_LOCK();");
                    const char *cp = strstr(body, "memcpy(out, &s_spectrum");
                    const char *rd = strstr(body, "spec_read_epoch_live(ep);");
                    const char *ul = strstr(body, "SPEC_UNLOCK();");
                    NCHECK("epoch_fallback_order", count_key(body, "SPEC_LOCK();") == 1 && count_key(body, "SPEC_UNLOCK();") == 1);
                    NCHECK("epoch_fallback_order", count_key(body, "spec_read_epoch_live(ep);") == 1 && count_key(body, "memcpy(out, &s_spectrum") == 1);
                    NCHECK("epoch_fallback_order", fb && lk && cp && rd && ul && fb < lk && lk < cp && cp < rd && rd < ul);
                    free(body);
                }
            }
        }
        free(src);
    }

    char *main_src = load_file("../../main/main.c");
    NCHECK("epoch_no_nvs", main_src != NULL);
    if (main_src) {
        const char *boot_init = strstr(main_src, "spectrum_boot_id_init(");
        const char *wifi_init = strstr(main_src, "wifi_manager_init();");
        NCHECK("epoch_no_nvs", boot_init != NULL && wifi_init != NULL && boot_init > wifi_init);
        free(main_src);
    }

    for (int i = 0; i < 5; i++) {
        free(g_files[i]);
    }

    printf("INFO k2: test=epoch_fields samples=%d differ_from_ref=%d\n", g_samples_fields, g_differ_fields);
    printf("INFO k2: test=calib_channels samples=%d differ_from_ref=%d\n", g_samples_cc, g_differ_cc);
    printf("INFO k2: test=epoch_events samples=%d differ_from_ref=%d\n", g_samples_events, g_differ_events);
    printf("INFO k2: test=boot_id_mix samples=%d differ_from_ref=%d\n", g_samples_mix, g_differ_mix);
}
