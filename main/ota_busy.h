#pragma once
// AWF-5 P1-фикс (verify-awf5-github-ota-2026-09-27.md разд.2.4): общий замок
// между ручной POST /api/ota (main/web_server.c) и GitHub-install_task
// (main/ota_github_client.c) -- оба независимо брали
// esp_ota_get_next_update_partition() и могли писать в один слот одновременно.
// Оба пути обязаны ota_busy_acquire() ДО esp_ota_begin() и ota_busy_release()
// после esp_ota_end()/esp_ota_abort(); занято -> НЕ звать esp_ota_begin вовсе.
#include <stdbool.h>

typedef enum { OTA_BUSY_NONE = 0, OTA_BUSY_MANUAL, OTA_BUSY_GITHUB } ota_busy_owner_t;

// Чистая логика (host-тест tests/host/test_ota_busy.c): пробует занять
// свободное состояние. true+*state=who при успехе; false, *state не тронут,
// если уже занято (в т.ч. тем же who -- повторный вход не разрешён, single-
// flight per owner тоже держится этим же примитивом).
static inline bool ota_busy_try_acquire_pure(ota_busy_owner_t *state, ota_busy_owner_t who)
{
    if (*state != OTA_BUSY_NONE) return false;
    *state = who;
    return true;
}

// Снимает занятость, только если владелец совпадает (чужой release -- no-op,
// безопасно звать из общего error-path даже когда acquire не вызывался).
static inline void ota_busy_release_pure(ota_busy_owner_t *state, ota_busy_owner_t who)
{
    if (*state == who) *state = OTA_BUSY_NONE;
}

// FreeRTOS-обёртки (main/ota_busy.c) -- реальная точка вызова из обоих путей.
bool ota_busy_acquire(ota_busy_owner_t who);
void ota_busy_release(ota_busy_owner_t who);
void ota_busy_init(void);   // один раз из app_main(), до web_server_init()
