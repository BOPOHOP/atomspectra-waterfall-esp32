#include "ota_github_decision.h"
#include "test_util.h"
#include <string.h>

static ota_gh_version_t mk(uint32_t maj, uint32_t min, uint32_t pat, bool has_rc, uint32_t rc)
{
    ota_gh_version_t v = { maj, min, pat, has_rc, rc };
    return v;
}

static void decision_newer_and_above_min_is_installable(void)
{
    ota_gh_version_t best = mk(1, 2, 27, false, 0);
    ota_gh_version_t cur  = mk(1, 2, 26, false, 0);
    ota_gh_version_t min  = mk(1, 2, 26, true, 1);
    ota_gh_decision_t d = ota_gh_decide(&best, &cur, true, &min);
    CHECK(d.newer);
    CHECK(d.installable);
    CHECK(strcmp(d.reason, "") == 0);
}

static void decision_equal_version_is_up_to_date(void)
{
    ota_gh_version_t best = mk(1, 2, 26, false, 0);
    ota_gh_version_t cur  = mk(1, 2, 26, false, 0);
    ota_gh_version_t min  = mk(1, 2, 26, true, 1);
    ota_gh_decision_t d = ota_gh_decide(&best, &cur, true, &min);
    CHECK(!d.newer);
    CHECK(!d.installable);
    CHECK(strcmp(d.reason, "up_to_date") == 0);
}

static void decision_older_than_current_is_up_to_date(void)
{
    ota_gh_version_t best = mk(1, 2, 25, false, 0);
    ota_gh_version_t cur  = mk(1, 2, 26, false, 0);
    ota_gh_version_t min  = mk(1, 2, 26, true, 1);
    ota_gh_decision_t d = ota_gh_decide(&best, &cur, true, &min);
    CHECK(!d.newer);
    CHECK(!d.installable);
    CHECK(strcmp(d.reason, "up_to_date") == 0);
}

static void decision_newer_but_below_min_blocked(void)
{
    // best новее cur, но старше минимальной версии с Wi-Fi OTA (например cur
    // -- древняя dev-сборка без механизма вовсе, best -- 1.2.20 < min 1.2.26-rc1).
    ota_gh_version_t best = mk(1, 2, 20, false, 0);
    ota_gh_version_t cur  = mk(1, 2, 10, false, 0);
    ota_gh_version_t min  = mk(1, 2, 26, true, 1);
    ota_gh_decision_t d = ota_gh_decide(&best, &cur, true, &min);
    CHECK(d.newer);
    CHECK(!d.installable);
    CHECK(strcmp(d.reason, "too_old_for_wifi_ota") == 0);
}

static void decision_exactly_min_rc_is_installable(void)
{
    ota_gh_version_t best = mk(1, 2, 26, true, 1);   // ровно минимальная версия
    ota_gh_version_t cur  = mk(1, 2, 25, false, 0);
    ota_gh_version_t min  = mk(1, 2, 26, true, 1);
    ota_gh_decision_t d = ota_gh_decide(&best, &cur, true, &min);
    CHECK(d.newer);
    CHECK(d.installable);
    CHECK(strcmp(d.reason, "") == 0);
}

static void decision_rc_below_rc_min_blocked(void)
{
    ota_gh_version_t best = mk(1, 2, 26, true, 0);   // rc0 -- ниже min rc1, та же тройка
    ota_gh_version_t cur  = mk(1, 2, 25, false, 0);
    ota_gh_version_t min  = mk(1, 2, 26, true, 1);
    ota_gh_decision_t d = ota_gh_decide(&best, &cur, true, &min);
    CHECK(d.newer);
    CHECK(!d.installable);
    CHECK(strcmp(d.reason, "too_old_for_wifi_ota") == 0);
}

static void decision_no_current_version_treated_as_newer(void)
{
    ota_gh_version_t best = mk(1, 2, 26, true, 1);
    ota_gh_version_t cur  = mk(0, 0, 0, false, 0);   // не используется при have_cur=false
    ota_gh_version_t min  = mk(1, 2, 26, true, 1);
    ota_gh_decision_t d = ota_gh_decide(&best, &cur, false, &min);
    CHECK(d.newer);
    CHECK(d.installable);
}

void ota_gh_decision_suite(void)
{
    decision_newer_and_above_min_is_installable();
    decision_equal_version_is_up_to_date();
    decision_older_than_current_is_up_to_date();
    decision_newer_but_below_min_blocked();
    decision_exactly_min_rc_is_installable();
    decision_rc_below_rc_min_blocked();
    decision_no_current_version_treated_as_newer();
}
