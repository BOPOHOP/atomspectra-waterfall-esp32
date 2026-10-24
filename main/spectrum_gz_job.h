#pragma once
#include "spectrum_gz_plan.h"

// Тип задания для сжатия
typedef struct {
    int kind;
    int channels;
    uint32_t enq_ms;
    void *req;
} spec_gz_job_t;

// Таблица обратных вызовов для выполнения задания
typedef struct {
    uint32_t (*now_ms)(void *c);
    uint8_t  (*level)(void *c);
    bool     (*acquire)(void *c, const spec_gz_job_t *j, const void **body, size_t *len);
    void     (*release)(void *c, const spec_gz_job_t *j);
    uint8_t *(*out_alloc)(void *c, size_t cap);
    void     (*out_free)(void *c, uint8_t *out);
    int      (*compress)(void *c, uint8_t level, const void *body, size_t len, uint8_t *out, size_t cap, size_t *out_len);
    bool     (*send_gz)(void *c, const spec_gz_job_t *j, const uint8_t *out, size_t out_len);
    bool     (*send_plain)(void *c, const spec_gz_job_t *j);      // несжатый ответ; false = ошибка отправки (сессию закрыть)
    void     (*close_session)(void *c, const spec_gz_job_t *j);   // httpd_sess_trigger_close, ДО complete
    void     (*complete)(void *c, const spec_gz_job_t *j);
} spec_gz_ops_t;

// Счетчики результатов
typedef struct {
    uint32_t ok, fallback, timeout, nomem, fail, send_err;
} spec_gz_cnt_t;

// Несжатый ответ из задачи + завершение; ошибка отправки -> закрыть сессию ДО complete (F2)
static inline void spec_gz_job_plain(const spec_gz_ops_t *o, void *c, const spec_gz_job_t *j, spec_gz_cnt_t *n) {
    if (!o->send_plain(c, j)) {
        n->send_err++;
        o->close_session(c, j);
    }
    o->complete(c, j);
}

// Выполнение задания сжатия
static inline void spec_gz_job_run(const spec_gz_ops_t *o, void *c, const spec_gz_job_t *j, spec_gz_cnt_t *n) {
    // 1. Проверка дедлайна
    if (spec_gz_expired(o->now_ms(c), j->enq_ms)) {
        n->timeout++;
        n->fallback++;
        spec_gz_job_plain(o, c, j, n);
        return;
    }

    // 2. Проверка уровня сжатия
    uint8_t lvl = o->level(c);
    if (lvl == 0) {
        n->fallback++;
        spec_gz_job_plain(o, c, j, n);
        return;
    }

    // 3. Получение тела запроса
    const void *body;
    size_t len;
    if (!o->acquire(c, j, &body, &len)) {
        n->fallback++;
        spec_gz_job_plain(o, c, j, n);
        return;
    }

    // 4. Проверка целесообразности сжатия
    if (!spec_gz_worth(len)) {
        o->release(c, j);
        spec_gz_job_plain(o, c, j, n);
        return;
    }

    // 5. Выделение буфера для результата
    size_t cap = spec_gz_slot_cap(len);
    uint8_t *out = o->out_alloc(c, cap);
    if (!out) {
        n->nomem++;
        n->fallback++;
        o->release(c, j);
        spec_gz_job_plain(o, c, j, n);
        return;
    }

    // 6. Сжатие
    size_t out_len;
    int r = o->compress(c, lvl, body, len, out, cap, &out_len);
    if (r < 0) {
        n->fail++;
        n->fallback++;
        o->out_free(c, out);
        o->release(c, j);
        spec_gz_job_plain(o, c, j, n);
        return;
    }
    if (r == 0) {
        o->out_free(c, out);
        o->release(c, j);
        spec_gz_job_plain(o, c, j, n);
        return;
    }

    // 7. Отправка сжатого результата
    o->release(c, j);
    bool sent = o->send_gz(c, j, out, out_len);
    o->out_free(c, out);
    if (sent) {
        n->ok++;
    } else {
        n->send_err++;
        o->close_session(c, j);   // F2: как при возврате ошибки из синхронного обработчика
    }
    o->complete(c, j);
}
