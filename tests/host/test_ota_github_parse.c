#include "ota_github_parse.h"
#include "test_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *load_fixture_alloc(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    *out_len = n;
    return buf;
}

// Реальная выгрузка `gh api repos/.../releases?per_page=10` (10 релизов,
// среди них firmware-v1.2.18 draft=true, firmware-v1.2.24 prerelease=true,
// firmware-v1.2.25 -- последний нормальный релиз). Проверяет разбор
// НАСТОЯЩЕГО документа GitHub (вложенные объекты author/assets/uploader,
// экранирование в текстах release notes), а не только синтетики.
static void parse_real_fixture_stable_only(void)
{
    size_t len;
    char *buf = load_fixture_alloc("fixtures/releases_page1_raw.json", &len);
    CHECK(buf != NULL);
    if (!buf) return;

    const char *os, *oe;
    ota_gh_version_t best;
    CHECK(ota_gh_releases_pick_best(buf, len, false, &os, &oe, &best));
    CHECK(best.major == 1 && best.minor == 2 && best.patch == 25 && !best.has_rc);

    size_t ulen;
    const char *url = ota_gh_find_asset_url(os, oe, "atomspectra_gw.bin", &ulen);
    CHECK(url != NULL);
    if (url) {
        CHECK(memcmp(url, "https://github.com/", 19) == 0);
        bool found = false;
        const char *needle = "firmware-v1.2.25";
        size_t nlen = strlen(needle);
        for (size_t off = 0; off + nlen <= ulen; off++) {
            if (memcmp(url + off, needle, nlen) == 0) { found = true; break; }
        }
        CHECK(found);
    }

    const char *sums_url = ota_gh_find_asset_url(os, oe, "SHA256SUMS.txt", &ulen);
    CHECK(sums_url != NULL);

    const char *missing = ota_gh_find_asset_url(os, oe, "no_such_asset", &ulen);
    CHECK(missing == NULL);

    free(buf);
}

// С каналом предрелизов включённым максимум должен стать 1.2.25 всё равно
// (1.2.25 не prerelease и это самый большой тег среди всех 10 записей --
// это подтверждено сравнением тегов в самой фикстуре).
static void parse_real_fixture_with_prerelease_channel(void)
{
    size_t len;
    char *buf = load_fixture_alloc("fixtures/releases_page1_raw.json", &len);
    CHECK(buf != NULL);
    if (!buf) return;

    const char *os, *oe;
    ota_gh_version_t best;
    CHECK(ota_gh_releases_pick_best(buf, len, true, &os, &oe, &best));
    CHECK(best.major == 1 && best.minor == 2 && best.patch == 25 && !best.has_rc);

    free(buf);
}

// draft всегда отбрасывается независимо от канала: firmware-v1.2.18 --
// draft=true в фикстуре, и это самый старый тег в наборе, так что его
// отбрасывание проверяется отдельно синтетическим документом с ОДНИМ
// draft-релизом (иначе тест не отличил бы "отброшен как draft" от
// "проиграл по версии").
static void parse_synthetic_draft_only_rejected(void)
{
    const char *json =
        "[{\"tag_name\":\"firmware-v9.9.9\",\"draft\":true,\"prerelease\":false,"
        "\"assets\":[{\"name\":\"a.bin\",\"browser_download_url\":\"http://x/a.bin\"}]}]";
    const char *os, *oe;
    ota_gh_version_t best;
    CHECK(!ota_gh_releases_pick_best(json, strlen(json), true, &os, &oe, &best));
    CHECK(!ota_gh_releases_pick_best(json, strlen(json), false, &os, &oe, &best));
}

// prerelease отбрасывается только при want_prerelease=false.
static void parse_synthetic_prerelease_gate(void)
{
    const char *json =
        "[{\"tag_name\":\"firmware-v3.0.0-rc1\",\"draft\":false,\"prerelease\":true,"
        "\"assets\":[]}]";
    const char *os, *oe;
    ota_gh_version_t best;
    CHECK(!ota_gh_releases_pick_best(json, strlen(json), false, &os, &oe, &best));
    CHECK(ota_gh_releases_pick_best(json, strlen(json), true, &os, &oe, &best));
    CHECK(best.has_rc && best.rc == 1);
}

// Кавычки и фигурные скобки внутри строкового поля "body" не должны сбивать
// границы объекта релиза (в реальных release notes встречаются markdown-
// таблицы и код-блоки с { }).
static void parse_synthetic_body_with_braces_and_quotes(void)
{
    const char *json =
        "[{\"tag_name\":\"firmware-v1.0.0\",\"draft\":false,\"prerelease\":false,"
        "\"body\":\"пример { \\\"json\\\": 1 } и кавычки \\\" внутри\","
        "\"assets\":[{\"name\":\"x.bin\",\"browser_download_url\":\"http://x/x.bin\"}]},"
        "{\"tag_name\":\"firmware-v2.0.0\",\"draft\":false,\"prerelease\":false,"
        "\"assets\":[{\"name\":\"y.bin\",\"browser_download_url\":\"http://x/y.bin\"}]}]";
    const char *os, *oe;
    ota_gh_version_t best;
    CHECK(ota_gh_releases_pick_best(json, strlen(json), false, &os, &oe, &best));
    CHECK(best.major == 2);
    size_t ulen;
    const char *url = ota_gh_find_asset_url(os, oe, "y.bin", &ulen);
    CHECK(url != NULL);
}

// Пустой массив и массив без единого разбираемого тега -> false.
static void parse_synthetic_empty_and_unparsable(void)
{
    const char *empty_json = "[]";
    const char *os, *oe;
    ota_gh_version_t best;
    CHECK(!ota_gh_releases_pick_best(empty_json, strlen(empty_json), true, &os, &oe, &best));

    const char *bad_json =
        "[{\"tag_name\":\"v1.0.0\",\"draft\":false,\"prerelease\":false,\"assets\":[]}]";
    CHECK(!ota_gh_releases_pick_best(bad_json, strlen(bad_json), true, &os, &oe, &best));
}

// P3 №5 (verify-awf5-github-ota-2026-09-27.md:258, sweep-B задача 3): "assets"
// (с РЕАЛЬНЫМИ именами из fixtures/releases_page1_raw.json) идёт ПЕРЕД
// top-level "name" -- без учёта глубины find_key нашёл бы вложенное
// asset.name="atomspectra_gw.bin" раньше настоящего release.name.
static const char *k_nested_name_json =
    "[{\"assets\":[{\"name\":\"atomspectra_gw.bin\","
    "\"browser_download_url\":\"http://x/a\"},"
    "{\"name\":\"SHA256SUMS.txt\",\"browser_download_url\":\"http://x/b\"}],"
    "\"tag_name\":\"firmware-v1.2.28\",\"draft\":false,\"prerelease\":false,"
    "\"name\":\"firmware-v1.2.28-release-title\"}]";

static void parse_nested_name_does_not_mask_toplevel_field(void)
{
    const char *json = k_nested_name_json;
    const char *os, *oe;
    ota_gh_version_t best;
    CHECK(ota_gh_releases_pick_best(json, strlen(json), false, &os, &oe, &best));
    size_t nlen;
    const char *name = ota_gh__string_field(os, oe, "\"name\"", &nlen);
    CHECK(name != NULL);
    if (name) {
        CHECK(nlen == strlen("firmware-v1.2.28-release-title"));
        CHECK(memcmp(name, "firmware-v1.2.28-release-title", nlen) == 0);
    }
}

void gh_parse_suite(void)
{
    parse_real_fixture_stable_only();
    parse_real_fixture_with_prerelease_channel();
    parse_synthetic_draft_only_rejected();
    parse_synthetic_prerelease_gate();
    parse_synthetic_body_with_braces_and_quotes();
    parse_synthetic_empty_and_unparsable();
    parse_nested_name_does_not_mask_toplevel_field();
}
