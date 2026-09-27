#include "ota_github_version.h"
#include "test_util.h"

static void version_parse_ok_cases(void)
{
    ota_gh_version_t v;

    CHECK(ota_gh_version_parse_tag("firmware-v1.2.26", &v));
    CHECK(v.major == 1 && v.minor == 2 && v.patch == 26 && !v.has_rc);

    CHECK(ota_gh_version_parse_tag("firmware-v1.2.26-rc1", &v));
    CHECK(v.major == 1 && v.minor == 2 && v.patch == 26 && v.has_rc && v.rc == 1);

    CHECK(ota_gh_version_parse_tag("firmware-v0.0.0", &v));
    CHECK(v.major == 0 && v.minor == 0 && v.patch == 0 && !v.has_rc);

    CHECK(ota_gh_version_parse_tag("firmware-v10.20.300-rc42", &v));
    CHECK(v.major == 10 && v.minor == 20 && v.patch == 300 && v.has_rc && v.rc == 42);
}

static void version_parse_reject_cases(void)
{
    ota_gh_version_t v;

    CHECK(!ota_gh_version_parse_tag(NULL, &v));
    CHECK(!ota_gh_version_parse_tag("firmware-v1.2.26", NULL));
    CHECK(!ota_gh_version_parse_tag("v1.2.26", &v));               // нет префикса firmware-
    CHECK(!ota_gh_version_parse_tag("firmware-v1.2", &v));         // нет patch
    CHECK(!ota_gh_version_parse_tag("firmware-v1.2.", &v));        // patch пуст
    CHECK(!ota_gh_version_parse_tag("firmware-va.2.26", &v));      // нечисловой major
    CHECK(!ota_gh_version_parse_tag("firmware-v1.2.26-rc", &v));   // rc без числа
    CHECK(!ota_gh_version_parse_tag("firmware-v1.2.26-rc1x", &v)); // мусор после rc
    CHECK(!ota_gh_version_parse_tag("firmware-v1.2.26x", &v));     // мусор после patch
    CHECK(!ota_gh_version_parse_tag("firmware-v1.2.26-beta1", &v)); // не -rc
    CHECK(!ota_gh_version_parse_tag("", &v));
}

static ota_gh_version_t mk(uint32_t maj, uint32_t min, uint32_t pat, bool has_rc, uint32_t rc)
{
    ota_gh_version_t v = { maj, min, pat, has_rc, rc };
    return v;
}

static void version_cmp_semver_priority(void)
{
    // 1.2.26-rc1 < 1.2.26-rc2 < 1.2.26 < 1.2.27, и 1.2.25 < 1.2.26-rc1.
    ota_gh_version_t rc1 = mk(1, 2, 26, true, 1);
    ota_gh_version_t rc2 = mk(1, 2, 26, true, 2);
    ota_gh_version_t rel = mk(1, 2, 26, false, 0);
    ota_gh_version_t next = mk(1, 2, 27, false, 0);
    ota_gh_version_t prev = mk(1, 2, 25, false, 0);

    CHECK(ota_gh_version_cmp(&rc1, &rc2) < 0);
    CHECK(ota_gh_version_cmp(&rc2, &rel) < 0);
    CHECK(ota_gh_version_cmp(&rel, &next) < 0);
    CHECK(ota_gh_version_cmp(&prev, &rc1) < 0);
    CHECK(ota_gh_version_cmp(&rc1, &prev) > 0);
    CHECK(ota_gh_version_cmp(&rel, &rel) == 0);
    CHECK(ota_gh_version_cmp(&rc1, &rc1) == 0);

    ota_gh_version_t major2 = mk(2, 0, 0, false, 0);
    CHECK(ota_gh_version_cmp(&next, &major2) < 0);

    ota_gh_version_t minor_hi = mk(1, 3, 0, false, 0);
    CHECK(ota_gh_version_cmp(&rel, &minor_hi) < 0);
}

void version_suite(void)
{
    version_parse_ok_cases();
    version_parse_reject_cases();
    version_cmp_semver_priority();
}
