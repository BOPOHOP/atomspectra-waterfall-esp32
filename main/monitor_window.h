#pragma once
// 1.2.36 #MX-14 (G): SMA главной из кольца платы (host-pure). SMA = Σcounts / Σdur по последним сэмплам кольца,
// пока Σdur < W; сэмпл, на котором Σdur достигает W, включается (решение оператора). head — куда ляжет СЛЕДУЮЩИЙ сэмпл.
#include <stddef.h>
#include <stdint.h>
#include "monitor.h"
#include <math.h>
// #DOSE-A: окно дозы (host-pure). Отбор сэмплов ИДЕНТИЧЕН mon_window_sum() (цикл идёт по ВСЕМ сэмплам, пока Σdur < w_sec;
// сэмпл достижения W включается); в суммы дозы входят только сэмплы с конечным dose[idx]. Сумма — float с компенсацией Кэхэна.
static inline uint32_t mon_window_dose_sum(const monitor_sample_t *ring, const float *dose, uint32_t cap, uint32_t head,
                                           uint32_t count, uint32_t w_sec,
                                           float *sum_ws, uint32_t *sum_dur_dose, uint32_t *n_dose)
{
    float s = 0.0f, c = 0.0f;
    uint32_t sd = 0, sdd = 0, n = 0, nd = 0;
    for (uint32_t i = 0; i < count && sd < w_sec; i++) {
        uint32_t idx = (head + cap - 1 - i) % cap;
        sd += ring[idx].dur;
        n++;
        float v = dose[idx];
        if (isfinite(v)) {
            float y = v - c;
            float t = s + y;
            c = (t - s) - y;
            s = t;
            sdd += ring[idx].dur;
            nd++;
        }
    }
    *sum_ws = s;
    *sum_dur_dose = sdd;
    *n_dose = nd;
    return n;
}
static inline uint32_t mon_window_sum(const monitor_sample_t *ring, uint32_t cap, uint32_t head,
                                      uint32_t count, uint32_t w_sec,
                                      uint64_t *sum_counts, uint32_t *sum_dur)
{
    uint64_t sc = 0;
    uint32_t sd = 0, n = 0;
    for (uint32_t i = 0; i < count && sd < w_sec; i++) {
        const monitor_sample_t *s = &ring[(head + cap - 1 - i) % cap];
        sc += s->counts;
        sd += s->dur;
        n++;
    }
    *sum_counts = sc;
    *sum_dur = sd;
    return n;
}
