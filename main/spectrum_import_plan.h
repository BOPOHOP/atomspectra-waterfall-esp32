#pragma once
// 1.2.31: импорт фонового спектра (проект audit/design-1.2.31-import.md). Чистая логика без ESP-IDF — host-тест
// tests/host/test_spectrum_import_plan.c. Тело запроса ASI1: 128 Б заголовка + 8192 x u32, всё little-endian,
// раскладка задана смещениями (spectrum_data_t содержит time_t/bool/паддинг и на хосте и плате различается).
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "atomspectra.h"

#define SPEC_IMPORT_MAGIC    0x31495341u
#define SPEC_IMPORT_HDR      128u
#define SPEC_IMPORT_SIZE     (SPEC_IMPORT_HDR + SPECTRUM_CHANNELS * 4u)
#define SPEC_IMPORT_MAX_TIME 315360000u

typedef enum { IMP_OK, IMP_BAD_SIZE, IMP_BAD_MAGIC, IMP_BAD_VERSION, IMP_BAD_CHANNELS,
               IMP_BAD_CRC, IMP_BAD_TIME, IMP_BAD_COUNTS, IMP_BAD_CALIB, IMP_BAD_SERIAL } imp_err_t;

static inline uint32_t imp_rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
// CRC-32 IEEE (отражённый, 0xEDB88320). c — значение предыдущего куска (0 для начала).
static inline uint32_t imp_crc32(const uint8_t *p, size_t n, uint32_t c)
{
    c = ~c;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}
static inline const char *imp_err_str(imp_err_t e)
{
    static const char *const s[] = { "ok", "bad_size", "bad_magic", "bad_version", "bad_channels",
                                     "bad_crc", "bad_time", "bad_counts", "bad_calib", "bad_serial" };
    return ((unsigned)e < sizeof(s) / sizeof(s[0])) ? s[e] : "bad";
}
// Калибровка: order -1..4; при order >= 0 коэффициенты 0..order конечны и не все нули; хвост за order обнуляется.
static inline imp_err_t imp_take_calib(const uint8_t *b, spectrum_data_t *o)
{
    int32_t ord = (int32_t)imp_rd32(b + 20);
    if (ord < -1 || ord >= CALIB_COEFFS) return IMP_BAD_CALIB;
    memset(o->calibration, 0, sizeof(o->calibration));
    o->calib_order = ord; o->calib_valid = (ord >= 0);
    int any = 0;
    for (int i = 0; i <= ord; i++) {
        double d; memcpy(&d, b + 24 + 8 * i, 8);
        if (!isfinite(d)) return IMP_BAD_CALIB;
        if (d != 0.0) any = 1;
        o->calibration[i] = d;
    }
    return (ord >= 0 && !any) ? IMP_BAD_CALIB : IMP_OK;
}
// Серийник: 48 Б, печатный ASCII 0x20..0x7E, обязательно завершён NUL; в запись кладётся с префиксом "IMP:" (признак импорта).
static inline imp_err_t imp_take_serial(const uint8_t *b, spectrum_data_t *o)
{
    size_t n = 0;
    while (n < 48 && b[72 + n] != 0) { if (b[72 + n] < 0x20 || b[72 + n] > 0x7E || b[72 + n] == '"' || b[72 + n] == '\\') return IMP_BAD_SERIAL; n++; }
    if (n == 48) return IMP_BAD_SERIAL;
    memset(o->serial_number, 0, sizeof(o->serial_number));
    memcpy(o->serial_number, "IMP:", 4); memcpy(o->serial_number + 4, b + 72, n);
    return IMP_OK;
}
// Метаданные записи по уже проверенным s (сумма каналов), t (время, с), lost.
static inline void imp_fill_meta(const uint8_t *b, spectrum_data_t *o, uint64_t s, uint32_t t, uint32_t lost)
{
    uint64_t sa = (uint64_t)imp_rd32(b + 64) | (uint64_t)imp_rd32(b + 68) << 32;   // i64: >2^32 и отрицательные не принимаются
    o->total_counts = (uint32_t)s; o->total_time_sec = t; o->lost_impulses = lost; o->cps = t ? (uint32_t)(s / t) : 0;
    o->saved_at = (sa <= 0xFFFFFFFFull) ? (time_t)sa : time(NULL);
    o->cpu_load = 0; o->pulse_width = 0; o->temperature[0] = o->temperature[1] = o->temperature[2] = NAN;
    o->valid = true;
}
// Разбор и проверка тела ASI1 в *o (на плате — в PSRAM). При отказе содержимое *o не определено.
// Порядок проверок — как в таблице design-1.2.31-import.md §1.2.
static inline imp_err_t spectrum_import_decode(const uint8_t *b, size_t len, spectrum_data_t *o)
{
    if (len != SPEC_IMPORT_SIZE) return IMP_BAD_SIZE;
    if (imp_rd32(b) != SPEC_IMPORT_MAGIC) return IMP_BAD_MAGIC;
    if ((b[4] | b[5] << 8) != 1 || (b[6] | b[7] << 8) != (int)SPEC_IMPORT_HDR) return IMP_BAD_VERSION;
    if (imp_rd32(b + 8) != SPECTRUM_CHANNELS) return IMP_BAD_CHANNELS;
    uint32_t c = imp_crc32(b, 124, 0);
    c = imp_crc32(b + SPEC_IMPORT_HDR, len - SPEC_IMPORT_HDR, c);
    if (c != imp_rd32(b + 124)) return IMP_BAD_CRC;
    uint32_t t = imp_rd32(b + 12);
    if (t == 0 || t > SPEC_IMPORT_MAX_TIME) return IMP_BAD_TIME;
    uint64_t s = 0;
    for (int i = 0; i < SPECTRUM_CHANNELS; i++) s += (o->bins[i] = imp_rd32(b + SPEC_IMPORT_HDR + 4 * i));
    uint32_t lost = imp_rd32(b + 16);
    if (s == 0 || s + lost > UINT32_MAX) return IMP_BAD_COUNTS;
    imp_err_t e = imp_take_calib(b, o);
    if (e == IMP_OK) e = imp_take_serial(b, o);
    if (e != IMP_OK) return e;
    imp_fill_meta(b, o, s, t, lost);
    return IMP_OK;
}
// POST /api/saved/<i>/delete: индекс 0..9998 или -1. Раньше любой POST /api/saved/* удалял запись (atoi без проверки
// суффикса: /api/saved/import и /api/saved/abc стирали spec_0000.bin). Допустим только точный путь (после него — конец или '?').
static inline int saved_delete_index(const char *uri)
{
    static const char pre[] = "/api/saved/", suf[] = "/delete";
    if (!uri || strncmp(uri, pre, sizeof(pre) - 1) != 0) return -1;
    const char *p = uri + sizeof(pre) - 1;
    int n = 0, v = 0;
    while (*p >= '0' && *p <= '9' && n < 5) { v = v * 10 + (*p - '0'); p++; n++; }
    if (n == 0 || n > 4) return -1;
    if (strncmp(p, suf, sizeof(suf) - 1) != 0) return -1;
    p += sizeof(suf) - 1;
    if (*p != '\0' && *p != '?') return -1;
    return (v <= 9998) ? v : -1;
}
