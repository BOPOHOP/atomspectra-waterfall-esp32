// З-1 (release-gate-1.2.36-code-pass1): нехватка памяти при росте буфера spectrum.json сверх 64 КиБ - без утечки (сам spectrum_http_cache.c, учёт блоков glue_alloc).
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
        glue_req_free(&r);
    }
    size_t after_shared = glue_live_blocks();
    /* 2) private buffer path (another sender holds the shared buffer) */
    s_json_users = 1;
    {
        httpd_req_t r; glue_req_init(&r);
        esp_err_t e = spectrum_http_send_json(&r);
        NCHECK("glue_json_oom", e == ESP_FAIL && r.status == 500);
        NCHECK("glue_json_oom", glue_live_blocks() == after_shared);
        glue_req_free(&r);
    }
    s_json_users = 0;
    /* 3) memory is back: the same request succeeds and the body is larger than 64 KiB */
    glue_fail_above = SIZE_MAX;
    httpd_req_t ok; glue_req_init(&ok);
    esp_err_t e3 = spectrum_http_send_json(&ok);
    NCHECK("glue_json_oom", e3 == ESP_OK && ok.status == 200 && ok.body_len > 65536);
    NCHECK("glue_json_oom", glue_sem_depth == 0);
    printf("INFO glue_cache_oom: bins_sum=%" PRIu64 " json_len=%zu blocks_base=%zu blocks_after=%zu\n", glue_bins_sum(&g_spec), ok.body_len, base, glue_live_blocks());
    glue_req_free(&ok);
    if (g_failures) { printf("test_glue_cache_oom: %d CHECK(S) FAILED\n", g_failures); return 1; }
    printf("test_glue_cache_oom: OK\n");
    return 0;
}
