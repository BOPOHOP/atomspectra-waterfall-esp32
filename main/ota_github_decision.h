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
// "считаем best новее", ota_gh_check() уже вело себя так). Порядок причин
// сохранён из исходного ota_gh_check(): !newer проверяется ПЕРВЫМ (та же
// версия ИЛИ даунгрейд -> "up_to_date", известный P3 из аудита -- не правим
// здесь, вне поручения этого хода), затем !above_min.
static inline ota_gh_decision_t ota_gh_decide(const ota_gh_version_t *best,
                                               const ota_gh_version_t *cur, bool have_cur,
                                               const ota_gh_version_t *min_ver)
{
    ota_gh_decision_t d;
    d.newer = have_cur ? (ota_gh_version_cmp(best, cur) > 0) : true;
    bool above_min = ota_gh_version_cmp(best, min_ver) >= 0;
    d.installable = d.newer && above_min;
    if (!d.newer) d.reason = "up_to_date";
    else if (!above_min) d.reason = "too_old_for_wifi_ota";
    else d.reason = "";
    return d;
}
