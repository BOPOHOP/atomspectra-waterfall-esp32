#pragma once
// #DOSE-A: живая мощность дозы по кривой/коэффициенту (расчёт на плате). Общие типы и прототипы для spectrogram.c,
// monitor.c, spectrum_http_cache.c, web_server.c. Заголовок host-pure: только stdlib (его включают host-тесты).
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <math.h>
#include <inttypes.h>

// Результат monitor_dose_live(): значение МД за окно и режим.
typedef struct {
    bool        have;   // есть конечное значение rate
    float       rate;   // µSv/h
    const char *mode;   // "curve" | "scalar" | "none"
    uint32_t    w;      // фактический охват окна, с
} spec_dose_t;

// Реализованы в spectrogram.c. Режим: 0 = нет МД (ни кривой, ни k), 1 = scalar (k > 0), 2 = curve (LUT загружена).
int      spectrogram_dose_mode(void);
// Поколение кривой/k: НЕЧЁТНОЕ, пока кривая (пере)загружается; растёт при любой её смене.
uint32_t spectrogram_dose_gen(void);
// Σ (cur[i]-prev[i]) * lut[i], µSv/h·с; false — режим не curve или откат гистограммы (cur[i] < prev[i]).
bool     spectrogram_dose_delta_sum(const uint32_t *prev, const uint32_t *cur, float *out_wsum);
// Прежнее API (spectrogram.h): µSv/h на 1 cps для scalar. Повтор прототипа допустим.
float    spectrogram_get_dose_k(void);

// Хвост JSON: ,"dose_rate":<число|null>,"dose_mode":"<режим>","dose_w":<с> (БЕЗ закрывающей '}'). NaN/нет значения -> null
// (JSON не допускает NaN). Возвращает число записанных символов (как snprintf), 0 при ошибке; усечение — размер >= cap.
static inline size_t spec_dose_tail(char *buf, size_t cap, const spec_dose_t *d)
{
    const char *mode = (d && d->mode) ? d->mode : "none";
    int k;
    if (d && d->have && isfinite(d->rate))
        k = snprintf(buf, cap, ",\"dose_rate\":%.4f,\"dose_mode\":\"%s\",\"dose_w\":%" PRIu32, (double)d->rate, mode, d->w);
    else
        k = snprintf(buf, cap, ",\"dose_rate\":null,\"dose_mode\":\"%s\",\"dose_w\":%" PRIu32, mode, d ? d->w : (uint32_t)0);
    return (k < 0) ? 0 : (size_t)k;
}