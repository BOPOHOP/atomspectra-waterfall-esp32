#pragma once
/* 1.2.39-t1: чистая проверка «кадр CMD_TEXT от TCP-клиента — сброс прибора?» (host-тест
 * tests/host/test_tcp_reset.c). Данные кадра (без байта команды) — текст команды с
 * НЕОБЯЗАТЕЛЬНЫМ завершающим \0: BecqMoni шлёт «-rst» без \0 (03 2D 72 73 74).
 * Команда = текст до первого \0 либо до конца кадра. cap — ёмкость буфера data: при
 * кадре без \0 терминатор ставится на месте (кадр уже разобран декодером; сам кадр,
 * уходящий прибору, не меняется — он идёт из сырого буфера recv). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "acq_intent.h"

static inline bool tcp_text_frame_is_reset(uint8_t *data, size_t len, size_t cap)
{
    if (len == 0) return false;
    if (data[len - 1] != '\0') {
        if (len >= cap) return false;   /* нет места под терминатор: не гадаем */
        data[len] = '\0';
    }
    return cmd_is_device_reset((const char *)data);
}
