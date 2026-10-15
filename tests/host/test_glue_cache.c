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

/* 1.2.37: TCP_NODELAY on the request socket before the body goes out; glue_setsockopt() counts, glue_on_send() snapshots the counter at send time */
static int g_on_at_send = -1;
static void hook_nodelay_at_send(httpd_req_t *r) { (void)r; g_on_at_send = glue_nodelay_on; }
static void nodelay_reset(void) { glue_nodelay_on = glue_nodelay_off = glue_nodelay_other = 0; g_on_at_send = -1; }

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
    /* 1.2.37 glue_nodelay: NODELAY on exactly once per spectrum request, BEFORE the send, and never cleared (httpd_resp_send returns before the data left) */
    glue_on_send = hook_nodelay_at_send;
    httpd_req_t rn; glue_req_init(&rn);
    nodelay_reset();
    NCHECK("glue_nodelay", spectrum_http_send_meta(&rn) == ESP_OK);          /* meta.json is not a spectrum response: no setsockopt */
    NCHECK("glue_nodelay", glue_nodelay_on == 0 && glue_nodelay_off == 0 && glue_nodelay_other == 0);
    glue_req_free(&rn); glue_req_init(&rn);
    nodelay_reset();                                                         /* spectrum.json from the cached buffer */
    NCHECK("glue_nodelay", spectrum_http_send_json(&rn) == ESP_OK && rn.status == 200 && rn.body_len == rj.body_len);
    NCHECK("glue_nodelay", glue_nodelay_on == 1 && glue_nodelay_off == 0 && glue_nodelay_other == 0 && g_on_at_send == 1);
    glue_req_free(&rn); glue_req_init(&rn);
    uint32_t builds_before = s_json_builds;
    glue_now_us += 3000000;                                                  /* TTL expired: this request rebuilds the JSON (second send branch) */
    nodelay_reset();
    NCHECK("glue_nodelay", spectrum_http_send_json(&rn) == ESP_OK && rn.status == 200 && rn.body_len == rj.body_len);
    NCHECK("glue_nodelay", s_json_builds == builds_before + 1);              /* the build branch really ran */
    NCHECK("glue_nodelay", glue_nodelay_on == 1 && glue_nodelay_off == 0 && glue_nodelay_other == 0 && g_on_at_send == 1);
    glue_req_free(&rn); glue_req_init(&rn);
    nodelay_reset();                                                         /* binary /api/spectrum */
    NCHECK("glue_nodelay", spectrum_http_send_bins(&rn) == ESP_OK && rn.status == 200 && rn.body_len > 0);
    NCHECK("glue_nodelay", glue_nodelay_on == 1 && glue_nodelay_off == 0 && glue_nodelay_other == 0 && g_on_at_send == 1);
    glue_req_free(&rn); glue_req_init(&rn);
    glue_setsockopt_fail = 1; nodelay_reset();                               /* setsockopt failure: no crash, the response is still sent */
    NCHECK("glue_nodelay", spectrum_http_send_json(&rn) == ESP_OK && rn.status == 200 && rn.body_len == rj.body_len && rn.sends == 1);
    glue_setsockopt_fail = 0; glue_on_send = NULL;
    glue_req_free(&rn);
    NCHECK("glue_nodelay", glue_sem_depth == 0);
    printf("INFO glue_cache: bins_sum=%" PRIu64 " meta_len=%zu json_len=%zu expected_len=%zu\n", glue_bins_sum(&g_spec), rm.body_len, rj.body_len, exp_len);
    free(exp); glue_req_free(&rm); glue_req_free(&rj); glue_req_free(&r2);
    if (g_failures) { printf("test_glue_cache: %d CHECK(S) FAILED\n", g_failures); return 1; }
    printf("test_glue_cache: OK\n");
    return 0;
}
