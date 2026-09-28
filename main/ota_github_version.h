#pragma once
// AWF-5: разбор тега релиза GitHub ("firmware-vX.Y.Z" / "firmware-vX.Y.Z-rcN")
// и сравнение версий semver-приоритетом (для host-теста и прошивки — общий
// код, зависимостей нет, поэтому файл используется на обеих сторонах).
//
// Прецедент rc: 1.2.26-rc1 < 1.2.26-rc2 < 1.2.26 < 1.2.27 (релиз без -rc
// считается "выше" любого rc той же тройки чисел — обычный semver-приоритет
// pre-release < release).
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
    uint32_t major;
    uint32_t minor;
    uint32_t patch;
    bool     has_rc;     // true -- версия ...-rcN (pre-release)
    uint32_t rc;         // валиден только при has_rc
} ota_gh_version_t;

// Разбирает тег вида "firmware-vX.Y.Z" или "firmware-vX.Y.Z-rcN".
// Возвращает true при успехе. Отклоняет: отсутствие префикса, нечисловые
// компоненты, "-rc" без числа, мусор после номера rc, версии без patch.
static inline bool ota_gh_version_parse_tag(const char *tag, ota_gh_version_t *out)
{
    if (!tag || !out) return false;
    static const char PFX[] = "firmware-v";
    size_t pfx_len = sizeof(PFX) - 1;
    if (strncmp(tag, PFX, pfx_len) != 0) return false;
    const char *p = tag + pfx_len;

    char *end = NULL;
    unsigned long v;

    if (!isdigit((unsigned char)*p)) return false;
    v = strtoul(p, &end, 10);
    if (end == p || *end != '.') return false;
    out->major = (uint32_t)v;
    p = end + 1;

    if (!isdigit((unsigned char)*p)) return false;
    v = strtoul(p, &end, 10);
    if (end == p || *end != '.') return false;
    out->minor = (uint32_t)v;
    p = end + 1;

    if (!isdigit((unsigned char)*p)) return false;
    v = strtoul(p, &end, 10);
    if (end == p) return false;
    out->patch = (uint32_t)v;
    p = end;

    if (*p == '\0') {
        out->has_rc = false;
        out->rc = 0;
        return true;
    }
    if (strncmp(p, "-rc", 3) != 0) return false;
    p += 3;
    if (!isdigit((unsigned char)*p)) return false;
    v = strtoul(p, &end, 10);
    if (end == p || *end != '\0') return false;
    out->has_rc = true;
    out->rc = (uint32_t)v;
    return true;
}

// Сравнение: <0 a<b, 0 a==b, >0 a>b. semver-приоритет: release > любой rc
// той же тройки; rc сравниваются по номеру; тройки чисел -- лексикографически.
static inline int ota_gh_version_cmp(const ota_gh_version_t *a, const ota_gh_version_t *b)
{
    if (a->major != b->major) return (a->major < b->major) ? -1 : 1;
    if (a->minor != b->minor) return (a->minor < b->minor) ? -1 : 1;
    if (a->patch != b->patch) return (a->patch < b->patch) ? -1 : 1;
    if (a->has_rc == b->has_rc) {
        if (!a->has_rc) return 0;
        if (a->rc != b->rc) return (a->rc < b->rc) ? -1 : 1;
        return 0;
    }
    return a->has_rc ? -1 : 1;
}
