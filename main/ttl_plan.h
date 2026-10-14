#pragma once
#include <stdbool.h>
#include <stdint.h>
/* 1.2.36 H5 (C): срок жизни кэшированного значения (esp_timer, мкс). at_us == 0 — «ещё не читали».
 * Одна проверка для медленной части /api/system (обходы кучи, стеки задач) и для esp_littlefs_info. */
static inline bool ttl_expired(int64_t at_us, int64_t now_us, int64_t ttl_us)
{
    return at_us == 0 || now_us - at_us > ttl_us;
}
