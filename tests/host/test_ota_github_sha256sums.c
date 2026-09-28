#include "ota_github_sha256sums.h"
#include "test_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Читает fixtures/SHA256SUMS_v1.2.25.txt (реальная выгрузка релиза,
// tests/host/fixtures/) целиком в буфер. Возвращает длину, 0 при ошибке.
static size_t load_fixture(const char *path, char *buf, size_t cap)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = '\0';
    return n;
}

static void sha256sums_real_fixture(void)
{
    static char buf[8192];
    size_t n = load_fixture("fixtures/SHA256SUMS_v1.2.25.txt", buf, sizeof(buf));
    CHECK(n > 0);
    if (n == 0) return;

    char hex[65];
    CHECK(ota_gh_sha256sums_find(buf, n, "atomspectra_gw.bin", hex));
    CHECK(strcmp(hex, "3b302bdae7b4f5edd30511b5238870f506cc4cd2a8bdd9f85ffb52f7c3b02614") == 0);

    CHECK(ota_gh_sha256sums_find(buf, n, "flash_args", hex));
    CHECK(strcmp(hex, "edfd5f132ebdd976097570519f3c1695e2dce7048b6e06d11445f253c15ffff0") == 0);

    CHECK(!ota_gh_sha256sums_find(buf, n, "no_such_file.bin", hex));
}

static void sha256sums_synthetic_cases(void)
{
    char hex[65];
    const char *bin_mode = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa *foo.bin\n";
    CHECK(ota_gh_sha256sums_find(bin_mode, strlen(bin_mode), "foo.bin", hex));
    CHECK(strcmp(hex, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") == 0);

    const char *text_mode = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb  bar.bin\n";
    CHECK(ota_gh_sha256sums_find(text_mode, strlen(text_mode), "bar.bin", hex));
    CHECK(strcmp(hex, "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb") == 0);

    const char *crlf = "cccccccc" "cccccccc" "cccccccc" "cccccccc"
                        "cccccccc" "cccccccc" "cccccccc" "cccccccc" " *baz.bin\r\n";
    CHECK(ota_gh_sha256sums_find(crlf, strlen(crlf), "baz.bin", hex));

    const char *empty = "";
    CHECK(!ota_gh_sha256sums_find(empty, 0, "foo.bin", hex));

    const char *short_hash = "abcd *tiny.bin\n";
    CHECK(!ota_gh_sha256sums_find(short_hash, strlen(short_hash), "tiny.bin", hex));

    const char *not_hex = "gggggggggggggggggggggggggggggggggggggggggggggggggggggggggggggg *bad.bin\n";
    CHECK(!ota_gh_sha256sums_find(not_hex, strlen(not_hex), "bad.bin", hex));

    // Регистрозависимость имени файла.
    const char *casey = "dddddddd" "dddddddd" "dddddddd" "dddddddd"
                         "dddddddd" "dddddddd" "dddddddd" "dddddddd" " *Foo.Bin\n";
    CHECK(!ota_gh_sha256sums_find(casey, strlen(casey), "foo.bin", hex));

    // Имя-подстрока другого имени не должно совпасть (точная длина).
    const char *prefix = "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee *atomspectra_gw.bin.old\n";
    CHECK(!ota_gh_sha256sums_find(prefix, strlen(prefix), "atomspectra_gw.bin", hex));
}

void sha256sums_suite(void)
{
    sha256sums_real_fixture();
    sha256sums_synthetic_cases();
}
