#define _POSIX_C_SOURCE 200809L
#include "../../main/n42_time_plan.h"
#include <stdlib.h>
#include <string.h>

static int fails = 0;

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while(0)

static int iso_eq(int64_t t, const char *want) {
    char buf[32];
    int ret = n42_start_iso_utc(t, buf, sizeof(buf));
    if (ret != 20) return 0;
    if (strcmp(buf, want) != 0) return 0;
    return 1;
}

int main(void) {
    setenv("TZ", "MSK-3", 1);
    tzset();

    CHECK(iso_eq(1700000000, "2023-11-14T22:13:20Z"));
    CHECK(iso_eq(1700000000 - 2592000, "2023-10-15T22:13:20Z"));
    CHECK(iso_eq(0, "1970-01-01T00:00:00Z"));
    CHECK(iso_eq(1709164800, "2024-02-29T00:00:00Z"));
    CHECK(iso_eq(1767225599, "2025-12-31T23:59:59Z"));
    CHECK(iso_eq(1704067200 + 3600 * 3, "2024-01-01T03:00:00Z"));

    char b[32];
    memset(b, 'x', sizeof(b));
    CHECK(n42_start_iso_utc(1700000000, b, 21) == 20 && b[19] == 'Z' && b[20] == '\0');
    CHECK(n42_start_iso_utc(1700000000, b, 20) == 0);
    CHECK(n42_start_iso_utc(1700000000, NULL, 32) == 0);
    CHECK(n42_start_iso_utc(-1, b, 32) == 0);
    /* F8: граница года 9999: 253402300799 = 9999-12-31T23:59:59Z (ещё можно), +1 = год 10000 (отказ). */
    CHECK(iso_eq(253402300799LL, "9999-12-31T23:59:59Z"));
    CHECK(n42_start_iso_utc(253402300800LL, b, 32) == 0);
    CHECK(n42_start_iso_utc(1099511627776LL, b, 32) == 0);   /* 2^40 */

    if (fails == 0) {
        printf("test_n42_time_plan: OK\n");
        return 0;
    } else {
        printf("test_n42_time_plan: FAILED\n");
        return 1;
    }
}
