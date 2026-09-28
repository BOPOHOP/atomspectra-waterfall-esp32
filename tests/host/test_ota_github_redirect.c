#include "ota_github_redirect.h"
#include "test_util.h"

static void status_2xx_stops_ok(void)
{
    CHECK(ota_http_redirect_decide(200, 0, 10) == OTA_HTTP_REDIRECT_STOP_OK);
    CHECK(ota_http_redirect_decide(204, 3, 10) == OTA_HTTP_REDIRECT_STOP_OK);
    CHECK(ota_http_redirect_decide(299, 0, 10) == OTA_HTTP_REDIRECT_STOP_OK);
}

static void github_302_continues_when_hops_left(void)
{
    // живой случай: github.com/.../releases/download/... -> 302 -> release-assets.githubusercontent.com
    CHECK(ota_http_redirect_decide(302, 0, 10) == OTA_HTTP_REDIRECT_CONTINUE);
}

static void all_redirect_codes_continue(void)
{
    int codes[] = {301, 302, 303, 307, 308};
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++)
        CHECK(ota_http_redirect_decide(codes[i], 0, 10) == OTA_HTTP_REDIRECT_CONTINUE);
}

static void redirect_exhausted_stops_fail(void)
{
    CHECK(ota_http_redirect_decide(302, 10, 10) == OTA_HTTP_REDIRECT_STOP_FAIL);
    CHECK(ota_http_redirect_decide(302, 11, 10) == OTA_HTTP_REDIRECT_STOP_FAIL);
}

static void non_redirect_error_stops_fail(void)
{
    CHECK(ota_http_redirect_decide(404, 0, 10) == OTA_HTTP_REDIRECT_STOP_FAIL);
    CHECK(ota_http_redirect_decide(500, 0, 10) == OTA_HTTP_REDIRECT_STOP_FAIL);
    CHECK(ota_http_redirect_decide(0, 0, 10) == OTA_HTTP_REDIRECT_STOP_FAIL);
}

static void status_is_redirect_helper(void)
{
    CHECK(ota_http_status_is_redirect(302));
    CHECK(!ota_http_status_is_redirect(200));
    CHECK(!ota_http_status_is_redirect(404));
}

void ota_gh_redirect_suite(void)
{
    status_2xx_stops_ok();
    github_302_continues_when_hops_left();
    all_redirect_codes_continue();
    redirect_exhausted_stops_fail();
    non_redirect_error_stops_fail();
    status_is_redirect_helper();
}
