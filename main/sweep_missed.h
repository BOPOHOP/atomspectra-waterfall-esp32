#ifndef SWEEP_MISSED_H
#define SWEEP_MISSED_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    bool     have;      /* at least one commit seen */
    uint32_t last_ms;   /* time of the last commit, milliseconds, wrapping uint32 */
    uint32_t done;      /* missed intervals accumulated at commits */
} sweep_missed_t;

/* Число пропущенных интервалов в разрыве elapsed_ms */
static inline uint32_t sweep_gap_intervals(uint32_t elapsed_ms) {
    uint64_t r = ((uint64_t)elapsed_ms + 500) / 1000;
    return (r > 0) ? (uint32_t)(r - 1) : 0;
}

/* Фиксация коммита: накопление пропущенных интервалов */
static inline void sweep_missed_commit(sweep_missed_t *s, uint32_t now_ms) {
    if (s->have) {
        s->done += sweep_gap_intervals(now_ms - s->last_ms);
    }
    s->have = true;
    s->last_ms = now_ms;
}

/* Чтение текущего количества пропущенных интервалов */
static inline uint32_t sweep_missed_read(const sweep_missed_t *s, uint32_t now_ms) {
    if (!s->have) return s->done;   /* нет базы (до первого коммита или после сброса): только накопленное */
    return s->done + sweep_gap_intervals(now_ms - s->last_ms);
}

/* Сброс состояния: пауза до первого коммита не учитывается */
static inline void sweep_missed_forget(sweep_missed_t *s) {
    s->have = false;
}

#endif
