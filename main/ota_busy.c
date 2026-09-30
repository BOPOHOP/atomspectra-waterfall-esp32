// AWF-5 P1-фикс. См. main/ota_busy.h.
#include "ota_busy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

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

// #OTA-VR: см. ota_busy.h. Между повторами 200 мс — сдвинуть проверку относительно записи flash соседями.
typedef struct { const esp_partition_t *p; int n; } boot_ctx_t;
static int set_boot_cb(void *c)
{
    boot_ctx_t *b = c;
    if (b->n++) vTaskDelay(pdMS_TO_TICKS(200));
    return esp_ota_set_boot_partition(b->p);
}

esp_err_t ota_set_boot_verified(const esp_partition_t *p)
{
    boot_ctx_t b = { p, 0 };
    int calls = 0;
    esp_err_t err = ota_boot_retry_pure(set_boot_cb, &b, 3, ESP_ERR_OTA_VALIDATE_FAILED, &calls);
    if (calls > 1) ESP_LOGW("ota_busy", "#OTA-VR: image verify retried, attempts=%d result=%s", calls, esp_err_to_name(err));
    return err;
}

bool ota_busy_is_busy(void)
{
    if (!s_lock) return false;   // ota_busy_init() не вызван -- как и до фикса, не блокируем
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool busy = ota_busy_is_busy_pure(s_state);
    xSemaphoreGive(s_lock);
    return busy;
}
