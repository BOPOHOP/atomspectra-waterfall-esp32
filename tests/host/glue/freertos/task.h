#pragma once
#include "FreeRTOS.h"
void vTaskDelay(TickType_t t);
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, uint32_t stack, void *arg,
                                   int prio, void *handle, int core);
