#pragma once
#include <stdint.h>
/* Host glue stubs: single-threaded test, semaphores are counters only. */
typedef uint32_t TickType_t;
typedef int BaseType_t;
#define portMAX_DELAY ((TickType_t)0xffffffffu)
#define pdTRUE  1
#define pdFALSE 0
#define pdPASS  1
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
