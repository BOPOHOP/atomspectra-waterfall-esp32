// AWF-5 P1-фикс. См. main/ota_busy.h.
#include "ota_busy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_lock;
static ota_busy_owner_t  s_state = OTA_BUSY_NONE;

void ota_busy_init(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();   // app_main(), однопоточно -- без гонки
}

bool ota_busy_acquire(ota_busy_owner_t who)
{
    if (!s_lock) return false;   // ota_busy_init() не вызван -- отказ, не тихий пропуск
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool ok = ota_busy_try_acquire_pure(&s_state, who);
    xSemaphoreGive(s_lock);
    return ok;
}

void ota_busy_release(ota_busy_owner_t who)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    ota_busy_release_pure(&s_state, who);
    xSemaphoreGive(s_lock);
}

bool ota_busy_is_busy(void)
{
    if (!s_lock) return false;   // ota_busy_init() не вызван -- как и до фикса, не блокируем
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool busy = ota_busy_is_busy_pure(s_state);
    xSemaphoreGive(s_lock);
    return busy;
}
