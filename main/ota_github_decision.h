#pragma once
// AWF-5 P2-фикс (verify-awf5-github-ota-2026-09-27.md разд.«Итог» №3):
// решающая часть ota_gh_check() (newer/above_min/reason) вынесена в чистую
// функцию от версий -- host-тестируема без сети/HTTP/NVS.
#include <stdbool.h>
#include "ota_github_version.h"

typedef struct {
    bool newer;         // best > cur (или have_cur=false -- считаем новее)
    bool installable;    // newer && best >= min_ver
    const char *reason;  // "" | "up_to_date" | "too_old_for_wifi_ota"
} ota_gh_decision_t;

// have_cur=false -- текущая версия не распознана (эквивалент старого кода:
// "считаем best новее"). Порядок причин: !newer проверяется ПЕРВЫМ, затем
// !above_min. P3 №6 (verify-awf5-github-ota-2026-09-27.md:259, sweep-B
// задача 4): best<cur (даунгрейд) -> "downgrade_blocked", best==cur ->
// "up_to_date" -- раньше обе ветки давали одну и ту же причину "up_to_date".
static inline ota_gh_decision_t ota_gh_decide(const ota_gh_version_t *best,
                                               const ota_gh_version_t *cur, bool have_cur,
                                               const ota_gh_version_t *min_ver)
{
    ota_gh_decision_t d;
    d.newer = have_cur ? (ota_gh_version_cmp(best, cur) > 0) : true;
    bool above_min = ota_gh_version_cmp(best, min_ver) >= 0;
    d.installable = d.newer && above_min;
    if (!d.newer) {
        bool is_downgrade = have_cur && ota_gh_version_cmp(best, cur) < 0;
        d.reason = is_downgrade ? "downgrade_blocked" : "up_to_date";
    }
    else if (!above_min) d.reason = "too_old_for_wifi_ota";
    else d.reason = "";
    return d;
}
