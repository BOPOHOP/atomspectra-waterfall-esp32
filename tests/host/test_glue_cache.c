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

static int g_failures;
#define NCHECK(name, cond) do { if (!(cond)) { printf("FAIL %s %s:%d: %s\n", name, __FILE__, __LINE__, #cond); g_failures++; } } while (0)

int main(void) {
    glue_fill_stand(&g_spec, 1);
    /* meta.json of the same cache generation */
    httpd_req_t rm; glue_req_init(&rm);
    esp_err_t em = spectrum_http_send_meta(&rm);
    NCHECK("glue_meta_stand", em == ESP_OK);
    NCHECK("glue_meta_stand", rm.status == 200);
    NCHECK("glue_meta_stand", rm.body != NULL && strcmp(rm.body, STAND_META_JSON) == 0);   /* byte-equal to the live stand capture */
    /* spectrum.json built lazily from that meta */
    httpd_req_t rj; glue_req_init(&rj);
    esp_err_t ej = spectrum_http_send_json(&rj);
    NCHECK("glue_json_body", ej == ESP_OK);
    NCHECK("glue_json_body", rj.status == 200);
    NCHECK("glue_json_body", rj.body_len > 0);
    size_t exp_len = 0;
    char *exp = rm.body ? glue_expected_json(rm.body, rm.body_len, g_spec.bins, &exp_len) : NULL;
    NCHECK("glue_json_body", exp != NULL);
    NCHECK("glue_json_body", exp != NULL && rj.body_len == exp_len && rj.body != NULL && memcmp(rj.body, exp, exp_len) == 0);
    NCHECK("glue_json_body", strcmp(rj.render_hdr, rm.render_hdr) == 0);
    /* second request: served from the cached buffer, same bytes */
    httpd_req_t r2; glue_req_init(&r2);
    NCHECK("glue_json_body", spectrum_http_send_json(&r2) == ESP_OK);
    NCHECK("glue_json_body", r2.status == 200 && r2.body_len == rj.body_len && r2.body_len > 0 && memcmp(r2.body, rj.body, rj.body_len) == 0);
    NCHECK("glue_json_body", glue_sem_depth == 0);
    printf("INFO glue_cache: bins_sum=%" PRIu64 " meta_len=%zu json_len=%zu expected_len=%zu\n", glue_bins_sum(&g_spec), rm.body_len, rj.body_len, exp_len);
    free(exp); glue_req_free(&rm); glue_req_free(&rj); glue_req_free(&r2);
    if (g_failures) { printf("test_glue_cache: %d CHECK(S) FAILED\n", g_failures); return 1; }
    printf("test_glue_cache: OK\n");
    return 0;
}
