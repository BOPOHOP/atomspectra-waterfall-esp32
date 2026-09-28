#include "ota_github_download_retry.h"
#include "test_util.h"

#define DL_MAX 5

static void dl_206_resumes(void)
{
    CHECK(ota_gh_dl_decide(206, 1, DL_MAX) == OTA_GH_DL_RESUME_RANGE);
    CHECK(ota_gh_dl_decide(206, DL_MAX, DL_MAX) == OTA_GH_DL_RESUME_RANGE);
}

static void dl_200_restarts(void)
{
    CHECK(ota_gh_dl_decide(200, 1, DL_MAX) == OTA_GH_DL_RESTART_ZERO);
}

static void dl_attempt_exceeds_max_gives_up(void)
{
    CHECK(ota_gh_dl_decide(206, DL_MAX + 1, DL_MAX) == OTA_GH_DL_GIVE_UP);
    CHECK(ota_gh_dl_decide(200, DL_MAX + 1, DL_MAX) == OTA_GH_DL_GIVE_UP);
}

static void dl_error_or_unexpected_status_gives_up(void)
{
    // М4 сценарий 1: reopen_status==-1 (сам esp_http_client_open() не удался —
    // обрыв Wi-Fi, самый частый случай на практике) ТЕПЕРЬ retry, не немедленный
    // GIVE_UP — бюджет в DL_MAX попыток раньше для него не работал вовсе.
    CHECK(ota_gh_dl_decide(-1, 1, DL_MAX) == OTA_GH_DL_RESUME_RANGE);
    CHECK(ota_gh_dl_decide(-1, DL_MAX, DL_MAX) == OTA_GH_DL_RESUME_RANGE);
    CHECK(ota_gh_dl_decide(-1, DL_MAX + 1, DL_MAX) == OTA_GH_DL_GIVE_UP);  // бюджет исчерпан
    // Подтверждённая ошибка сервера — сдаёмся сразу, бюджет на неё не тратим.
    CHECK(ota_gh_dl_decide(404, 1, DL_MAX) == OTA_GH_DL_GIVE_UP);
    CHECK(ota_gh_dl_decide(500, 1, DL_MAX) == OTA_GH_DL_GIVE_UP);
}

// М4 сценарий 2: обрыв ровно на границе конца образа — не отказ, а завершение.
static void dl_already_complete_at_eof(void)
{
    CHECK(ota_gh_dl_is_already_complete(1000, 1000) == true);   // всё принято
    CHECK(ota_gh_dl_is_already_complete(999, 1000) == false);   // не хватает 1 байта
    CHECK(ota_gh_dl_is_already_complete(1000, 0) == false);     // clen неизвестна — не наш случай
    CHECK(ota_gh_dl_is_already_complete(0, -1) == false);
}

void ota_gh_dl_retry_suite(void)
{
    dl_206_resumes();
    dl_200_restarts();
    dl_attempt_exceeds_max_gives_up();
    dl_error_or_unexpected_status_gives_up();
    dl_already_complete_at_eof();
}
