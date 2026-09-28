#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "spectrogram.h"   /* wf_seg_reg_t */

typedef struct { uint32_t idx, g0, g1; } wf_seg_range_snap_t;

/**
 * Восстановление диапазонов строк для уцелевших сегментов после сбоя очистки.
 * При неудачной очистке кольцо PSRAM не сбрасывается, но реестр пересобирается с диска,
 * теряя информацию о глобальных номерах строк (g0, g1). Эта функция ищет запись в
 * сохраненном снимке реестра (prev) по индексу сегмента. Если сегмент был частью
 * текущей сессии (g1 > g0), его диапазон восстанавливается: g0 берется из снимка,
 * а g1 вычисляется как g0 + rows (количество строк в файле на диске). Это позволяет
 * корректно экспортировать данные без пересчета диапазонов подряд от нуля.
 */
static inline bool wf_seg_restore_range(const wf_seg_range_snap_t *prev, int n, uint32_t idx,
                                        uint32_t rows, uint32_t *g0, uint32_t *g1) {
    if (rows == 0) {
        *g0 = 0;
        *g1 = 0;
        return false;
    }

    for (int i = 0; i < n; ++i) {
        if (prev[i].idx == idx) {
            // Если сегмент был из прежней сессии или невалиден, сбрасываем диапазон
            if (prev[i].g1 <= prev[i].g0) {
                *g0 = 0;
                *g1 = 0;
                return false;
            }
            // Восстанавливаем g0 из снимка, g1 вычисляем по размеру файла
            *g0 = prev[i].g0;
            *g1 = prev[i].g0 + rows;
            return true;
        }
    }

    // Сегмент не найден в снимке
    *g0 = 0;
    *g1 = 0;
    return false;
}

/* У6 (раунд 3): запись реестра для сегмента, найденного на диске при пересборке, — та же
 * цепочка, что reg_add → reg_mark_finalized → (диапазон восстановлен) reg_set_range →
 * reg_update_open, без реестра и лока (host-тест). default_bytes — заготовка reg_add. */
static inline void wf_seg_rebuild_entry(wf_seg_reg_t *e, uint32_t idx, uint32_t seg_seq,
        int64_t started_at, uint32_t rows, uint32_t bytes, uint32_t default_bytes,
        const wf_seg_range_snap_t *prev, int n_prev)
{
    uint32_t g0 = 0, g1 = 0;
    bool had_range = wf_seg_restore_range(prev, n_prev, idx, rows, &g0, &g1);
    e->idx = idx;
    e->seg_seq = seg_seq;
    e->started_at = started_at;
    e->rows = rows;
    e->bytes = bytes ? bytes : default_bytes;
    e->g0 = 0;
    e->g1 = 0;
    e->finalized = true;
    e->valid = true;
    if (had_range) {
        /* Н3: сегмент текущей сессии — прежний g0, g1 по строкам файла */
        e->g0 = g0;
        e->bytes = bytes;
        e->g1 = g1;
    }
}
