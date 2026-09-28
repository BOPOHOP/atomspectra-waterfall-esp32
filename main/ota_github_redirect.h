#pragma once
// AWF-5 fix: решение "продолжать редирект или остановиться" -- чистая
// функция, host-тестируема без esp_http_client. Тело -- см. ниже в файле.
#include <stdbool.h>

typedef enum {
    OTA_HTTP_REDIRECT_STOP_OK,
    OTA_HTTP_REDIRECT_CONTINUE,
    OTA_HTTP_REDIRECT_STOP_FAIL
} ota_http_redirect_action_t;

static inline bool ota_http_status_is_redirect(int status)
{
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

// hop -- номер уже совершённого редиректа (0 при первом ответе), max_hops --
// потолок (main/ota_github_client.c держит его равным max_redirection_count).
static inline ota_http_redirect_action_t ota_http_redirect_decide(int status, int hop, int max_hops)
{
    if (status >= 200 && status < 300) return OTA_HTTP_REDIRECT_STOP_OK;
    if (ota_http_status_is_redirect(status) && hop < max_hops) return OTA_HTTP_REDIRECT_CONTINUE;
    return OTA_HTTP_REDIRECT_STOP_FAIL;
}
