#include "ota_timeout_budget.h"
#include "test_util.h"

// D4 (verify-awf4-2026-09-27.md:250, sweep-B задача 1): ручная заливка по
// отчёту пережила 30 подряд идущих HTTPD_SOCK_ERR_TIMEOUT -- граница обязана
// пропустить streak=1..30 (continue) и оборвать РОВНО на 31-м.
#define OTA_TB_MAX 30u

static void budget_not_exceeded_within_observed_survival(void)
{
    for (uint32_t streak = 1; streak <= OTA_TB_MAX; streak++)
        CHECK(!ota_timeout_budget_exceeded(streak, OTA_TB_MAX));
}

static void budget_exceeded_right_after_observed_survival(void)
{
    CHECK(ota_timeout_budget_exceeded(OTA_TB_MAX + 1, OTA_TB_MAX));
}

static void budget_exceeded_stays_exceeded_further(void)
{
    CHECK(ota_timeout_budget_exceeded(OTA_TB_MAX + 2, OTA_TB_MAX));
    CHECK(ota_timeout_budget_exceeded(1000, OTA_TB_MAX));
}

static void budget_zero_streak_not_exceeded(void)
{
    CHECK(!ota_timeout_budget_exceeded(0, OTA_TB_MAX));
}

void ota_timeout_budget_suite(void)
{
    budget_not_exceeded_within_observed_survival();
    budget_exceeded_right_after_observed_survival();
    budget_exceeded_stays_exceeded_further();
    budget_zero_streak_not_exceeded();
}
