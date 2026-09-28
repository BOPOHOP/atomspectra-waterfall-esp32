#pragma once
#include <stdint.h>
#include <stdbool.h>

// #REC-12 (sweep-A, 1.2.28): пин сегмента на время ЧТЕНИЯ HTTP-слоем (pull-путь
// GET /api/waterfall/segment и потоковый экспорт n42 с flash) — зеркало push-пина
// s_seg_pinned (spectrogram.c, #REC-11-A2): кольцо keep-last и прочие удаляющие пути
// не трогают сегмент, пока его читают.
// Отдельное состояние, а не s_seg_pinned: push-пин живёт под s_fs_lock, который писатель
// держит секундами (fsync/финализация ~1 МБ) — httpd встал бы на нём целиком (#FW-61).
// Здесь состояние крошечное, его защищает спинлок в spectrogram.c.
// Поле unlinking закрывает гонку: удаляющие пути выбирают файл под s_fs_lock и
// отпускают его на время quiet-ожидания и unlink (секунды). Правило симметричное:
// пин не берётся, пока idx удаляется; удаление не начинается, пока idx запинен.
// Удаляющих задач несколько (wf_fs, выгрузчик, httpd) — слотов удаления несколько.

#define WF_PIN_NONE 0xFFFFFFFFu
#define WF_PIN_UNLINK_SLOTS 4

typedef struct {
    uint32_t pull;                             /* segment being read by HTTP layer, or WF_PIN_NONE */
    uint32_t unlinking[WF_PIN_UNLINK_SLOTS];   /* segments being unlinked, WF_PIN_NONE = free slot */
} wf_pull_pin_t;

static inline void wf_pin_init(wf_pull_pin_t *p) {
    p->pull = WF_PIN_NONE;
    for (uint32_t i = 0; i < WF_PIN_UNLINK_SLOTS; ++i) {
        p->unlinking[i] = WF_PIN_NONE;
    }
}

static inline bool wf_pin_is_unlinking(const wf_pull_pin_t *p, uint32_t idx) {
    if (idx == WF_PIN_NONE) return false;
    for (uint32_t i = 0; i < WF_PIN_UNLINK_SLOTS; ++i) {
        if (p->unlinking[i] == idx) return true;
    }
    return false;
}

static inline bool wf_pin_pull_held(const wf_pull_pin_t *p, uint32_t idx) {
    return idx != WF_PIN_NONE && p->pull == idx;
}

/* false: idx==NONE, или уже unlinking, или pull занят ДРУГИМ сегментом */
static inline bool wf_pin_pull_try(wf_pull_pin_t *p, uint32_t idx) {
    if (idx == WF_PIN_NONE) return false;
    if (wf_pin_is_unlinking(p, idx)) return false;
    if (p->pull != WF_PIN_NONE && p->pull != idx) return false;
    p->pull = idx;
    return true;
}

static inline void wf_pin_pull_release(wf_pull_pin_t *p, uint32_t idx) {
    if (idx != WF_PIN_NONE && p->pull == idx) {
        p->pull = WF_PIN_NONE;
    }
}

/* О1 (release-gate-firmware-v1.2.28-code.md): комментарий поправлен —
 * старая редакция обещала false "если уже unlinking", код (:63) отдаёт true
 * (повторный вызов для ТОГО ЖЕ idx — не ошибка, второй удаляющий не должен
 * получать отказ на уже помеченном к удалению файле; закреплено тестом
 * test_sweepA_small.c:151). false: idx==NONE, ИЛИ pull-held, ИЛИ нет
 * свободных слотов unlinking для НОВОГО idx. */
static inline bool wf_pin_unlink_begin(wf_pull_pin_t *p, uint32_t idx) {
    if (idx == WF_PIN_NONE) return false;
    if (wf_pin_pull_held(p, idx)) return false;
    if (wf_pin_is_unlinking(p, idx)) return true;
    for (uint32_t i = 0; i < WF_PIN_UNLINK_SLOTS; ++i) {
        if (p->unlinking[i] == WF_PIN_NONE) {
            p->unlinking[i] = idx;
            return true;
        }
    }
    return false;
}

static inline void wf_pin_unlink_end(wf_pull_pin_t *p, uint32_t idx) {
    if (idx == WF_PIN_NONE) return;
    for (uint32_t i = 0; i < WF_PIN_UNLINK_SLOTS; ++i) {
        if (p->unlinking[i] == idx) {
            p->unlinking[i] = WF_PIN_NONE;
        }
    }
}
