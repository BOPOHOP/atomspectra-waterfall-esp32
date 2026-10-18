#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <time.h>

#include "glue_alloc.h"

#define malloc(n) glue_malloc(n)
#define realloc(p, n) glue_realloc(p, n)
#define free(p) glue_free(p)
#include "../../main/spectrum_http_cache.c"
#undef malloc
#undef realloc
#undef free

#include "glue_cache_env.h"
#include "spectrum_fold.h"

static int g_failures;
static int g_k0_calls;
static bool k0_hook(httpd_req_t *r, int ch, esp_err_t *o) { (void)r; (void)ch; (void)o; g_k0_calls++; return false; }
#define NCHECK(name, cond) do { if (!(cond)) { printf("FAIL %s %s:%d: %s\n", name, __FILE__, __LINE__, #cond); g_failures++; } } while (0)

static char *load_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz) { free(buf); return NULL; }
    buf[sz] = '\0';
    return buf;
}

static const char *jkey(const char *json, const char *key) {
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(json, pat);
    if (!p) return NULL;
    return p + strlen(pat);
}

static uint32_t ju32(const char *json, const char *key) {
    const char *p = jkey(json, key);
    if (!p) return 0;
    while (*p == ' ' || *p == '\t') p++;
    return (uint32_t)strtoul(p, NULL, 10);
}

static bool parse_bins(const char *json, uint32_t *bins) {
    const char *p = json;
    if (strncmp(p, "{\"bins\":[", 9) != 0) return false;
    p += 9;
    for (int i = 0; i < 8192; i++) {
        bins[i] = (uint32_t)strtoul(p, (char **)&p, 10);
        if (i < 8191) {
            if (*p != ',') return false;
            p++;
        }
    }
    if (*p != ']') return false;
    return true;
}

static int count_key(const char *s, const char *pat) {
    int c = 0;
    const char *p = s;
    size_t len = strlen(pat);
    while ((p = strstr(p, pat)) != NULL) {
        c++;
        p += len;
    }
    return c;
}

static bool body_u32(const httpd_req_t *r, const char *key_with_quotes_and_colon, uint32_t *v) {
    const char *p = strstr(r->body, key_with_quotes_and_colon);
    if (!p) return false;
    p += strlen(key_with_quotes_and_colon);
    while (*p == ' ' || *p == '\t') p++;
    if (*p < '0' || *p > '9') return false;
    *v = (uint32_t)strtoul(p, NULL, 10);
    return true;
}

static uint32_t body_bin0(const httpd_req_t *r) {
    const char *p = r->body;
    if (strncmp(p, "{\"bins\":[", 9) != 0) return 0;
    p += 9;
    return (uint32_t)strtoul(p, NULL, 10);
}

static void new_generation(void) {
    spectrum_http_cache_invalidate();
    glue_now_us += 3000000;
}

int main(void) {
    int g_samples_gen = 0;
    int g_differ_gen = 0;
    int g_samples_v1 = 0;
    int g_differ_v1 = 0;
    int g_samples_trunc = 0;

    // TEST 1: epoch_cache_gen
    glue_fill_stand(&g_spec, 1);
    g_spec.bins[0] = 1000;
    g_live_ep.dev_resets = 1;
    g_live_ep.spec_resets = 2;
    g_boot_id = 0x00AA5555u;
    new_generation();

    // Request A
    {
        httpd_req_t r;
        glue_req_init(&r);
        r.query = "channels=8192";
        spectrum_http_send_json(&r);
        uint32_t v;
        bool ok = (r.status == 200)
            && body_u32(&r, "\"dev_resets\":", &v) && v == 1
            && s_ep.spec_resets == 2
            && body_u32(&r, "\"boot_id\":", &v) && v == 0x00AA5555u
            && body_bin0(&r) == 1000;
        NCHECK("epoch_cache_gen", ok);
        g_samples_gen++;
        if (!ok) g_differ_gen++;
        glue_req_free(&r);
    }

    // Device moves on, cache has not rebuilt
    g_spec.bins[0] = 5000;
    g_live_ep.dev_resets = 3;
    g_live_ep.spec_resets = 4;

    // Request B (should show old generation)
    {
        httpd_req_t r;
        glue_req_init(&r);
        r.query = "channels=8192";
        spectrum_http_send_json(&r);
        uint32_t v;
        bool ok = (r.status == 200)
            && body_bin0(&r) == 1000
            && body_u32(&r, "\"dev_resets\":", &v) && v == 1
            && s_ep.spec_resets == 2;
        NCHECK("epoch_cache_gen", ok);
        g_samples_gen++;
        if (!ok) g_differ_gen++;
        glue_req_free(&r);
    }

    // Rebuild
    new_generation();

    // Request C (should show new generation)
    {
        httpd_req_t r;
        glue_req_init(&r);
        r.query = "channels=8192";
        spectrum_http_send_json(&r);
        uint32_t v;
        bool ok = (r.status == 200)
            && body_bin0(&r) == 5000
            && body_u32(&r, "\"dev_resets\":", &v) && v == 3
            && s_ep.spec_resets == 4;
        NCHECK("epoch_cache_gen", ok);
        g_samples_gen++;
        if (!ok) g_differ_gen++;
        glue_req_free(&r);
    }

    // F4.2 K0: the gzip hook is installed during the frozen v1 check; v1 (no channels) must never reach it, bytes stay as in D2b
    spectrum_http_cache_set_gz_hook(k0_hook);
    // TEST 2: v1_bytes
    for (int i = 1; i <= 5; i++) {
        char path[64];
        snprintf(path, sizeof(path), "fixtures/frozen_146/spectrum_%d.json", i);
        char *f = load_file(path);
        if (!f) {
            NCHECK("v1_bytes", false);
            g_samples_v1++;
            g_differ_v1++;
            continue;
        }

        static uint32_t fb[8192];
        if (!parse_bins(f, fb)) {
            NCHECK("v1_bytes", false);
            g_samples_v1++;
            g_differ_v1++;
            free(f);
            continue;
        }

        memset(&g_spec, 0, sizeof g_spec);
        memcpy(g_spec.bins, fb, sizeof(fb));
        g_spec.total_counts = ju32(f, "total");
        g_spec.total_time_sec = ju32(f, "time");
        g_spec.cps = ju32(f, "cps");
        g_spec.lost_impulses = ju32(f, "lost");
        g_spec.cpu_load = (uint16_t)ju32(f, "cpu");

        const char *t1 = jkey(f, "t1");
        g_spec.temperature[0] = t1 ? strtof(t1, NULL) : 0.0f;
        g_spec.temperature[1] = NAN;
        g_spec.temperature[2] = NAN;

        const char *sn = jkey(f, "serial");
        if (sn && *sn == '"') {
            sn++;
            size_t len = 0;
            while (sn[len] && sn[len] != '"') len++;
            if (len >= sizeof(g_spec.serial_number)) len = sizeof(g_spec.serial_number) - 1;
            memcpy(g_spec.serial_number, sn, len);
            g_spec.serial_number[len] = '\0';
        }

        const char *cal = jkey(f, "calib");
        if (cal && *cal == '[') {
            cal++;
            for (int c = 0; c < 5; c++) {
                char *end;
                g_spec.calibration[c] = strtod(cal, &end);
                cal = end;
                if (c < 4 && *cal == ',') cal++;
            }
        }
        g_spec.calib_order = 4;
        g_spec.calib_valid = true;
        g_spec.valid = true;

        g_live_hist_ok = ju32(f, "hist_ok");
        g_live_hist_drop = ju32(f, "hist_drop");
        g_live_bridge_drop = ju32(f, "bridge_drop");
        g_live_usb_rx_err = ju32(f, "usb_rx_err");
        g_live_rx_ring_drops = ju32(f, "rx_ring_drops");
        g_live_reset_unconfirmed = ju32(f, "reset_unconfirmed");
        g_live_dead = (int)ju32(f, "dead");
        g_live_as_age = (int)ju32(f, "autosave_age_sec");
        g_live_as_streak = (int)ju32(f, "autosave_fail_streak");

        const char *live = jkey(f, "live");
        g_live_live = live ? strtof(live, NULL) : 0.0f;

        g_spec_ok = true;
        g_live_ep.dev_resets = 7;
        g_live_ep.spec_resets = 8;
        g_boot_id = 0x0BADF00Du;
        new_generation();

        httpd_req_t r;
        glue_req_init(&r);
        r.query = NULL;
        spectrum_http_send_json(&r);

        bool bytes_ok = (r.status == 200) && (r.body_len == strlen(f)) && (memcmp(r.body, f, r.body_len) == 0);
        bool no_epoch = (count_key(r.body, "boot_id") == 0)
            && (count_key(r.body, "dev_resets") == 0)
            && (count_key(r.body, "spec_resets") == 0)
            && (count_key(r.body, "calib_channels") == 0);

        NCHECK("v1_bytes", bytes_ok);
        NCHECK("v1_bytes", no_epoch);

        g_samples_v1++;
        if (!bytes_ok) g_differ_v1++;

        glue_req_free(&r);
        free(f);
    }

    NCHECK("v1_bytes", g_k0_calls == 0);   /* F4.2 K0: v1 never reaches the hook */
    spectrum_http_cache_set_gz_hook(NULL);
    // TEST 3: glue_query_trunc
    glue_fill_stand(&g_spec, 1);
    g_live_ep = (spec_epoch_t){0, 0};
    g_spec_ok = true;
    new_generation();

    {
        char q[160];
        q[0] = 'p'; q[1] = '=';
        for (int i = 0; i < 79; i++) q[2 + i] = 'a';
        const char *tail = "&channels=20480";
        memcpy(q + 81, tail, strlen(tail) + 1);

        httpd_req_t r;
        glue_req_init(&r);
        r.query = q;
        spectrum_http_send_json(&r);
        NCHECK("glue_query_trunc", r.status == 400 && r.body_len == 0);
        g_samples_trunc++;
        glue_req_free(&r);
    }

    {   /* К0: длинный query БЕЗ channels - ответ версии 1 (200), как до D2; вход "_=<94 символа>" (96 символов, буфер 95) и 118 символов */
        static const int lens[2] = {94, 116};
        for (int k = 0; k < 2; k++) {
            char q2[160];
            q2[0] = '_'; q2[1] = '=';
            for (int i = 0; i < lens[k]; i++) q2[2 + i] = 'a';
            q2[2 + lens[k]] = '\0';

            httpd_req_t r;
            glue_req_init(&r);
            r.query = q2;
            spectrum_http_send_json(&r);
            NCHECK("glue_query_trunc_v1", r.status == 200 && strncmp(r.body, "{\"bins\":[", 9) == 0 && strstr(r.body, "\"boot_id\"") == NULL);
            g_samples_trunc++;
            glue_req_free(&r);
        }
    }

    {   /* К0: граница - усечённый хвост может начинаться с "channels" (q[] = 95 символов) -> 400; чужой хвост -> 200 */
        static const char *tails[4] = {"&", "&chan", "&channels", "&chx"};
        static const int want400[4] = {1, 1, 1, 0};
        for (int k = 0; k < 4; k++) {
            char q3[160];
            int pre = 95 - (int)strlen(tails[k]);   /* "_=" + a... + хвост ровно 95 символов, дальше (за буфером) идёт "channels=2048" */
            q3[0] = '_'; q3[1] = '=';
            for (int i = 2; i < pre; i++) q3[i] = 'a';
            memcpy(q3 + pre, tails[k], strlen(tails[k]));
            q3[95] = '\0';
            strcat(q3, k == 0 ? "channels=2048" : "x");

            httpd_req_t r;
            glue_req_init(&r);
            r.query = q3;
            spectrum_http_send_json(&r);
            if (want400[k]) { NCHECK("glue_query_trunc", r.status == 400 && r.body_len == 0); }
            else { NCHECK("glue_query_trunc_v1", r.status == 200); }   /* чужой хвост - ответ версии 1 (тот же класс, что и К0-вход) */
            g_samples_trunc++;
            glue_req_free(&r);
        }
    }

    {
        httpd_req_t r;
        glue_req_init(&r);
        r.query = "p=aaaa&channels=2048";
        spectrum_http_send_json(&r);
        NCHECK("glue_query_trunc", r.status == 200 && strncmp(r.body, "{\"bins\":[", 9) == 0);
        g_samples_trunc++;
        glue_req_free(&r);
    }

    printf("INFO k2: test=epoch_cache_gen samples=%d differ_from_ref=%d\n", g_samples_gen, g_differ_gen);
    printf("INFO k2: test=v1_bytes samples=%d differ_from_ref=%d\n", g_samples_v1, g_differ_v1);
    printf("INFO k2: test=glue_query_trunc samples=%d\n", g_samples_trunc);
    NCHECK("glue_sem", glue_sem_depth == 0);

    if (g_failures) {
        printf("test_glue_epoch: %d CHECK(S) FAILED\n", g_failures);
        return 1;
    }
    printf("test_glue_epoch: OK\n");
    return 0;
}
