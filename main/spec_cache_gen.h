#pragma once
#include <stdbool.h>
#include <stdint.h>

/* М5 (release-gate-firmware-v1.2.28-code.md): решение "отбросить ли только
 * что собранный кэш" — сборка снимается ВНЕ мьютекса (main/spectrum_http_cache.c
 * spectrum_http_cache_finish_build), invalidate() может успеть между снимком
 * и commit'ом. gen_before — поколение на момент старта сборки, gen_now — на
 * момент commit'а (оба читаются под тем же s_mtx). Разошлись — снимок мог
 * быть снят со старыми данными, отбрасываем. */
static inline bool spec_cache_gen_stale(uint32_t gen_before, uint32_t gen_now)
{
    return gen_before != gen_now;
}
