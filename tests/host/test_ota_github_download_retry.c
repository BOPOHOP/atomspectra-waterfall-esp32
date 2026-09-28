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
    CHECK(ota_gh_dl_decide(-1, 1, DL_MAX) == OTA_GH_DL_GIVE_UP);   // reopen сам не удался
    CHECK(ota_gh_dl_decide(404, 1, DL_MAX) == OTA_GH_DL_GIVE_UP);
    CHECK(ota_gh_dl_decide(500, 1, DL_MAX) == OTA_GH_DL_GIVE_UP);
}

void ota_gh_dl_retry_suite(void)
{
    dl_206_resumes();
    dl_200_restarts();
    dl_attempt_exceeds_max_gives_up();
    dl_error_or_unexpected_status_gives_up();
}
