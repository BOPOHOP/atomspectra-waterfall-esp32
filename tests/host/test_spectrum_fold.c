// D1 (feat/d1-channels): host-тесты К1 - свёртка bins для GET /api/spectrum.json?channels=N (main/spectrum_fold.h).
// Образцы боевого вида (#SA-3a): 5 снимков spectrum.json с платы .146 (fixtures/frozen_146/, sha256 в frozen MANIFEST Codeaudit).
#include "spectrum_fold.h"
#include "test_util.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define NCHECK(name, cond) do { if (!(cond)) { printf("FAIL %s %s:%d: %s\n", name, __FILE__, __LINE__, #cond); g_failures++; } } while (0)

static char g_meta[2048];
static char g_out[200000];
static char g_v1[200000];
static uint32_t g_bins[8192];
static uint32_t g_bins2[8192];
static uint32_t g_vals[8192];
static uint32_t g_vals2[8192];
static char *g_files[5];
static uint32_t g_total[5];
static int g_samples;
static int g_differ_ref;
static const spec_epoch_t g_ep_t = { 7u, 8u };   /* D2: эпоха/boot_id образцов формата v2 (поля проверяет epoch_fields) */
#define G_BOOT_T 0x0badf00du

static char *load_file(const char *path, size_t *len) {
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
    if (len) *len = (size_t)sz;
    return buf;
}

static const char *parse_bins(const char *json, uint32_t *bins) {
    const char *p = json;
    if (strncmp(p, "{\"bins\":[", 9) != 0) return NULL;
    p += 9;
    for (int i = 0; i < 8192; i++) {
        if (i > 0) {
            if (*p != ',') return NULL;
            p++;
        }
        char *end;
        unsigned long v = strtoul(p, &end, 10);
        if (end == p) return NULL;
        bins[i] = (uint32_t)v;
        p = end;
    }
    if (*p != ']') return NULL;
    p++;
    return p;
}

static int parse_out_bins(const char *out, uint32_t *vals, int max) {
    const char *p = out;
    if (strncmp(p, "{\"bins\":[", 9) != 0) return 0;
    p += 9;
    int count = 0;
    while (*p != ']') {
        if (count >= max) return 0;
        if (count > 0) {
            if (*p != ',') return 0;
            p++;
        }
        char *end;
        unsigned long v = strtoul(p, &end, 10);
        if (end == p) return 0;
        vals[count++] = (uint32_t)v;
        p = end;
    }
    if (*p != ']') return 0;
    return count;
}

static const char *jfield(const char *json, const char *key) {
    char pattern[128];
    snprintf(pattern, sizeof pattern, "\"%s\":", key);
    const char *found = strstr(json, pattern);
    if (!found) return NULL;
    return found + strlen(pattern);
}

static bool build_meta_from_file(const char *json_after_bins, char *meta, size_t cap, size_t *meta_len, size_t *a_end, size_t *b_start) {
    spec_meta_in_t in;
    memset(&in, 0, sizeof in);

    const char *v;
    v = jfield(json_after_bins, "total"); if (v) in.total = (uint32_t)strtoul(v, NULL, 10);
    v = jfield(json_after_bins, "cps"); if (v) in.cps = (uint32_t)strtoul(v, NULL, 10);
    v = jfield(json_after_bins, "lost"); if (v) in.lost = (uint32_t)strtoul(v, NULL, 10);
    v = jfield(json_after_bins, "time"); if (v) in.time = (uint32_t)strtoul(v, NULL, 10);
    v = jfield(json_after_bins, "hist_ok"); if (v) in.hist_ok = (uint32_t)strtoul(v, NULL, 10);
    v = jfield(json_after_bins, "hist_drop"); if (v) in.hist_drop = (uint32_t)strtoul(v, NULL, 10);
    v = jfield(json_after_bins, "bridge_drop"); if (v) in.bridge_drop = (uint32_t)strtoul(v, NULL, 10);
    v = jfield(json_after_bins, "usb_rx_err"); if (v) in.usb_rx_err = (uint32_t)strtoul(v, NULL, 10);
    v = jfield(json_after_bins, "rx_ring_drops"); if (v) in.rx_ring_drops = (uint32_t)strtoul(v, NULL, 10);
    v = jfield(json_after_bins, "reset_unconfirmed"); if (v) in.reset_unconfirmed = (uint32_t)strtoul(v, NULL, 10);
    v = jfield(json_after_bins, "autosave_age_sec"); if (v) in.as_age = (int)strtol(v, NULL, 10);
    v = jfield(json_after_bins, "autosave_fail_streak"); if (v) in.as_streak = (int)strtol(v, NULL, 10);
    v = jfield(json_after_bins, "dead"); if (v) in.dead = (int)strtol(v, NULL, 10);
    v = jfield(json_after_bins, "cpu"); if (v) in.cpu = (unsigned)strtoul(v, NULL, 10);
    v = jfield(json_after_bins, "live"); if (v) in.live = (float)strtod(v, NULL);

    // t1, t2, t3
    const char *keys[3] = {"t1", "t2", "t3"};
    char *dsts[3] = {in.t1, in.t2, in.t3};
    for (int i = 0; i < 3; i++) {
        v = jfield(json_after_bins, keys[i]);
        if (v) {
            size_t len = 0;
            while (v[len] && v[len] != ',' && v[len] != '}') len++;
            if (len >= 16) len = 15;
            memcpy(dsts[i], v, len);
            dsts[i][len] = '\0';
        }
    }

    // serial
    v = jfield(json_after_bins, "serial");
    if (v) {
        if (*v == '"') v++;
        size_t len = 0;
        while (v[len] && v[len] != '"') len++;
        if (len >= 64) len = 63;
        memcpy(in.serial, v, len);
        in.serial[len] = '\0';
    }

    // calib
    in.calib_valid = true;
    in.calib_order = 4;
    v = jfield(json_after_bins, "calib");
    if (v) {
        if (*v == '[') v++;
        for (int i = 0; i < CALIB_COEFFS; i++) {
            if (i > 0) {
                if (*v == ',') v++;
            }
            char *end;
            double d = strtod(v, &end);
            in.calibration[i] = d;
            v = end;
        }
    }

    v = jfield(json_after_bins, "calib_set");
    if (v) {
        in.calib_set = (strncmp(v, "true", 4) == 0);
    }

    *meta_len = spec_meta_fmt(meta, cap, &in, a_end, b_start);
    return (*meta_len < cap) && (*a_end > 0) && (*b_start >= *a_end);
}

static void ref_fold(const uint32_t *bins, int n, uint32_t *out) {
    int m = 8192 / n;
    for (int k = 0; k < n; k++) {
        uint64_t sum = 0;
        for (int j = 0; j < m; j++) {
            sum += bins[k * m + j];
        }
        out[k] = (uint32_t)sum;
    }
}

static void test_channels_parse(void) {
    struct { const char *q; int n; } valid[] = {
        {"channels=8192", 8192},
        {"channels=4096", 4096},
        {"channels=2048", 2048},
        {"channels=1024", 1024},
        {"a=1&channels=2048&b=2", 2048},
        {"x=1&channels=4096", 4096}
    };
    for (size_t i = 0; i < sizeof(valid)/sizeof(valid[0]); i++) {
        int n = 0;
        int r = spec_channels_query_parse(valid[i].q, &n);
        NCHECK("channels_parse_ok", r == 1 && n == valid[i].n);
    }

    const char *absent[] = {NULL, "", "x=1", "xchannels=4096", "mychannels=4096&z=1", "_=1700000000", "channelsx=4096", "channels2=4096&y=1", "Channels=4096"};
    for (size_t i = 0; i < sizeof(absent)/sizeof(absent[0]); i++) {
        int n = -7;
        int r = spec_channels_query_parse(absent[i], &n);
        NCHECK("channels_parse_absent", r == 0 && n == -7);
    }

    const char *invalid[] = {
        "channels=0", "channels=3", "channels=16384", "channels=abc", "channels=",
        "channels", "channels&x=1", "channels=04096", "channels=-4096", "channels=+4096",
        "channels=4096.0", "channels=4096x", "channels= 4096", "channels=%34096",
        "channels=99999", "channels=100000", "channels=0x1000", "channels=4097",
        "channels=512", "channels=2", "channels=1", "channels=8193",
        "channels=1024&channels=3", "channels=1024&channels=1024", "channels=1024&channels", "a=1&channels=4096&b=2&channels=2048"   /* D2 (P3): повтор ключа - 400 */
    };
    for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) {
        int n = -7;
        int r = spec_channels_query_parse(invalid[i], &n);
        NCHECK("channels_400", r == -1 && n == -7);
    }
}

static void test_v1_frozen(void) {
    for (int i = 0; i < 5; i++) {
        size_t len = 0;
        len = strlen(g_files[i]);
        
        uint32_t *bins = g_bins;
        const char *after = parse_bins(g_files[i], bins);
        if (!after) {
            NCHECK("v1_frozen_bytes", 0);
            continue;
        }
        
        size_t meta_len, a_end, b_start;
        if (!build_meta_from_file(after, g_meta, sizeof g_meta, &meta_len, &a_end, &b_start)) {
            NCHECK("v1_frozen_bytes", 0);
            continue;
        }
        
        size_t out_len = spec_json_full_fmt(g_v1, sizeof g_v1, bins, g_meta, meta_len, a_end, b_start);
        NCHECK("v1_frozen_bytes", out_len == len && memcmp(g_v1, g_files[i], len) == 0);
    }
}

static void test_v2_fields(void) {
    int ns[] = {8192, 4096, 2048, 1024};
    for (int i = 0; i < 5; i++) {
        uint32_t *bins = g_bins;
        const char *after = parse_bins(g_files[i], bins);
        if (!after) {
            NCHECK("v2_fields", 0);
            continue;
        }
        
        size_t meta_len, a_end, b_start;
        if (!build_meta_from_file(after, g_meta, sizeof g_meta, &meta_len, &a_end, &b_start)) {
            NCHECK("v2_fields", 0);
            continue;
        }
        
        for (int j = 0; j < 4; j++) {
            int n = ns[j];
            size_t out_len = spec_json_full_fmt_v2(g_out, sizeof g_out, bins, n, g_meta, meta_len, a_end, b_start, &g_ep_t, G_BOOT_T);
            
            // Check start
            NCHECK("v2_fields", strncmp(g_out, "{\"bins\":[", 9) == 0);
            
            // Find end of bins array
            const char *end_arr = strstr(g_out, "],");
            if (!end_arr) {
                NCHECK("v2_fields", 0);
                continue;
            }
            
            const char *tail = end_arr + 2;

            /* Хвост после "],": meta[1..a_end) + ,"channels":N + meta[b_start..meta_len-1) (без закрывающей '}'), далее поля эпохи
               (их проверяет epoch_fields, здесь - только то, что за ними идёт запятая). */
            char expected_tail[2048];
            size_t exp_len = 0;
            memcpy(expected_tail + exp_len, g_meta + 1, a_end - 1);
            exp_len += a_end - 1;
            exp_len += (size_t)snprintf(expected_tail + exp_len, sizeof expected_tail - exp_len, ",\"channels\":%d", n);
            memcpy(expected_tail + exp_len, g_meta + b_start, meta_len - 1 - b_start);
            exp_len += meta_len - 1 - b_start;

            NCHECK("v2_fields", strncmp(tail, expected_tail, exp_len) == 0 && tail[exp_len] == ',');
            NCHECK("v2_fields", g_out[out_len - 1] == '}');
        }
    }
}

static void test_fold_sum(void) {
    int ns[] = {8192, 4096, 2048, 1024};
    for (int i = 0; i < 5; i++) {
        uint32_t *bins = g_bins;
        const char *after = parse_bins(g_files[i], bins);
        if (!after) {
            NCHECK("fold_sum", 0);
            continue;
        }
        
        // Calculate total from bins
        uint64_t sum_bins = 0;
        for (int k = 0; k < 8192; k++) sum_bins += bins[k];
        
        // Get total from file
        const char *v = jfield(after, "total");
        uint32_t file_total = v ? (uint32_t)strtoul(v, NULL, 10) : 0;
        g_total[i] = file_total;
        
        size_t meta_len, a_end, b_start;
        if (!build_meta_from_file(after, g_meta, sizeof g_meta, &meta_len, &a_end, &b_start)) {
            NCHECK("fold_sum", 0);
            continue;
        }
        
        for (int j = 0; j < 4; j++) {
            int n = ns[j];
            (void)spec_json_full_fmt_v2(g_out, sizeof g_out, bins, n, g_meta, meta_len, a_end, b_start, &g_ep_t, G_BOOT_T);
            int count = parse_out_bins(g_out, g_vals, 8192);
            
            uint64_t sum_out = 0;
            for (int k = 0; k < count; k++) sum_out += g_vals[k];
            
            NCHECK("fold_sum", sum_out == (uint64_t)file_total && sum_out == sum_bins);
            
            // Reference comparison
            uint32_t ref[8192];
            ref_fold(bins, n, ref);
            g_samples++;
            bool diff = (count != n);
            for (int k = 0; !diff && k < n; k++) {
                if (g_vals[k] != ref[k]) diff = true;
            }
            if (diff) g_differ_ref++;
            NCHECK("fold_ref", !diff);   /* D2 (P2): независимая ссылка ref_fold, 5 снимков x 4 N = 20 образцов */
        }
    }
}

static void test_fold_width(void) {
    int ns[] = {8192, 4096, 2048, 1024};
    for (int i = 0; i < 5; i++) {
        uint32_t *bins = g_bins;
        const char *after = parse_bins(g_files[i], bins);
        if (!after) {
            NCHECK("fold_width", 0);
            continue;
        }
        
        size_t meta_len, a_end, b_start;
        if (!build_meta_from_file(after, g_meta, sizeof g_meta, &meta_len, &a_end, &b_start)) {
            NCHECK("fold_width", 0);
            continue;
        }
        
        for (int j = 0; j < 4; j++) {
            int n = ns[j];
            (void)spec_json_full_fmt_v2(g_out, sizeof g_out, bins, n, g_meta, meta_len, a_end, b_start, &g_ep_t, G_BOOT_T);
            int count = parse_out_bins(g_out, g_vals, 8192);
            NCHECK("fold_width", count == n);
            
            // Add 1 to every channel
            for (int k = 0; k < 8192; k++) g_bins2[k] = bins[k] + 1;
            
            (void)spec_json_full_fmt_v2(g_out, sizeof g_out, g_bins2, n, g_meta, meta_len, a_end, b_start, &g_ep_t, G_BOOT_T);
            int count2 = parse_out_bins(g_out, g_vals2, 8192);
            
            if (count > 0 && count2 > 0) {
                uint32_t diff = g_vals2[0] - g_vals[0];
                uint32_t expected_diff = 8192 / n;
                NCHECK("fold_width", diff == expected_diff);
            }
        }
    }
}

static void test_fold_origin(void) {
    int ns[] = {8192, 4096, 2048, 1024};
    for (int i = 0; i < 5; i++) {
        uint32_t *bins = g_bins;
        const char *after = parse_bins(g_files[i], bins);
        if (!after) {
            NCHECK("fold_origin", 0);
            continue;
        }
        
        size_t meta_len, a_end, b_start;
        if (!build_meta_from_file(after, g_meta, sizeof g_meta, &meta_len, &a_end, &b_start)) {
            NCHECK("fold_origin", 0);
            continue;
        }
        
        for (int j = 0; j < 4; j++) {
            int n = ns[j];
            (void)spec_json_full_fmt_v2(g_out, sizeof g_out, bins, n, g_meta, meta_len, a_end, b_start, &g_ep_t, G_BOOT_T);
            int count_a = parse_out_bins(g_out, g_vals, 8192);
            
            // Add 1000 to channel 0 only
            for (int k = 0; k < 8192; k++) g_bins2[k] = bins[k];
            g_bins2[0] += 1000;
            
            (void)spec_json_full_fmt_v2(g_out, sizeof g_out, g_bins2, n, g_meta, meta_len, a_end, b_start, &g_ep_t, G_BOOT_T);
            int count_b = parse_out_bins(g_out, g_vals2, 8192);
            
            int min_count = count_a < count_b ? count_a : count_b;
            if (min_count > 0) {
                NCHECK("fold_origin", g_vals2[0] == g_vals[0] + 1000);
                for (int k = 1; k < min_count; k++) {
                    NCHECK("fold_origin", g_vals2[k] == g_vals[k]);
                }
            }
        }
    }
}

static void test_fold_saturate(void) {
    // bins all zero except bins[0] = 0xFFFFFFFFu and bins[1] = 0xFFFFFFFFu
    for (int k = 0; k < 8192; k++) g_bins[k] = 0;
    g_bins[0] = 0xFFFFFFFFu;
    g_bins[1] = 0xFFFFFFFFu;
    
    int n = 4096; // m = 2
    
    // Re-parse file 1 to get the "after" pointer for meta building
    uint32_t *tmp_bins = g_bins2; // Use g_bins2 for parsing file 1
    const char *after1 = parse_bins(g_files[0], tmp_bins);
    if (!after1) {
        NCHECK("fold_saturate", 0);
        return;
    }
    
    size_t meta_len, a_end, b_start;
    if (!build_meta_from_file(after1, g_meta, sizeof g_meta, &meta_len, &a_end, &b_start)) {
        NCHECK("fold_saturate", 0);
        return;
    }
    
    (void)spec_json_full_fmt_v2(g_out, sizeof g_out, g_bins, n, g_meta, meta_len, a_end, b_start, &g_ep_t, G_BOOT_T);
    int count = parse_out_bins(g_out, g_vals, 8192);
    
    if (count > 0) {
        NCHECK("fold_saturate", g_vals[0] == 4294967295u);
    } else {
        NCHECK("fold_saturate", 0);
    }
}

void spectrum_fold_suite(void) {
    g_samples = 0;
    g_differ_ref = 0;
    
    for (int i = 0; i < 5; i++) {
        char path[256];
        snprintf(path, sizeof path, "fixtures/frozen_146/spectrum_%d.json", i + 1);
        size_t len;
        g_files[i] = load_file(path, &len);
        if (!g_files[i]) {
            NCHECK("frozen_load", 0);
            // Free any previously loaded files
            for (int j = 0; j < i; j++) free(g_files[j]);
            return;
        }
    }
    
    test_channels_parse();
    test_v1_frozen();
    test_v2_fields();
    test_fold_sum();
    test_fold_width();
    test_fold_origin();
    test_fold_saturate();
    
    for (int i = 0; i < 5; i++) {
        free(g_files[i]);
        g_files[i] = NULL;
    }
    
    printf("INFO spectrum_fold: samples=%d files=5 n_values=4 differ_from_ref=%d\n", g_samples, g_differ_ref);
}
