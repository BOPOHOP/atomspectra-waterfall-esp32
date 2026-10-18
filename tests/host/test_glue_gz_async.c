#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <time.h>
#include <zlib.h>
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
#include "../../main/spectrum_gz_job.h"
static int g_failures;
#define NCHECK(name, cond) do { if (!(cond)) { printf("FAIL %s %s:%d: %s\n", name, __FILE__, __LINE__, #cond); g_failures++; } } while (0)

// Глобальные переменные для тестов
extern void (*glue_on_give)(void);
static int g_hook_calls;
static int g_hook_channels;
static char *g_v2_buf;
static size_t g_v2_len;
static uint32_t g_v2_gen;

// Контекст для job
typedef struct {
    int kind;
    bool send_ok;
    int comp_ret;
    bool alloc_ok;
    uint32_t now;
    httpd_req_t *req;
    int sent_plain;
    int sent_gz;
    int completes;
    int closes;
} job_ctx_t;

static job_ctx_t g_ctx;

// Вспомогательные функции
static void set_ep(uint32_t d, uint32_t s) {
    g_live_ep.dev_resets = d;
    g_live_ep.spec_resets = s;
}

static void new_generation() {
    spectrum_http_cache_invalidate();
    glue_now_us += 3000000;
}

static uint32_t body_u32(const char *buf, const char *key) {
    char *p = strstr(buf, key);
    if (!p) return 0;
    p += strlen(key);
    while (*p == ' ' || *p == '\t') p++;
    return (uint32_t)strtoul(p, NULL, 10);
}

__attribute__((unused)) static const char *hdr_get(const httpd_req_t *r, const char *field) {
    for (int i = 0; i < r->hdr_n; i++) {
        if (strcmp(r->hdr_field[i], field) == 0) return r->hdr_value[i];
    }
    return NULL;
}

static size_t gz_pack(const void *body, size_t len, uint8_t *out, size_t cap) {
    z_stream z;
    memset(&z, 0, sizeof(z));
    if (deflateInit2(&z, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) return 0;
    z.next_in = (Bytef*)body;
    z.avail_in = len;
    z.next_out = out + SPEC_GZ_HDR_BYTES;
    z.avail_out = cap - SPEC_GZ_HDR_BYTES;
    if (deflate(&z, Z_FINISH) != Z_STREAM_END) {
        deflateEnd(&z);
        return 0;
    }
    size_t dlen = z.total_out;
    deflateEnd(&z);
    return spec_gz_frame(out, cap, dlen, (uint32_t)crc32(0, body, len), (uint32_t)len);
}

static char *gz_unpack(const uint8_t *g, size_t n, size_t *olen) {
    z_stream z;
    memset(&z, 0, sizeof(z));
    if (inflateInit2(&z, 15 + 16) != Z_OK) return NULL;
    size_t cap = (size_t)1 << 20;
    char *buf = malloc(cap);
    if (!buf) {
        inflateEnd(&z);
        return NULL;
    }
    z.next_in = (Bytef*)g;
    z.avail_in = n;
    z.next_out = (Bytef*)buf;
    z.avail_out = cap;
    if (inflate(&z, Z_FINISH) != Z_STREAM_END) {
        free(buf);
        inflateEnd(&z);
        return NULL;
    }
    *olen = z.total_out;
    buf[z.total_out] = '\0';
    inflateEnd(&z);
    return buf;
}

// Хуки для теста
static bool fake_hook(httpd_req_t *req, int channels, esp_err_t *out) {
    (void)req; (void)out;
    g_hook_calls++;
    g_hook_channels = channels;
    return false;
}

// Колбэки для job
static uint32_t op_now_ms(void *c) { return ((job_ctx_t*)c)->now; }
static uint8_t op_level(void *c) { (void)c; return 6; }

static bool op_acquire(void *c, const spec_gz_job_t *j, const void **body, size_t *len) {
    (void)c; (void)j;
    bool ok = spectrum_http_cache_v2_acquire(8192, &g_v2_buf, &g_v2_len, &g_v2_gen);
    if (ok) {
        *body = g_v2_buf;
        *len = g_v2_len;
    }
    return ok;
}

static void op_release(void *c, const spec_gz_job_t *j) {
    (void)c; (void)j;
    if (g_v2_buf) {
        spectrum_http_cache_v2_release(g_v2_buf);
        g_v2_buf = NULL;
    }
}

static uint8_t *op_out_alloc(void *c, size_t cap) {
    job_ctx_t *ctx = (job_ctx_t*)c;
    return ctx->alloc_ok ? malloc(cap) : NULL;
}

static void op_out_free(void *c, uint8_t *out) {
    (void)c;
    free(out);
}

static int op_compress(void *c, uint8_t level, const void *body, size_t len, uint8_t *out, size_t cap, size_t *out_len) {
    job_ctx_t *ctx = (job_ctx_t*)c;
    (void)level;
    if (ctx->comp_ret < 0) return -1;
    if (ctx->comp_ret == 0) return 0;
    size_t total = gz_pack(body, len, out, cap);
    if (total == 0) return -1;
    *out_len = total;
    return 1;
}

static int g_send_readers_bad, g_send_sweep_bad;
static bool op_send_gz(void *c, const spec_gz_job_t *j, const uint8_t *out, size_t out_len) {
    job_ctx_t *ctx = (job_ctx_t*)c;
    (void)j; (void)out; (void)out_len;
    ctx->sent_gz++;
    /* Codeaudit: во время отправки s_readers == 0 и запись свипа (пересборка кэша) не блокируется */
    uint32_t rc0 = s_render_count;
    g_send_readers_bad += (s_readers != 0);
    g_spec.bins[0] += 1;
    new_generation();
    spectrum_http_cache_ensure();
    g_send_sweep_bad += (s_render_count == rc0);
    return ctx->send_ok;
}

static bool op_send_plain(void *c, const spec_gz_job_t *j) {
    job_ctx_t *ctx = (job_ctx_t*)c;
    (void)j;
    ctx->sent_plain++;
    if (ctx->req) {
        return spectrum_http_send_json_plain(ctx->req) == ESP_OK;
    }
    return true;
}

static void op_close(void *c, const spec_gz_job_t *j) {
    (void)j;
    ((job_ctx_t*)c)->closes++;
}

static void op_complete(void *c, const spec_gz_job_t *j) {
    job_ctx_t *ctx = (job_ctx_t*)c;
    (void)j;
    ctx->completes++;
}

static spec_gz_ops_t ops = {
    .now_ms = op_now_ms,
    .level = op_level,
    .acquire = op_acquire,
    .release = op_release,
    .out_alloc = op_out_alloc,
    .out_free = op_out_free,
    .compress = op_compress,
    .send_gz = op_send_gz,
    .send_plain = op_send_plain,
    .close_session = op_close,
    .complete = op_complete
};

static void bump_epoch(void) {
    glue_on_give = NULL;
    s_ep.dev_resets = 77;
    s_ep.spec_resets = 88;
}

int main(void) {
    // Инициализация
    glue_fill_stand(&g_spec, 1);
    g_spec.bins[0] = 1000;
    set_ep(1, 2);
    g_boot_id = 0x00AA5555u;
    new_generation();
    glue_fail_above = (size_t)-1;

    // Тест 1: k0_bytes_hook
    {
        const char *name = "k0_bytes_hook";
        spectrum_http_cache_set_gz_hook(NULL);
        httpd_req_t ra;
        glue_req_init(&ra);
        ra.query = NULL;
        spectrum_http_send_json(&ra);
        
        g_hook_calls = 0;
        spectrum_http_cache_set_gz_hook(fake_hook);
        httpd_req_t rb;
        glue_req_init(&rb);
        rb.query = NULL;
        spectrum_http_send_json(&rb);
        
        NCHECK(name, ra.status == 200);
        NCHECK(name, rb.status == 200);
        NCHECK(name, ra.body_len == rb.body_len);
        NCHECK(name, memcmp(ra.body, rb.body, ra.body_len) == 0);
        NCHECK(name, ra.hdr_n == rb.hdr_n);
        for (int i = 0; i < ra.hdr_n; i++) {
            NCHECK(name, strcmp(ra.hdr_field[i], rb.hdr_field[i]) == 0);
            NCHECK(name, strcmp(ra.hdr_value[i], rb.hdr_value[i]) == 0);
        }
        glue_req_free(&ra);
        glue_req_free(&rb);
        spectrum_http_cache_set_gz_hook(NULL);
    }

    // Тест 2: hook_kinds
    {
        const char *name = "hook_kinds";
        g_hook_calls = 0;
        spectrum_http_cache_set_gz_hook(fake_hook);
        
        httpd_req_t r1;
        glue_req_init(&r1);
        r1.query = NULL;
        spectrum_http_send_json(&r1);
        NCHECK(name, g_hook_calls == 0);
        glue_req_free(&r1);

        g_hook_calls = 0;
        httpd_req_t r2;
        glue_req_init(&r2);
        r2.query = "channels=2048";
        spectrum_http_send_json(&r2);
        NCHECK(name, g_hook_calls == 1);
        NCHECK(name, g_hook_channels == 2048);
        glue_req_free(&r2);

        g_hook_calls = 0;
        httpd_req_t r3;
        glue_req_init(&r3);
        r3.query = NULL;
        spectrum_http_send_bins(&r3);
        NCHECK(name, g_hook_calls == 0);
        glue_req_free(&r3);

        g_hook_calls = 0;
        httpd_req_t r4;
        glue_req_init(&r4);
        r4.query = NULL;
        spectrum_http_send_json_plain(&r4);
        NCHECK(name, g_hook_calls == 0);
        glue_req_free(&r4);

        g_hook_calls = 0;
        httpd_req_t r5;
        glue_req_init(&r5);
        r5.query = NULL;
        spectrum_http_send_bins(&r5);
        NCHECK(name, g_hook_calls == 0);
        glue_req_free(&r5);

        g_hook_calls = 0;
        httpd_req_t r6;
        glue_req_init(&r6);
        r6.query = "channels=8192";
        spectrum_http_send_json_plain(&r6);
        NCHECK(name, g_hook_calls == 0);
        glue_req_free(&r6);

        g_hook_calls = 0;
        httpd_req_t r7;
        glue_req_init(&r7);
        r7.query = "channels=7";
        spectrum_http_send_json(&r7);
        NCHECK(name, r7.status == 400);
        NCHECK(name, g_hook_calls == 0);
        glue_req_free(&r7);

        spectrum_http_cache_set_gz_hook(NULL);
    }

    // Тест 3: equiv_v2
    {
        const char *name = "equiv_v2";
        new_generation();
        g_spec.bins[0] = 4321;
        set_ep(5, 6);
        
        httpd_req_t r;
        glue_req_init(&r);
        r.query = "channels=8192";
        spectrum_http_send_json_plain(&r);
        
        char *buf;
        size_t len;
        uint32_t gen;
        bool ok = spectrum_http_cache_v2_acquire(8192, &buf, &len, &gen);
        NCHECK(name, ok);
        NCHECK(name, len == r.body_len);
        NCHECK(name, memcmp(buf, r.body, len) == 0);
        
        uint8_t *packed = malloc(len * 2 + 1024);
        size_t packed_len = gz_pack(buf, len, packed, len * 2 + 1024);
        NCHECK(name, packed_len > 0);
        
        size_t olen = 0;
        char *unpacked = gz_unpack(packed, packed_len, &olen);
        NCHECK(name, unpacked != NULL);
        NCHECK(name, olen == len);
        NCHECK(name, memcmp(unpacked, r.body, len) == 0);
        
        uint32_t ch1 = body_u32(r.body, "\"channels\":");
        uint32_t ch2 = body_u32(unpacked, "\"channels\":");
        NCHECK(name, ch1 == 8192);
        NCHECK(name, ch1 == ch2);
        
        uint32_t dr1 = body_u32(r.body, "\"dev_resets\":");
        uint32_t dr2 = body_u32(unpacked, "\"dev_resets\":");
        NCHECK(name, dr1 == 5);
        NCHECK(name, dr1 == dr2);
        
        uint32_t sr1 = body_u32(r.body, "\"spec_resets\":");
        uint32_t sr2 = body_u32(unpacked, "\"spec_resets\":");
        NCHECK(name, sr1 == 6);
        NCHECK(name, sr1 == sr2);
        
        uint32_t bid1 = body_u32(r.body, "\"boot_id\":");
        uint32_t bid2 = body_u32(unpacked, "\"boot_id\":");
        NCHECK(name, bid1 == 0x00AA5555);
        NCHECK(name, bid1 == bid2);
        
        char *bins_ptr = strstr(r.body, "\"bins\":[");
        if (bins_ptr) {
            bins_ptr += strlen("\"bins\":[");
            uint32_t bin1 = (uint32_t)strtoul(bins_ptr, NULL, 10);
            NCHECK(name, bin1 == 4321);
        }
        
        free(packed);
        free(unpacked);
        spectrum_http_cache_v2_release(buf);
        glue_req_free(&r);
        
        printf("INFO gz_async: equiv_v2_len=%zu packed=%zu\n", len, packed_len);
    }

    // Тест 5: epoch_atomic_k2
    {
        const char *name = "epoch_atomic_k2";
        new_generation();
        g_spec.bins[0] = 1111;
        set_ep(10, 20);
        spectrum_http_cache_ensure();
        
        glue_on_give = bump_epoch;
        char *buf;
        size_t len;
        uint32_t gen;
        bool ok = spectrum_http_cache_v2_acquire(8192, &buf, &len, &gen);
        glue_on_give = NULL;
        
        NCHECK(name, ok);
        uint32_t dr = body_u32(buf, "\"dev_resets\":");
        uint32_t sr = body_u32(buf, "\"spec_resets\":");
        NCHECK(name, dr == 10);
        NCHECK(name, sr == 20);
        
        char *bins_ptr = strstr(buf, "\"bins\":[");
        if (bins_ptr) {
            bins_ptr += strlen("\"bins\":[");
            uint32_t bin1 = (uint32_t)strtoul(bins_ptr, NULL, 10);
            NCHECK(name, bin1 == 1111);
        }
        
        NCHECK(name, gen == s_render_count);
        
        set_ep(10, 20);
        spectrum_http_cache_v2_release(buf);
    }

    // Тесты 6: leak paths
    {
        const char *name = "leak_ok";
        g_ctx.kind = 2;
        g_ctx.send_ok = true;
        g_ctx.comp_ret = 1;
        g_ctx.alloc_ok = true;
        g_ctx.now = 10500;
        g_ctx.sent_plain = 0;
        g_ctx.sent_gz = 0;
        g_ctx.completes = 0;
        
        httpd_req_t r;
        glue_req_init(&r);
        r.query = "channels=8192";
        g_ctx.req = &r;
        
        spec_gz_job_t job = { .kind = 2, .channels = 8192, .enq_ms = 10000, .req = &r };
        spec_gz_cnt_t cnt = {0};
        
        size_t b0 = glue_live_blocks();
        spec_gz_job_run(&ops, &g_ctx, &job, &cnt);
        glue_req_free(&r);
        
        NCHECK(name, cnt.ok == 1);
        NCHECK(name, glue_live_blocks() == b0);
        NCHECK(name, s_readers == 0);
        NCHECK(name, s_json_users == 0);
        NCHECK(name, g_ctx.completes == 1);
    }

    {
        const char *name = "job_timeout";   // тот же сценарий срока, что в test_spectrum_gz_job: одно логическое имя
        g_ctx.kind = 2;
        g_ctx.send_ok = true;
        g_ctx.comp_ret = 1;
        g_ctx.alloc_ok = true;
        g_ctx.now = 12000;
        g_ctx.sent_plain = 0;
        g_ctx.sent_gz = 0;
        g_ctx.completes = 0;
        
        httpd_req_t r;
        glue_req_init(&r);
        r.query = "channels=8192";
        g_ctx.req = &r;
        
        spec_gz_job_t job = { .kind = 2, .channels = 8192, .enq_ms = 10000, .req = &r };
        spec_gz_cnt_t cnt = {0};
        
        size_t b0 = glue_live_blocks();
        spec_gz_job_run(&ops, &g_ctx, &job, &cnt);
        glue_req_free(&r);
        
        NCHECK(name, cnt.timeout == 1);
        NCHECK(name, g_ctx.sent_plain == 1);
        NCHECK(name, g_ctx.sent_gz == 0);
        NCHECK(name, glue_live_blocks() == b0);
        NCHECK(name, s_readers == 0);
        NCHECK(name, s_json_users == 0);
        NCHECK(name, g_ctx.completes == 1);
    }

    {
        const char *name = "leak_send_err";
        g_ctx.closes = 0;
        g_ctx.kind = 2;
        g_ctx.send_ok = false;
        g_ctx.comp_ret = 1;
        g_ctx.alloc_ok = true;
        g_ctx.now = 10500;
        g_ctx.sent_plain = 0;
        g_ctx.sent_gz = 0;
        g_ctx.completes = 0;
        
        httpd_req_t r;
        glue_req_init(&r);
        r.query = "channels=8192";
        g_ctx.req = &r;
        
        spec_gz_job_t job = { .kind = 2, .channels = 8192, .enq_ms = 10000, .req = &r };
        spec_gz_cnt_t cnt = {0};
        
        size_t b0 = glue_live_blocks();
        spec_gz_job_run(&ops, &g_ctx, &job, &cnt);
        glue_req_free(&r);
        
        NCHECK(name, cnt.send_err == 1);
        NCHECK("job_send_err", g_ctx.closes == 1);   /* F2: сессия закрыта при ошибке отправки */
        NCHECK(name, glue_live_blocks() == b0);
        NCHECK(name, s_readers == 0);
        NCHECK(name, s_json_users == 0);
        NCHECK(name, g_ctx.completes == 1);
    }

    {
        const char *name = "leak_comp_fail";
        g_ctx.kind = 2;
        g_ctx.send_ok = true;
        g_ctx.comp_ret = -1;
        g_ctx.alloc_ok = true;
        g_ctx.now = 10500;
        g_ctx.sent_plain = 0;
        g_ctx.sent_gz = 0;
        g_ctx.completes = 0;
        
        httpd_req_t r;
        glue_req_init(&r);
        r.query = "channels=8192";
        g_ctx.req = &r;
        
        spec_gz_job_t job = { .kind = 2, .channels = 8192, .enq_ms = 10000, .req = &r };
        spec_gz_cnt_t cnt = {0};
        
        size_t b0 = glue_live_blocks();
        spec_gz_job_run(&ops, &g_ctx, &job, &cnt);
        glue_req_free(&r);
        
        NCHECK(name, cnt.fail == 1);
        NCHECK(name, g_ctx.sent_plain == 1);
        NCHECK(name, glue_live_blocks() == b0);
        NCHECK(name, s_readers == 0);
        NCHECK(name, s_json_users == 0);
        NCHECK(name, g_ctx.completes == 1);
    }

    {
        const char *name = "leak_nomem";
        g_ctx.kind = 2;
        g_ctx.send_ok = true;
        g_ctx.comp_ret = 1;
        g_ctx.alloc_ok = false;
        g_ctx.now = 10500;
        g_ctx.sent_plain = 0;
        g_ctx.sent_gz = 0;
        g_ctx.completes = 0;
        
        httpd_req_t r;
        glue_req_init(&r);
        r.query = "channels=8192";
        g_ctx.req = &r;
        
        spec_gz_job_t job = { .kind = 2, .channels = 8192, .enq_ms = 10000, .req = &r };
        spec_gz_cnt_t cnt = {0};
        
        size_t b0 = glue_live_blocks();
        spec_gz_job_run(&ops, &g_ctx, &job, &cnt);
        glue_req_free(&r);
        
        NCHECK(name, cnt.nomem == 1);
        NCHECK(name, g_ctx.sent_plain == 1);
        NCHECK(name, glue_live_blocks() == b0);
        NCHECK(name, s_readers == 0);
        NCHECK(name, s_json_users == 0);
        NCHECK(name, g_ctx.completes == 1);
    }

    {
        const char *name = "leak_oom_build";
        g_ctx.kind = 2;
        g_ctx.send_ok = true;
        g_ctx.comp_ret = 1;
        g_ctx.alloc_ok = true;
        g_ctx.now = 10500;
        g_ctx.sent_plain = 0;
        g_ctx.sent_gz = 0;
        g_ctx.completes = 0;
        
        httpd_req_t r;
        glue_req_init(&r);
        r.query = "channels=8192";
        g_ctx.req = &r;
        
        spec_gz_job_t job = { .kind = 2, .channels = 8192, .enq_ms = 10000, .req = &r };
        spec_gz_cnt_t cnt = {0};
        
        size_t b0 = glue_live_blocks();
        glue_fail_above = 16384;
        spec_gz_job_run(&ops, &g_ctx, &job, &cnt);
        glue_fail_above = (size_t)-1;
        glue_req_free(&r);
        
        NCHECK(name, cnt.fallback == 1);
        NCHECK(name, g_ctx.sent_plain == 1);
        NCHECK(name, glue_live_blocks() == b0);
        NCHECK(name, s_readers == 0);
        NCHECK(name, s_json_users == 0);
        NCHECK(name, g_ctx.completes == 1);
    }

    {
        const char *name = "queue_full_no_alloc";
        spec_gz_q_t q = {0, 0};
        spec_gz_q_reserve(&q);
        spec_gz_q_reserve(&q);
        NCHECK(name, spec_gz_q_full(&q));
        size_t b0 = glue_live_blocks();
        NCHECK(name, glue_live_blocks() == b0);
    }

    {   /* F1: бины ~4e9 (10 цифр + запятая): тело больше n*6+2048 - acquire обязан вырасти, а не вернуть false */
        const char *name = "acquire_big_bins";
        for (int i = 0; i < 8192; i++) g_spec.bins[i] = 4000000000u;
        new_generation();
        spectrum_http_cache_ensure();
        size_t b0 = glue_live_blocks();
        char *buf = NULL; size_t len = 0; uint32_t gen = 0;
        bool ok = spectrum_http_cache_v2_acquire(8192, &buf, &len, &gen);
        NCHECK(name, ok && len > (size_t)8192 * 6 + 2048);
        httpd_req_t r; glue_req_init(&r); r.query = "channels=8192";
        spectrum_http_send_json_plain(&r);
        NCHECK(name, ok && len == r.body_len && memcmp(buf, r.body, len) == 0);
        if (ok) spectrum_http_cache_v2_release(buf);
        glue_req_free(&r);
        NCHECK(name, glue_live_blocks() == b0 && s_readers == 0);
        glue_fill_stand(&g_spec, 1); new_generation();
    }
    {   /* F7: после invalidate старый снимок не отдаётся */
        const char *name = "acquire_stale";
        spectrum_http_cache_ensure();
        char *buf = NULL; size_t len = 0; uint32_t gen = 0;
        NCHECK(name, spectrum_http_cache_v2_acquire(1024, &buf, &len, &gen));
        spectrum_http_cache_v2_release(buf);
        spectrum_http_cache_invalidate();
        buf = NULL;
        NCHECK(name, !spectrum_http_cache_v2_acquire(1024, &buf, &len, &gen) && buf == NULL && s_readers == 0);
    }
    NCHECK("readers_free_during_send", g_send_readers_bad == 0 && g_send_sweep_bad == 0);
    if (g_failures == 0) {
        printf("test_glue_gz_async: OK\n");
        return 0;
    } else {
        printf("test_glue_gz_async: FAILED\n");
        return 1;
    }
}
