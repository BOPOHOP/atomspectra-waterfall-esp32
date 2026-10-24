#pragma once
// #GZ-T (1.2.37-rc90, ТЕСТ): сжатие gzip тела /api/spectrum.json?channels=N (v2; v1 и /api/spectrum не сжимаются) ОТДЕЛЬНОЙ задачей (стек в PSRAM, ядро 0).
// Обработчик httpd сжатие НЕ выполняет: отдаёт готовое сжатое тело текущего поколения кэша спектра либо прежний ответ.
// По умолчанию выключено (gzip_level 0): задача не создаётся, памяти не берётся.
#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"

struct cJSON;

// Подключить крючки к кэшу спектра. Вызывается один раз из web_server_start до httpd_start. Памяти не берёт.
void spectrum_gz_init(void);

// Применить уровень из boot_config_gzip_level(): создать задачу (первое включение) и уведомить её. Только из задачи httpd
// (POST /api/boot-config и каждый запрос спектра).
void spectrum_gz_sync(void);

// Поля gz_* для /api/system.
void spectrum_gz_add_metrics(struct cJSON *root);
