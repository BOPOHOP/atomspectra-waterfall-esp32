#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "../../main/spectrum_gz_job.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int g_fail;
#define NCHECK(name, cond) do { if (!(cond)) { printf("FAIL %s %s:%d: %s\n", name, __FILE__, __LINE__, #cond); g_fail++; } } while (0)

// Фейковый контекст для тестирования
typedef struct {
    uint32_t now;
    uint8_t level;
    bool acq_ok;
    size_t body_len;
    bool alloc_ok;
    int comp_ret;
    bool send_ok;
    int acquired, released, allocs, frees, completes, plain, gz_sent, compress_calls;
    int released_at_send;
    bool plain_ok;
    int closes, close_late;
} fx_t;

// Статический буфер для тела запроса
static uint8_t g_body[4096];

// Фейковые операции
static uint32_t fx_now_ms(void *c) { return ((fx_t*)c)->now; }
static uint8_t fx_level(void *c) { return ((fx_t*)c)->level; }

static bool fx_acquire(void *c, const spec_gz_job_t *j, const void **body, size_t *len) {
    fx_t *f = (fx_t*)c;
    if (!f->acq_ok) return false;
    f->acquired++;
    *body = g_body;
    *len = f->body_len;
    return true;
}

static void fx_release(void *c, const spec_gz_job_t *j) {
    ((fx_t*)c)->released++;
}

static uint8_t *fx_out_alloc(void *c, size_t cap) {
    fx_t *f = (fx_t*)c;
    if (!f->alloc_ok) return NULL;
    f->allocs++;
    return (uint8_t*)malloc(cap);
}

static void fx_out_free(void *c, uint8_t *out) {
    ((fx_t*)c)->frees++;
    free(out);
}

static int fx_compress(void *c, uint8_t level, const void *body, size_t len, uint8_t *out, size_t cap, size_t *out_len) {
    fx_t *f = (fx_t*)c;
    f->compress_calls++;
    if (f->comp_ret > 0) {
        *out_len = 100;
    }
    return f->comp_ret;
}

static bool fx_send_gz(void *c, const spec_gz_job_t *j, const uint8_t *out, size_t out_len) {
    fx_t *f = (fx_t*)c;
    f->gz_sent++;
    f->released_at_send = f->released;
    return f->send_ok;
}

static bool fx_send_plain(void *c, const spec_gz_job_t *j) {
    ((fx_t*)c)->plain++;
    return ((fx_t*)c)->plain_ok;
}

static void fx_close(void *c, const spec_gz_job_t *j) {
    fx_t *f = (fx_t*)c;
    f->closes++;
    if (f->completes != 0) f->close_late++;   /* F2: close строго до complete */
}

static void fx_complete(void *c, const spec_gz_job_t *j) {
    ((fx_t*)c)->completes++;
}

// Инициализация контекста по умолчанию
static void fx_default(fx_t *f) {
    f->now = 10000;
    f->level = 6;
    f->acq_ok = true;
    f->body_len = 20000;
    f->alloc_ok = true;
    f->comp_ret = 1;
    f->send_ok = true;
    f->acquired = 0;
    f->released = 0;
    f->allocs = 0;
    f->frees = 0;
    f->completes = 0;
    f->plain = 0;
    f->gz_sent = 0;
    f->compress_calls = 0;
    f->released_at_send = 0;
    f->plain_ok = true;
    f->closes = 0;
    f->close_late = 0;
}

// Проверка баланса ресурсов
static bool balanced(const fx_t *f) {
    return (f->acquired == f->released && f->allocs == f->frees && f->completes == 1);
}

// Запуск сценария
static void run(fx_t *f, uint32_t enq, spec_gz_cnt_t *n) {
    memset(n, 0, sizeof(*n));
    spec_gz_job_t job = {
        .kind = 2,
        .channels = 8192,
        .enq_ms = enq,
        .req = NULL
    };
    spec_gz_ops_t ops = {
        .now_ms = fx_now_ms,
        .level = fx_level,
        .acquire = fx_acquire,
        .release = fx_release,
        .out_alloc = fx_out_alloc,
        .out_free = fx_out_free,
        .compress = fx_compress,
        .send_gz = fx_send_gz,
        .send_plain = fx_send_plain,
        .close_session = fx_close,
        .complete = fx_complete
    };
    spec_gz_job_run(&ops, f, &job, n);
}

int main(void) {
    fx_t f;
    spec_gz_cnt_t n;

    // Сценарий: успешное сжатие
    fx_default(&f);
    run(&f, 9000, &n);
    NCHECK("job_ok", n.ok == 1);
    NCHECK("job_ok", f.gz_sent == 1);
    NCHECK("job_ok", f.plain == 0);
    NCHECK("job_ok", f.closes == 0);
    NCHECK("job_ok", n.fallback == 0);
    NCHECK("job_ok", f.compress_calls == 1);
    NCHECK("job_ok", balanced(&f));
    NCHECK("job_ok", f.released_at_send == 1);

    // Сценарий: таймаут
    fx_default(&f);
    run(&f, 8000, &n);
    NCHECK("job_timeout", n.timeout == 1);
    NCHECK("job_timeout", n.fallback == 1);
    NCHECK("job_timeout", f.plain == 1);
    NCHECK("job_timeout", f.gz_sent == 0);
    NCHECK("job_timeout", f.compress_calls == 0);
    NCHECK("job_timeout", f.acquired == 0);
    NCHECK("job_timeout", balanced(&f));

    // Сценарий: граница 1500 мс
    fx_default(&f);
    run(&f, 8500, &n);
    NCHECK("job_edge1500", n.ok == 1);
    NCHECK("job_edge1500", f.plain == 0);
    NCHECK("job_edge1500", balanced(&f));

    // Сценарий: уровень 0
    fx_default(&f);
    f.level = 0;
    run(&f, 9000, &n);
    NCHECK("job_level0", n.fallback == 1);
    NCHECK("job_level0", f.plain == 1);
    NCHECK("job_level0", f.compress_calls == 0);
    NCHECK("job_level0", f.acquired == 0);
    NCHECK("job_level0", balanced(&f));

    // Сценарий: ошибка получения тела
    fx_default(&f);
    f.acq_ok = false;
    run(&f, 9000, &n);
    NCHECK("job_acq_fail", n.fallback == 1);
    NCHECK("job_acq_fail", f.plain == 1);
    NCHECK("job_acq_fail", f.compress_calls == 0);
    NCHECK("job_acq_fail", balanced(&f));

    // Сценарий: маленькое тело
    fx_default(&f);
    f.body_len = 500;
    run(&f, 9000, &n);
    NCHECK("job_tiny", f.plain == 1);
    NCHECK("job_tiny", n.fallback == 0);
    NCHECK("job_tiny", f.compress_calls == 0);
    NCHECK("job_tiny", f.allocs == 0);
    NCHECK("job_tiny", balanced(&f));

    // Сценарий: нехватка памяти
    fx_default(&f);
    f.alloc_ok = false;
    run(&f, 9000, &n);
    NCHECK("job_nomem", n.nomem == 1);
    NCHECK("job_nomem", n.fallback == 1);
    NCHECK("job_nomem", f.plain == 1);
    NCHECK("job_nomem", f.released == 1);
    NCHECK("job_nomem", balanced(&f));

    // Сценарий: ошибка сжатия
    fx_default(&f);
    f.comp_ret = -1;
    run(&f, 9000, &n);
    NCHECK("job_comp_fail", n.fail == 1);
    NCHECK("job_comp_fail", n.fallback == 1);
    NCHECK("job_comp_fail", f.plain == 1);
    NCHECK("job_comp_fail", f.frees == 1);
    NCHECK("job_comp_fail", balanced(&f));

    // Сценарий: несжимаемые данные
    fx_default(&f);
    f.comp_ret = 0;
    run(&f, 9000, &n);
    NCHECK("job_incompressible", f.plain == 1);
    NCHECK("job_incompressible", n.fallback == 0);
    NCHECK("job_incompressible", f.frees == 1);
    NCHECK("job_incompressible", balanced(&f));

    // Сценарий: ошибка отправки
    fx_default(&f);
    f.send_ok = false;
    run(&f, 9000, &n);
    NCHECK("job_send_err", n.send_err == 1);
    NCHECK("job_send_err", n.ok == 0);
    NCHECK("job_send_err", f.closes == 1 && f.close_late == 0);
    NCHECK("job_send_err", f.plain == 0);
    NCHECK("job_send_err", f.frees == 1);
    NCHECK("job_send_err", balanced(&f));

    /* F2: ошибка отправки несжатого ответа из задачи - код не теряется, сессия закрывается до complete */
    fx_default(&f);
    f.level = 0;
    f.plain_ok = false;
    run(&f, 9000, &n);
    NCHECK("job_plain_err", n.send_err == 1);
    NCHECK("job_plain_err", f.closes == 1 && f.close_late == 0);
    NCHECK("job_plain_err", balanced(&f));

    if (g_fail == 0) {
        printf("test_spectrum_gz_job: OK\n");
        return 0;
    } else {
        printf("test_spectrum_gz_job: FAILED\n");
        return 1;
    }
}
