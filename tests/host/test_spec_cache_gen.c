// М5 (release-gate-firmware-v1.2.28-code.md): host-тест на spec_cache_gen.h.
#include "spec_cache_gen.h"
#include "test_util.h"

void spec_cache_gen_suite(void)
{
    CHECK(spec_cache_gen_stale(5, 5) == false);   // invalidate() не вклинился — применяем
    CHECK(spec_cache_gen_stale(5, 6) == true);    // вклинился ровно один раз
    CHECK(spec_cache_gen_stale(5, 7) == true);    // вклинился несколько раз
    CHECK(spec_cache_gen_stale(0, 0) == false);   // ещё ни одной инвалидации в жизни кэша
}
