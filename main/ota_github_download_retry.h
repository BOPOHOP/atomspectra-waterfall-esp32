#pragma once
// P3 №7 (verify-awf5-github-ota-2026-09-27.md:260, sweep-B задача 5): решение
// "что делать после обрыва скачивания образа с GitHub" -- чистая функция,
// host-тестируема без сети/httpd. Симметрия с D4: конечное число попыток.
#include <stdbool.h>

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
    return OTA_GH_DL_GIVE_UP;
}
