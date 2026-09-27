#pragma once
// AWF-5: обновление прошивки с GitHub -- публичный API фоновой задачи и
// HTTP-обработчиков (main/web_server.c). Реализация -- ota_github_client.c.
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

// Минимальная версия релиза, устанавливаемого этим механизмом (у более
// старых релизов нет самого механизма OTA-по-Wi-Fi -- ставить их с GitHub
// нельзя, только по USB). "firmware-v1.2.26-rc1" -- первый релиз с AWF-5.
#define OTA_GH_MIN_INSTALLABLE_TAG "firmware-v1.2.26-rc1"
#define OTA_GH_REPO_OWNER "VibeEngineering-LLC"
#define OTA_GH_REPO_NAME  "atomspectra-waterfall-esp32"

typedef enum {
    OTA_GH_ST_IDLE = 0,
    OTA_GH_ST_CHECKING,
    OTA_GH_ST_DOWNLOADING,
    OTA_GH_ST_VERIFYING,
    OTA_GH_ST_INSTALLING,
    OTA_GH_ST_DONE,
    OTA_GH_ST_ERROR
} ota_gh_state_t;

typedef struct {
    ota_gh_state_t state;
    uint32_t bytes;
    uint32_t total;
    char error[96];
} ota_gh_progress_t;

// Вызывается один раз из app_main().
void ota_gh_client_init(void);

// NVS-тумблер "получать предрелизы" (default off).
bool ota_gh_prerelease_channel_get(void);
esp_err_t ota_gh_prerelease_channel_set(bool enabled);

// GET .../releases?per_page=10 синхронно (вызывающий -- httpd-обработчик).
// Пишет JSON {"current","latest","newer","installable","reason"} в out
// (cap байт, всегда завершён нулём при cap>0). Кэширует найденный релиз
// для последующего ota_gh_install_start().
esp_err_t ota_gh_check(char *out_json, size_t out_cap);

// Запускает фоновую задачу установки (идемпотентно). Использует кэш
// последнего ota_gh_check(); если кэша нет -- задача сама перезапросит check.
esp_err_t ota_gh_install_start(void);

// Текущий прогресс для GET /api/ota/github/progress (thread-safe копия).
ota_gh_progress_t ota_gh_get_progress(void);
