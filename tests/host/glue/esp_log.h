#pragma once
/* Host glue stubs: logging compiled out, but format and tag are still type-checked (no unused TAG). */
#include <stdio.h>
#define GLUE_LOG(tag, fmt, ...) do { (void)(tag); if (0) printf(fmt, ##__VA_ARGS__); } while (0)
#define ESP_LOGE(tag, fmt, ...) GLUE_LOG(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) GLUE_LOG(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) GLUE_LOG(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) GLUE_LOG(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGV(tag, fmt, ...) GLUE_LOG(tag, fmt, ##__VA_ARGS__)
