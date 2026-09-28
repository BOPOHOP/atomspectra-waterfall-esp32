#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "atomspectra.h"
#include "calib_autoread.h"

// R7 (sweep-A, 1.2.28): единый признак «калибровка есть» для ФАЙЛОВЫХ выводов —
// экспорт XML/N42/SPE (web_server.c), экспорт n42/ASWF водопада (web_waterfall.c),
// шапки сегментов ASWF (spectrogram.c). Раньше они решали по голому calib_valid и
// выписывали нулевой полином как заданную калибровку. Теперь — тот же предикат, что
// spectrum_calibration_is_missing()/calib_is_missing(): невалидна, все коэффициенты
// ровно 0.0 или есть NaN/Inf — «не задана». Нулевая калибровка даёт РОВНО тот же
// вывод, что уже выдаётся при calib_valid=false (блок калибровки не пишется) — новой
// формы вывода нет, сторонние читатели такой файл уже получают.
// JSON собственного Web UI (/api/device, /api/spectrum, render_spectrum_json, WS-шапка)
// сюда не входит: там решение отдаётся полем calib_set тем же предикатом (#AWF-12b F2/R4).

static inline bool calib_export_present(const spectrum_data_t *sp)
{
    return sp != NULL && !calib_is_missing(sp->calibration, CALIB_COEFFS, sp->calib_valid);
}
