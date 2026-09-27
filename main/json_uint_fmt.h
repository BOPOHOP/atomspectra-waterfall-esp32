#pragma once
#include <stdint.h>
#include <stddef.h>
// UI-P1 (2026-09-27, ui-perf-2026-09-27.md P1): замена vsnprintf("%"PRIu32)
// для горячего цикла SPECTRUM_CHANNELS=8192 бинов (spectrum_http_cache.c
// build_json_full). vsnprintf/append_fmt/realloc-доубливание под мьютексом
// на 8192 итерациях — источник выбросов 300-2000мс на /api/spectrum.
// Пишет ASCII-десятичное v в buf[0..], БЕЗ '\0'. buf обязан иметь >=10
// свободных байт (UINT32_MAX="4294967295"=10 симв.) — caller гарантирует.
// Возвращает число записанных байт.
static inline size_t json_uint32_to_ascii(uint32_t v, char *buf)
{
    if (v == 0) { buf[0] = '0'; return 1; }
    char tmp[10];
    int n = 0;
    while (v) { tmp[n++] = (char)('0' + (v % 10u)); v /= 10u; }
    for (int i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
    return (size_t)n;
}
// Пишет (","+)v в buf[pos..cap), НЕ выходя за cap. Возвращает число реально
// записанных байт, 0 если не влезло (caller растит буфер — как раньше делал
// append_fmt/realloc). need_comma=0 для первого элемента массива.
static inline size_t json_append_uint32_csv(char *buf, size_t cap, size_t pos,
                                             uint32_t v, int need_comma)
{
    char tmp[11];
    size_t tn = 0;
    if (need_comma) tmp[tn++] = ',';
    tn += json_uint32_to_ascii(v, tmp + tn);
    if (pos + tn > cap) return 0;
    for (size_t i = 0; i < tn; i++) buf[pos + i] = tmp[i];
    return tn;
}
