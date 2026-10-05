#pragma once
// #AWF-F2: StartDateTime в N42 — ISO 8601 в UTC с суффиксом Z (импорт читает его как UTC).
// Чистая функция без ESP-IDF (host-тест: tests/host/test_n42_time_plan.c).
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

// "YYYY-MM-DDTHH:MM:SSZ" (20 символов + NUL). Вход — эпоха UTC (saved_at - total_time_sec).
// Возврат: число символов либо 0 (NULL, n < 21, t < 0, год > 9999; нижняя граница года при t >= 0 недостижима).
// Формат отличается от эталона BecqMoni (dd.mm.yyyy местного времени): это сознательное решение #AWF-F2.
static inline int n42_start_iso_utc(int64_t t, char *buf, size_t n)
{
    time_t tt = (time_t)t;
    struct tm tm;
    if (!buf || n < 21 || t < 0 || gmtime_r(&tt, &tm) == NULL) return 0;
    if (tm.tm_year < -1900 || tm.tm_year > 9999 - 1900) return 0;
    return snprintf(buf, n, "%04d-%02d-%02dT%02d:%02d:%02dZ", tm.tm_year + 1900,
                    tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
}
