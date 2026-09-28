#pragma once
// D4 (verify-awf4-2026-09-27.md:250, sweep-B задача 1): предел подряд идущих
// HTTPD_SOCK_ERR_TIMEOUT в handle_ota_locked() (main/web_server.c) -- чистая
// проверка границы, host-тестируема без httpd/сети/железа
// (tests/host/test_ota_timeout_budget.c). Число (30) само по себе не здесь --
// OTA_MAX_CONSECUTIVE_TIMEOUTS остаётся в web_server.c рядом с обоснованием
// (живой прогон: ручная заливка переживает 30 подряд таймаутов).
#include <stdbool.h>
#include <stdint.h>

// streak -- счётчик ПОСЛЕ инкремента текущего таймаута (1-based: значение
// после первого таймаута подряд == 1). true -- бюджет исчерпан, оборвать приём.
static inline bool ota_timeout_budget_exceeded(uint32_t streak, uint32_t max_consecutive)
{
    return streak > max_consecutive;
}
