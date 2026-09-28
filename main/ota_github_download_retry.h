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
    // М4 сценарий 1: reopen_status==-1 — сам esp_http_client_open() не удался
    // (обрыв сети), самый частый случай на практике. Раньше это безусловно
    // давало GIVE_UP на 1-й попытке, бюджет в 5 попыток не работал для него
    // вовсе. Подтверждённая ошибка сервера (404/500…) — сдаёмся сразу как раньше.
    if (reopen_status == -1) return OTA_GH_DL_RESUME_RANGE;
    return OTA_GH_DL_GIVE_UP;
}

// М4 сценарий 2: обрыв чтения пришёлся РОВНО на конец образа (все байты уже
// приняты) — Range: bytes=received- на такой границе корректно получит 416, это
// НЕ отказ. clen<=0 — длина неизвестна заранее, "уже всё принято" не определить.
static inline bool ota_gh_dl_is_already_complete(uint32_t received, int64_t clen)
{
    return clen > 0 && (int64_t)received >= clen;
}
