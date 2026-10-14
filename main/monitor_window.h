#pragma once
// 1.2.36 #MX-14 (G): SMA главной из кольца платы (host-pure). SMA = Σcounts / Σdur по последним сэмплам кольца,
// пока Σdur < W; сэмпл, на котором Σdur достигает W, включается (решение оператора). head — куда ляжет СЛЕДУЮЩИЙ сэмпл.
#include <stddef.h>
#include <stdint.h>
#include "monitor.h"
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
