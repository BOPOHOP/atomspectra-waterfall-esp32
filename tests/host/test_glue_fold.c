// С-2 (release-gate-1.2.36-code-pass1): склейка spectrum_http_cache.c на хосте (сам .c, заглушки IDF в tests/host/glue). Мутант Y4 (нет s_meta_a_end = na;) обязан краснить glue_json_body.
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
#define NCHECK(name, cond) do { if (!(cond)) { printf("FAIL %s %s:%d: %s\n", name, __FILE__, __LINE__, #cond); g_failures++; } } while (0)

/* 1.2.37: TCP_NODELAY on the request socket before the body goes out; glue_setsockopt() counts, glue_on_send() snapshots the counter at send time */
static int g_on_at_send = -1;
__attribute__((unused)) static void hook_nodelay_at_send(httpd_req_t *r) { (void)r; g_on_at_send = glue_nodelay_on; }
__attribute__((unused)) static void nodelay_reset(void) { glue_nodelay_on = glue_nodelay_off = glue_nodelay_other = 0; g_on_at_send = -1; }

/* 1.2.37: how many times the handler set header `field` on this request, and the value of the first one (NULL if none) */
static int hdr_count(const httpd_req_t *r, const char *field) {
    int n = 0;
    for (int i = 0; i < r->hdr_n && i < 16; i++) if (strcmp(r->hdr_field[i], field) == 0) n++;
    return n;
}
__attribute__((unused)) static const char *hdr_val(const httpd_req_t *r, const char *field) {
    for (int i = 0; i < r->hdr_n && i < 16; i++) if (strcmp(r->hdr_field[i], field) == 0) return r->hdr_value[i];
    return NULL;
}

/* Helper to find a_end and b_start in meta string */
static void find_meta_bounds(const char *meta, size_t meta_len, size_t *a_end, size_t *b_start) {
    *a_end = 0;
    *b_start = 0;
    if (!meta || meta_len == 0) return;
    
    const char *needle = strstr(meta, ",\"channels\":");
    if (!needle) return;
    
    *a_end = (size_t)(needle - meta);
    
    // b_start is index of first ',' or '}' after a_end + 1
    // The needle ends with ':', so we start looking after the colon? 
    // Spec says: "b_start = index of the first ',' or '}' after a_end+1"
    // a_end is index of the first ',' in the needle.
    // So we look at meta[a_end + 1] onwards.
    for (size_t i = *a_end + 1; i < meta_len; i++) {
        if (meta[i] == ',' || meta[i] == '}') {
            *b_start = i;
            return;
        }
    }
    // If not found, maybe end of string? But spec implies it exists.
    *b_start = meta_len; 
}

int main(void) {
    glue_fill_stand(&g_spec, 1);
    /* meta.json of the same cache generation */
    httpd_req_t rm; glue_req_init(&rm);
    esp_err_t em = spectrum_http_send_meta(&rm);
    NCHECK("glue_meta_stand", em == ESP_OK);
    
    // Fetch v1 body
    httpd_req_t r1; glue_req_init(&r1);
    r1.query = NULL;
    esp_err_t e1 = spectrum_http_send_json(&r1);
    (void)e1;
    NCHECK("glue_v1_nochange", r1.status == 200 && r1.body_len > 0);
    
    // Compute a_end/b_start from rm.body
    size_t a_end, b_start;
    find_meta_bounds(rm.body, rm.body_len, &a_end, &b_start);
    
    // 2. Test valid N values
    int valid_ns[] = {8192, 4096, 2048, 1024};
    for (int i = 0; i < 4; i++) {
        int N = valid_ns[i];
        char q[64];
        snprintf(q, sizeof(q), "channels=%d", N);
        
        httpd_req_t r; glue_req_init(&r);
        r.query = q;
        esp_err_t e = spectrum_http_send_json(&r);
        (void)e;
        
        NCHECK("glue_v2_status", r.status == 200);
        
        // Compute expected body
        char *exp_buf = malloc(200000);
        if (exp_buf) {
            size_t exp_len = spec_json_full_fmt_v2(exp_buf, 200000, g_spec.bins, N, rm.body, rm.body_len, a_end, b_start, &g_live_ep, g_boot_id);
            NCHECK("glue_v2_body", r.body_len == exp_len && (exp_len == 0 || memcmp(r.body, exp_buf, exp_len) == 0));
            free(exp_buf);
        } else {
            NCHECK("glue_v2_body", 0);
        }
        
        NCHECK("glue_v2_hdrs", hdr_count(&r, "X-Spectrum-Render-Count")==1 && hdr_count(&r, "Cache-Control")==1 && hdr_count(&r, "X-Hist-Age-Ms")==1 && r.hdr_n==3);
        
        glue_req_free(&r);
    }
    
    // Special case: query with extra params
    {
        char q[] = "_=1&channels=2048&x=y";
        httpd_req_t r; glue_req_init(&r);
        r.query = q;
        esp_err_t e = spectrum_http_send_json(&r);
        (void)e;
        
        NCHECK("glue_v2_status", r.status == 200);
        
        char *exp_buf = malloc(200000);
        if (exp_buf) {
            size_t exp_len = spec_json_full_fmt_v2(exp_buf, 200000, g_spec.bins, 2048, rm.body, rm.body_len, a_end, b_start, &g_live_ep, g_boot_id);
            NCHECK("glue_v2_body", r.body_len == exp_len && (exp_len == 0 || memcmp(r.body, exp_buf, exp_len) == 0));
            free(exp_buf);
        } else {
            NCHECK("glue_v2_body", 0);
        }
        
        glue_req_free(&r);
    }
    
    // 3. No cache touch / no leak
    {
        uint32_t builds_before = s_json_builds;
        size_t blocks_before = glue_live_blocks();
        
        char q[] = "channels=4096";
        httpd_req_t r; glue_req_init(&r);
        r.query = q;
        esp_err_t e = spectrum_http_send_json(&r);
        (void)e;
        
        NCHECK("glue_v2_nocache", s_json_builds == builds_before && glue_live_blocks() == blocks_before && glue_sem_depth == 0);
        
        glue_req_free(&r);
    }
    
    // 4. Invalid values
    const char *invalid_qs[] = {"channels=3", "channels=", "channels=abc", "channels=16384", "channels=0", "channels", "channels=04096"};
    for (size_t i = 0; i < sizeof(invalid_qs)/sizeof(invalid_qs[0]); i++) {
        httpd_req_t r; glue_req_init(&r);
        r.query = invalid_qs[i];
        esp_err_t e = spectrum_http_send_json(&r);
        (void)e;
        
        NCHECK("glue_400", r.status == 400 && r.body_len == 0);
        NCHECK("glue_400_sem", glue_sem_depth == 0);
        
        glue_req_free(&r);
    }
    
    // 5. Alternation
    {
        // v1
        httpd_req_t rv1; glue_req_init(&rv1); rv1.query = NULL; spectrum_http_send_json(&rv1);
        NCHECK("glue_alternate", rv1.status == 200 && rv1.body_len == r1.body_len && memcmp(rv1.body, r1.body, r1.body_len) == 0);
        glue_req_free(&rv1);
        
        // v2 4096
        char q4096[] = "channels=4096";
        httpd_req_t rv2; glue_req_init(&rv2); rv2.query = q4096; spectrum_http_send_json(&rv2);
        char *exp4096 = malloc(200000);
        if (exp4096) {
            size_t el = spec_json_full_fmt_v2(exp4096, 200000, g_spec.bins, 4096, rm.body, rm.body_len, a_end, b_start, &g_live_ep, g_boot_id);
            NCHECK("glue_alternate", rv2.status == 200 && rv2.body_len == el && memcmp(rv2.body, exp4096, el) == 0);
            free(exp4096);
        }
        glue_req_free(&rv2);
        
        // v1
        httpd_req_t rv1b; glue_req_init(&rv1b); rv1b.query = NULL; spectrum_http_send_json(&rv1b);
        NCHECK("glue_alternate", rv1b.status == 200 && rv1b.body_len == r1.body_len && memcmp(rv1b.body, r1.body, r1.body_len) == 0);
        glue_req_free(&rv1b);
        
        // v2 2048
        char q2048[] = "channels=2048";
        httpd_req_t rv2b; glue_req_init(&rv2b); rv2b.query = q2048; spectrum_http_send_json(&rv2b);
        char *exp2048 = malloc(200000);
        if (exp2048) {
            size_t el = spec_json_full_fmt_v2(exp2048, 200000, g_spec.bins, 2048, rm.body, rm.body_len, a_end, b_start, &g_live_ep, g_boot_id);
            NCHECK("glue_alternate", rv2b.status == 200 && rv2b.body_len == el && memcmp(rv2b.body, exp2048, el) == 0);
            free(exp2048);
        }
        glue_req_free(&rv2b);
        
        // v1
        httpd_req_t rv1c; glue_req_init(&rv1c); rv1c.query = NULL; spectrum_http_send_json(&rv1c);
        NCHECK("glue_alternate", rv1c.status == 200 && rv1c.body_len == r1.body_len && memcmp(rv1c.body, r1.body, r1.body_len) == 0);
        glue_req_free(&rv1c);
        
        // v2 1024
        char q1024[] = "channels=1024";
        httpd_req_t rv2c; glue_req_init(&rv2c); rv2c.query = q1024; spectrum_http_send_json(&rv2c);
        char *exp1024 = malloc(200000);
        if (exp1024) {
            size_t el = spec_json_full_fmt_v2(exp1024, 200000, g_spec.bins, 1024, rm.body, rm.body_len, a_end, b_start, &g_live_ep, g_boot_id);
            NCHECK("glue_alternate", rv2c.status == 200 && rv2c.body_len == el && memcmp(rv2c.body, exp1024, el) == 0);
            free(exp1024);
        }
        glue_req_free(&rv2c);
        
        // v1
        httpd_req_t rv1d; glue_req_init(&rv1d); rv1d.query = NULL; spectrum_http_send_json(&rv1d);
        NCHECK("glue_alternate", rv1d.status == 200 && rv1d.body_len == r1.body_len && memcmp(rv1d.body, r1.body, r1.body_len) == 0);
        glue_req_free(&rv1d);
    }
    
    // 6. No channels key
    const char *noparam_qs[] = {"x=1", "_=170", "channelsx=4096"};
    for (size_t i = 0; i < sizeof(noparam_qs)/sizeof(noparam_qs[0]); i++) {
        httpd_req_t r; glue_req_init(&r);
        r.query = noparam_qs[i];
        esp_err_t e = spectrum_http_send_json(&r);
        (void)e;
        
        NCHECK("glue_noparam", r.status == 200 && r.body_len == r1.body_len && memcmp(r.body, r1.body, r1.body_len) == 0);
        
        glue_req_free(&r);
    }
    
    // 7. No data
    {
        g_spec_ok = false;
        spectrum_http_cache_invalidate();
        glue_now_us += 3000000;
        
        // channels=4096 -> 404
        char q4096[] = "channels=4096";
        httpd_req_t r404; glue_req_init(&r404); r404.query = q4096; spectrum_http_send_json(&r404);
        NCHECK("glue_v2_404", r404.status == 404);
        glue_req_free(&r404);
        
        // channels=3 -> 400
        char q3[] = "channels=3";
        httpd_req_t r400; glue_req_init(&r400); r400.query = q3; spectrum_http_send_json(&r400);
        NCHECK("glue_v2_404", r400.status == 400);
        glue_req_free(&r400);
        
        // NULL -> 404
        httpd_req_t rnull; glue_req_init(&rnull); rnull.query = NULL; spectrum_http_send_json(&rnull);
        NCHECK("glue_v2_404", rnull.status == 404);
        glue_req_free(&rnull);
        
        // Restore
        g_spec_ok = true;
        spectrum_http_cache_invalidate();
        glue_now_us += 3000000;
    }
    
    printf("INFO glue_fold: v1_len=%zu\n", r1.body_len);
    glue_req_free(&rm);
    glue_req_free(&r1);
    
    if (g_failures) {
        printf("test_glue_fold: %d CHECK(S) FAILED\n", g_failures);
        return 1;
    }
    printf("test_glue_fold: OK\n");
    return 0;
}
