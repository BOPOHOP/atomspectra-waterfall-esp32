#pragma once
// P3 №7 (verify-awf5-github-ota-2026-09-27.md:260, sweep-B задача 5): решение
// "что делать после обрыва скачивания образа с GitHub" -- чистая функция,
// host-тестируема без сети/httpd. Симметрия с D4: конечное число попыток.
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    OTA_GH_DL_RESUME_RANGE = 0,  // reopen вернул 206 -- сервер поддержал Range
    OTA_GH_DL_RESTART_ZERO,      // reopen вернул 200 -- Range не поддержан
    OTA_GH_DL_GIVE_UP,           // лимит попыток исчерпан либо иной статус
    OTA_GH_DL_REOPEN_LATER,      // сбой сети (-1): подождать и переоткрыть, чтение не продолжать
} ota_gh_dl_retry_action_t;

// attempt -- номер уже сделанной попытки возобновления (1-based, ПОСЛЕ
// инкремента при данном обрыве). reopen_status -- код HTTP-ответа на повторный
// esp_http_client_open() с заголовком Range (-1, если сам reopen не удался).
static inline ota_gh_dl_retry_action_t ota_gh_dl_decide(int reopen_status, int attempt,
                                                          int max_attempts)
{
    if (attempt > max_attempts) return OTA_GH_DL_GIVE_UP;
    if (reopen_status == 206) return OTA_GH_DL_RESUME_RANGE;
    if (reopen_status == 200) return OTA_GH_DL_RESTART_ZERO;
    // -1 — только сбой открытия (сеть); соединение закрыто, продолжать чтение нельзя;
    // ответ сервера (4xx/5xx) приходит сюда своим кодом (ota_gh_dl_reopen_status) и даёт GIVE_UP.
    if (reopen_status == -1) return OTA_GH_DL_REOPEN_LATER;
    return OTA_GH_DL_GIVE_UP;
}

// М4 сценарий 2: обрыв чтения пришёлся РОВНО на конец образа (все байты уже
// приняты) — Range: bytes=received- на такой границе корректно получит 416, это
// НЕ отказ. clen<=0 — длина неизвестна заранее, "уже всё принято" не определить.
static inline bool ota_gh_dl_is_already_complete(uint32_t received, int64_t clen)
{
    return clen > 0 && (int64_t)received >= clen;
}

// fail_status — HTTP-код ответа, на котором открытие остановилось (4xx/5xx),
// 0 — ответа не было (сеть).
static inline int ota_gh_dl_reopen_status(bool open_ok, int status_code, int fail_status)
{
    if (open_ok) return status_code;
    return fail_status > 0 ? fail_status : -1;
}

// Экспоненциальная задержка с ограничением.
static inline uint32_t ota_gh_dl_backoff_ms(int attempt)
{
    if (attempt < 1) return 2000;
    if (attempt > 5) return 30000;
    uint32_t delay = 2000U << (attempt - 1);
    return delay > 30000 ? 30000 : delay;
}

typedef int (*ota_gh_dl_reopen_fn)(void *ctx);
typedef void (*ota_gh_dl_wait_fn)(void *ctx, int attempt);

// проводка решения после переоткрытия — чистая, reopen/wait подставляет прошивка
// (ota_github_client.c) и host-тест; никогда не возвращает REOPEN_LATER;
// attempt — общий бюджет всей загрузки.
static inline ota_gh_dl_retry_action_t ota_gh_dl_reopen_until_decided(
    ota_gh_dl_reopen_fn reopen, ota_gh_dl_wait_fn wait, void *ctx,
    int *attempt, int max_attempts)
{
    for (;;) {
        (*attempt)++;
        if (*attempt > max_attempts) return OTA_GH_DL_GIVE_UP;
        ota_gh_dl_retry_action_t act = ota_gh_dl_decide(reopen(ctx), *attempt, max_attempts);
        if (act != OTA_GH_DL_REOPEN_LATER) return act;
        if (*attempt >= max_attempts) return OTA_GH_DL_GIVE_UP;
        wait(ctx, *attempt);
    }
}

