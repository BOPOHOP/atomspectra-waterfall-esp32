// З-3 + З-1 (release-gate-1.2.36-code-pass2): сам обработчик meta.json?w= (main/meta_w_handler.h) на хосте - код ответа, байты тела, окно SMA; meta - живой захват стенда 00000081 через сам spectrum_http_cache.c.
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

/* monitor.c stub: a window of the stand ring (cps 262) - 400 samples, 800 s, 209600 counts */
static int g_mon_calls;
static uint32_t g_mon_w;
void monitor_window(uint32_t w_sec, uint64_t *sum_counts, uint32_t *sum_dur, uint32_t *n) {
    g_mon_calls++;
    g_mon_w = w_sec;
    *sum_counts = 209600u;
    *sum_dur = 800u;
    *n = 400u;
}

#include "../../main/meta_w_handler.h"

static int g_failures;
#define NCHECK(name, cond) do { if (!(cond)) { printf("FAIL %s %s:%d: %s\n", name, __FILE__, __LINE__, #cond); g_failures++; } } while (0)

enum { W_META = 0, W_SMA = 1, W_414 = 414, W_400 = 400 };
typedef struct { const char *q; int want; uint32_t w; } mcase_t;

static char g_long_nw[200];    /* 199-char query without w (utm/fbclid tail of a shared link) */
static char g_q128_nw[129];    /* exactly 128 chars, no w: one more than the 127 the buffer holds */
static char g_long_w[200];     /* w first, then a long tail: 180 chars */
static char g_q127_w[128];     /* exactly 127 chars with w first: still fits */
static char g_cut_w[200];      /* the digits of w cross the 127-char boundary */
static char g_cut_key[200];    /* "&w" ends exactly at the 127-char boundary, "=600" is beyond */

static void fill(char *dst, size_t len, const char *head, const char *tail) {
    /* dst = head + 'A'... + tail, total length len (fbclid-like alnum filler) */
    size_t hl = strlen(head), tl = strlen(tail);
    memcpy(dst, head, hl);
    for (size_t i = hl; i < len - tl; i++) dst[i] = (char)('A' + (i % 26));
    memcpy(dst + len - tl, tail, tl);
    dst[len] = 0;
}

static void build_queries(void) {
    fill(g_long_nw, 199, "_=1760140800123&utm_source=telegram&utm_medium=social&fbclid=", "");
    fill(g_q128_nw, 128, "_=1760140800123&utm_source=telegram&fbclid=", "");
    fill(g_long_w, 180, "w=600&_=1760140800123&utm_source=telegram&fbclid=", "");
    fill(g_q127_w, 127, "w=600&_=1760140800123&fbclid=", "");
    fill(g_cut_w, 140, "_=1760140800123&fbclid=", "");
    memcpy(g_cut_w + 120, "&w=17280000", 11);              /* "&w=" at 120..122, digits 123..130: cut after "1728" */
    g_cut_w[131] = '&';
    fill(g_cut_key, 140, "_=1760140800123&fbclid=", "");
    memcpy(g_cut_key + 125, "&w=600", 6);                    /* the 127-char prefix ends with "&w" */
    g_cut_key[131] = '&';
}

static void run_case(const mcase_t *c, const char *expect_sma) {
    const char *name = c->want == W_META ? "glue_meta_w_meta" : c->want == W_SMA ? "glue_meta_w_sma" : c->want == W_414 ? "glue_meta_w_414" : "glue_meta_w_400";
    int f0 = g_failures;
    httpd_req_t r; glue_req_init(&r);
    r.query = c->q;
    g_mon_calls = 0; g_mon_w = 0xFFFFFFFFu;
    esp_err_t e = meta_w_handle(&r);
    if (c->want == W_META) {
        NCHECK(name, e == ESP_OK && r.status == 200);
        NCHECK(name, r.body != NULL && strcmp(r.body, STAND_META_JSON) == 0);   /* the same bytes as meta.json without w */
        NCHECK(name, r.chunks == 0 && g_mon_calls == 0);
    } else if (c->want == W_SMA) {
        NCHECK(name, e == ESP_OK && r.status == 200);
        NCHECK(name, g_mon_calls == 1 && g_mon_w == c->w);
        NCHECK(name, r.terminated == 1 && r.body != NULL && strcmp(r.body, expect_sma) == 0);
    } else {
        NCHECK(name, e == ESP_FAIL && r.status == c->want);
        NCHECK(name, r.body_len == 0 && g_mon_calls == 0);
    }
    if (g_failures != f0) printf("   case q=%.60s%s want=%d\n", c->q ? c->q : "(none)", (c->q && strlen(c->q) > 60) ? "..." : "", c->want);
    glue_req_free(&r);
}

int main(void) {
    glue_fill_stand(&g_spec, 1);
    build_queries();
    /* expected SMA body: meta without its closing brace + the tail of the same window */
    char tail[128];
    size_t tl = spec_meta_sma_tail(tail, sizeof tail, 209600u, 800u, 400u);
    size_t ml = strlen(STAND_META_JSON);
    static char expect_sma[1024];
    NCHECK("glue_meta_w_sma", tl > 0 && ml > 1 && ml - 1 + tl < sizeof expect_sma);
    memcpy(expect_sma, STAND_META_JSON, ml - 1);
    memcpy(expect_sma + ml - 1, tail, tl + 1);
    const mcase_t cases[] = {
        /* main page: w = win * iv, cache buster _=<ms> */
        { NULL, W_META, 0 },
        { "_=1760140800123", W_META, 0 },
        { "w=100&_=1760140800123", W_SMA, 100 },
        { "_=1760140800123&w=2000", W_SMA, 2000 },
        { g_q127_w, W_SMA, 600 },
        /* CASES_BEHAVIOUR_BEGIN */
        /* pass2 З-1: как 3c73979 - w не целое и запрос без w любой длины -> прежний meta (200); w уместился в буфер -> SMA */
        { "w=abc&_=1760140800123", W_META, 0 },
        { "w=1e+21&_=1760140800123", W_META, 0 },     /* JS w*iv for a huge interval */
        { "w=12.5&_=1760140800123", W_META, 0 },
        { "w=-5&_=1760140800123", W_META, 0 },
        { g_q128_nw, W_META, 0 },
        { g_long_nw, W_META, 0 },
        { g_long_w, W_SMA, 600 },
        /* CASES_BEHAVIOUR_END */
        { g_cut_w, W_414, 0 },
        { g_cut_key, W_414, 0 },
    };
    int n = (int)(sizeof cases / sizeof cases[0]);
    for (int i = 0; i < n; i++) run_case(&cases[i], expect_sma);
    NCHECK("glue_meta_w_meta", glue_sem_depth == 0);
    printf("INFO glue_meta_w: cases=%d q127=%zu q128=%zu long_nw=%zu long_w=%zu cut_w=%zu meta_len=%zu sma_len=%zu\n", n, strlen(g_q127_w), strlen(g_q128_nw), strlen(g_long_nw), strlen(g_long_w), strlen(g_cut_w), ml, strlen(expect_sma));
    if (g_failures) { printf("test_glue_meta_w: %d CHECK(S) FAILED\n", g_failures); return 1; }
    printf("test_glue_meta_w: OK\n");
    return 0;
}
