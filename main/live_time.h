#pragma once
#include <stdbool.h>
#include <stdint.h>

/* 1.2.36 OPT-1: одна формула живого времени (раньше — две static-копии:
 * web_server.c и spectrum_http_cache.c). Метод BecqMoni (эталон Am6er,
 * Utils/LiveTime.cs): мёртвое время на импульс tau = (RISE+FALL+1)/F, RISE/FALL/F —
 * из -inf прибора; за набор dead = (counts+lost)*tau, live = total - dead.
 * Тело дословно прежнее: тот же порядок float-операций, тот же перенос uint32 в сумме
 * counts+lost, поэтому результат побитово тот же (экспорты XML/N42/SPE и JSON). */
static inline float live_time_calc(uint32_t total_time_sec, uint32_t total_counts, uint32_t lost,
                                   bool di_valid, uint16_t rise, uint16_t fall, float freq)
{
    float total = (float)total_time_sec;
    if (!di_valid || freq <= 0.0f) return total;
    float tau = ((float)rise + (float)fall + 1.0f) / freq;
    float dead = (float)(total_counts + lost) * tau;
    if (dead < 0.0f) dead = 0.0f;
    if (dead > total) dead = total;
    return total - dead;
}
