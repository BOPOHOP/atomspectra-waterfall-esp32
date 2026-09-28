#pragma once
#include <stdint.h>

/* М3: кумулятивно назначить [g0,g1) оставшимся после неудачной очистки
 * сегментам, по возрастанию idx (как seg_open_new при первичной записи).
 * Без этого reg_add() оставляет g0=g1=0, wf_exp_plan считает сегмент
 * "старым" и отдаёт его целиком, а кольцо PSRAM (не сброшено на пути
 * неудачной очистки) накрывает те же строки — дубль (spectrogram.c
 * seg_rebuild_counters_from_disk). rows[] — строки сегментов УЖЕ по
 * возрастанию idx; g0_out/g1_out — результат, тот же порядок. */
static inline void wf_seg_rebuild_ranges(const uint32_t *rows, int n,
                                          uint32_t *g0_out, uint32_t *g1_out)
{
    uint32_t g = 0;
    for (int i = 0; i < n; i++) {
        g0_out[i] = g;
        g += rows[i];
        g1_out[i] = g;
    }
}
