#pragma once
#include <stdbool.h>
#include <stdint.h>
/* 1.2.36 P1 (E): не переписывать неизменный current.bin (только одноразовый путь, прибора нет).
 * Критерий: CRC32 снимка и s_reset_gen на момент снимка равны значениям ПОСЛЕДНЕЙ успешной записи этим же путём.
 * have=false (старт, Сброс, отказ записи, порционный путь, consume_abort) -> первая запись выполняется всегда. */
typedef struct { bool have; uint32_t crc; uint32_t gen; } as_last_t;

static inline bool autosave_unchanged(const as_last_t *l, uint32_t crc, uint32_t gen)
{
    return l->have && l->crc == crc && l->gen == gen;
}

/* С-1 (release-gate-1.2.36-code-pass1): пропуск неизменного снимка выходил из spectrum_autosave() раньше снятия reset.mark,
 * а повтор неудачного unlink метки держался именно на следующем автосейве -> метка оставалась навсегда, на старте спектр
 * удалялся. Шаг пропуска обязан сам повторить снятие метки (mark_retry = reset_mark_clear_if_current: снимает только при
 * gen снимка == текущему s_reset_gen и метке на flash). Политика пропуска прежняя (autosave_unchanged). */
typedef void (*as_mark_retry_fn)(uint32_t gen);

static inline bool autosave_skip_step(const as_last_t *l, uint32_t crc, uint32_t gen, as_mark_retry_fn mark_retry)
{
    if (!autosave_unchanged(l, crc, gen)) return false;
    mark_retry(gen);
    return true;
}
