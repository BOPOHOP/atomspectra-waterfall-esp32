#include "json_uint_fmt.h"
#include "test_util.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
// UI-P1: побайтовое совпадение старого форматтера (vsnprintf/append_fmt,
// как в build_json_full) и нового на 0/UINT32_MAX/случайных — тело функций
// добавляется следующими правками.
static void check_one(uint32_t v, int need_comma)
{
    char old_buf[16];
    int on = snprintf(old_buf, sizeof(old_buf), "%s%" PRIu32, need_comma ? "," : "", v);
    char new_buf[16];
    size_t nn = json_append_uint32_csv(new_buf, sizeof(new_buf), 0, v, need_comma);
    CHECK((size_t)on == nn);
    CHECK(nn > 0 && memcmp(old_buf, new_buf, nn) == 0);
}

void json_uint_fmt_suite(void)
{
    check_one(0, 0);
    check_one(0, 1);
    check_one(UINT32_MAX, 0);
    check_one(UINT32_MAX, 1);
    check_one(1, 0);
    check_one(9, 1);
    check_one(10, 1);
    check_one(4294967294u, 1);
    unsigned seed = 20260927u;
    for (int i = 0; i < 5000; i++) {
        seed = seed * 1103515245u + 12345u;
        check_one(seed, i % 2);
    }
    // Полный массив "[bins]"-стиля, byte-in-byte, как build_json_full.
    enum { N = 32 };
    uint32_t vals[N];
    vals[0] = 0; vals[1] = UINT32_MAX; vals[2] = 1; vals[3] = 4294967294u;
    unsigned s2 = 777u;
    for (int i = 4; i < N; i++) { s2 = s2 * 1103515245u + 12345u; vals[i] = s2; }

    char old_full[512]; size_t op = 0;
    char new_full[512]; size_t np = 0;
    for (int i = 0; i < N; i++) {
        int n = snprintf(old_full + op, sizeof(old_full) - op, "%s%" PRIu32, i ? "," : "", vals[i]);
        op += (size_t)n;
        np += json_append_uint32_csv(new_full, sizeof(new_full), np, vals[i], i != 0);
    }
    CHECK(op == np);
    CHECK(memcmp(old_full, new_full, np) == 0);
}
