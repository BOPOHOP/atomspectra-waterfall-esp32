#pragma once
#include "FreeRTOS.h"
typedef void *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t t);
BaseType_t xSemaphoreGive(SemaphoreHandle_t s);
/* balance of Take/Give over all semaphores (0 = every Take was given back) */
extern int glue_sem_depth;
/* F4.2: hook called from xSemaphoreGive after release (models a concurrent rebuild between critical sections) */
extern void (*glue_on_give)(void);
