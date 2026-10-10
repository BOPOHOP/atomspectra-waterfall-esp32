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
