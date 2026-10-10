// 1.2.36 H5 (C): срок жизни медленной части /api/system (main/ttl_plan.h). Образцы времени — микросекунды esp_timer.
#include "ttl_plan.h"
#include "test_util.h"
#include <stdio.h>
#define NCHECK(name, cond) do { if (!(cond)) { printf("FAIL %s %s:%d: %s\n", name, __FILE__, __LINE__, #cond); g_failures++; } } while (0)
#define TTL10 (10LL * 1000000LL)

static void test_ttl_first_call(void)
{
    int64_t nows[] = {0, 1, 5000000, TTL10, TTL10 + 1, 86400000000LL};
    size_t i;
    for (i = 0; i < sizeof(nows) / sizeof(nows[0]); i++) {
        NCHECK("ttl_first_call", ttl_expired(0, nows[i], TTL10));
        NCHECK("ttl_first_call", ttl_expired(0, nows[i], 0));
        NCHECK("ttl_first_call", ttl_expired(0, nows[i], 30000000));
    }
}

static void test_ttl_inside(void)
{
    int64_t at = 1000000;
    int64_t deltas[] = {0, 1, 5000000, TTL10 - 1, TTL10};
    size_t i;
    for (i = 0; i < sizeof(deltas) / sizeof(deltas[0]); i++) {
        NCHECK("ttl_inside", !ttl_expired(at, at + deltas[i], TTL10));
    }
    NCHECK("ttl_inside", !ttl_expired(at, at + 30000000, 30000000));
}

static void test_ttl_after(void)
{
    int64_t at = 1000000;
    int64_t deltas[] = {TTL10 + 1, TTL10 + 1000, 5 * TTL10};
    size_t i;
    for (i = 0; i < sizeof(deltas) / sizeof(deltas[0]); i++) {
        NCHECK("ttl_after", ttl_expired(at, at + deltas[i], TTL10));
    }
    NCHECK("ttl_after", ttl_expired(at, at + 30000001, 30000000));
}

void ttl_plan_suite(void)
{
    test_ttl_first_call();
    test_ttl_inside();
    test_ttl_after();
}
