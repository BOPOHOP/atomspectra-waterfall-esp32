#pragma once
/* Host glue stubs (release-gate 1.2.36 pass1 С-2): minimal ESP-IDF surface so that main glue .c files compiles on host. */
typedef int esp_err_t;
#define ESP_OK   0
#define ESP_FAIL (-1)
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_NOT_FOUND   0x105
