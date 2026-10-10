// З-1 (release-gate-1.2.36-code-pass1) + С-1 (pass2): нехватка памяти при росте буфера spectrum.json сверх 64 КиБ - без утечки и с возвратом счётчиков s_readers/s_json_users (сам spectrum_http_cache.c, учёт блоков glue_alloc, счётчики тест только читает).
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

static int g_failures;
#define NCHECK(name, cond) do { if (!(cond)) { printf("FAIL %s %s:%d: %s\n", name, __FILE__, __LINE__, #cond); g_failures++; } } while (0)

/* pass2 С-1: request B arrives while request A sends the cached spectrum.json (A really holds the shared buffer) */
static httpd_req_t g_rb;
static esp_err_t g_eb = ESP_OK;
static int g_nested;
static int g_b_users = -1, g_b_readers = -1;
static size_t g_b_blocks_before, g_b_blocks_after;

static void nested_b(httpd_req_t *ra) {
    (void)ra;
    if (g_nested++) return;                 /* once */
    glue_now_us += 3000000;                 /* cache TTL (2 s) expired: B rebuilds meta, the render count moves on, B must build JSON */
    g_b_blocks_before = glue_live_blocks();
    glue_req_init(&g_rb);
    g_eb = spectrum_http_send_json(&g_rb);
    g_b_users = s_json_users;               /* A still sends: exactly its own claim */
    g_b_readers = s_readers;
    g_b_blocks_after = glue_live_blocks();
}

int main(void) {
    /* live stand spectrum shape, amplitude x100000 (a very long / hot acquisition): the only way the JSON exceeds the 64 KiB first buffer */
    glue_fill_stand(&g_spec, 100000u);
    NCHECK("glue_json_oom", spectrum_http_cache_ensure());     /* cache init + meta, no JSON yet */
    size_t base = glue_live_blocks();
    glue_fail_above = 65536;                                   /* the 64 KiB buffer fits, growing to n+1 fails */
    /* 1) shared buffer path (s_json == NULL, nobody else sending): two failing requests */
    for (int k = 0; k < 2; k++) {
        httpd_req_t r; glue_req_init(&r);
        esp_err_t e = spectrum_http_send_json(&r);
        NCHECK("glue_json_oom", e == ESP_FAIL && r.status == 500);
        NCHECK("glue_json_oom", glue_live_blocks() == base + (s_json != NULL ? 1u : 0u));   /* the buffer is either kept as s_json or freed */
        NCHECK("glue_json_oom", s_json_users == 0);   /* С-1: the claim of the shared buffer is given back */
        NCHECK("glue_json_oom", s_readers == 0);      /* С-1: the reader count is given back */
        glue_req_free(&r);
    }
    /* 2) memory is back: the same request succeeds, the body is larger than 64 KiB, it is now cached */
    glue_fail_above = SIZE_MAX;
    httpd_req_t ok; glue_req_init(&ok);
    esp_err_t e2 = spectrum_http_send_json(&ok);
    NCHECK("glue_json_oom", e2 == ESP_OK && ok.status == 200 && ok.body_len > 65536);
    NCHECK("glue_json_oom", s_json_users == 0 && s_readers == 0);
    /* warm-up: one more cache generation (second meta buffer is allocated now, not inside B) and its JSON */
    glue_now_us += 3000000;
    NCHECK("glue_json_oom", spectrum_http_cache_ensure());
    httpd_req_t ok2; glue_req_init(&ok2);
    NCHECK("glue_json_oom", spectrum_http_send_json(&ok2) == ESP_OK && ok2.status == 200 && ok2.body_len == ok.body_len);
    /* 3) private buffer path: A sends the cached JSON (holds the shared buffer), B comes in during that send and fails to grow */
    size_t before_a = glue_live_blocks();
    glue_fail_above = 65536;
    glue_on_send = nested_b;
    httpd_req_t ra; glue_req_init(&ra);
    esp_err_t ea = spectrum_http_send_json(&ra);
    glue_on_send = NULL;
    NCHECK("glue_json_oom", g_nested == 1);
    NCHECK("glue_json_oom", ea == ESP_OK && ra.status == 200 && ra.body_len == ok.body_len);   /* A is not disturbed */
    NCHECK("glue_json_oom", g_eb == ESP_FAIL && g_rb.status == 500);
    NCHECK("glue_json_oom", g_b_users == 1);          /* only A's claim is left while A sends */
    NCHECK("glue_json_oom", g_b_readers == 0);
    NCHECK("glue_json_oom", g_b_blocks_after == g_b_blocks_before);   /* B's private 64 KiB block is freed */
    NCHECK("glue_json_oom", s_json_users == 0 && s_readers == 0);     /* after A, nothing is held */
    NCHECK("glue_json_oom", glue_live_blocks() == before_a);
    glue_fail_above = SIZE_MAX;
    NCHECK("glue_json_oom", glue_sem_depth == 0);
    printf("INFO glue_cache_oom: bins_sum=%" PRIu64 " json_len=%zu blocks_base=%zu blocks_after=%zu nested=%d b_users=%d b_readers=%d\n", glue_bins_sum(&g_spec), ok.body_len, base, glue_live_blocks(), g_nested, g_b_users, g_b_readers);
    glue_req_free(&ok); glue_req_free(&ok2); glue_req_free(&ra); glue_req_free(&g_rb);
    if (g_failures) { printf("test_glue_cache_oom: %d CHECK(S) FAILED\n", g_failures); return 1; }
    printf("test_glue_cache_oom: OK\n");
    return 0;
}
