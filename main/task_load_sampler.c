#include "task_load_sampler.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static task_load_snap_t s_prev, s_cur;
static bool s_have_prev;
static int64_t s_prev_us;
static task_load_result_t s_last;
static bool s_have_last;
static int64_t s_last_us;
static uint32_t s_last_window_ms;
static SemaphoreHandle_t s_mtx;
static StaticSemaphore_t s_mtx_buf;
static portMUX_TYPE s_init_mux = portMUX_INITIALIZER_UNLOCKED;

static void lock_init(void)
{
    if (s_mtx == NULL) {
        taskENTER_CRITICAL(&s_init_mux);
        if (s_mtx == NULL) {
            s_mtx = xSemaphoreCreateMutexStatic(&s_mtx_buf);
        }
        taskEXIT_CRITICAL(&s_init_mux);
    }
}

static bool take_snapshot(task_load_snap_t *snap)
{
    UBaseType_t n = uxTaskGetNumberOfTasks();
    TaskStatus_t *arr = malloc((n + 4) * sizeof(TaskStatus_t));
    if (arr == NULL) {
        return false;
    }
    configRUN_TIME_COUNTER_TYPE total_rt = 0;
    UBaseType_t got = uxTaskGetSystemState(arr, n + 4, &total_rt);
    if (got == 0) {
        free(arr);
        return false;
    }
    memset(snap, 0, sizeof *snap);
    snap->total = (uint32_t)total_rt;
    if (got > TASK_LOAD_MAX_TASKS) {
        got = TASK_LOAD_MAX_TASKS;
    }
    for (UBaseType_t i = 0; i < got; i++) {
        snprintf(snap->e[i].name, sizeof snap->e[i].name, "%s", arr[i].pcTaskName);
        snap->e[i].runtime = (uint32_t)arr[i].ulRunTimeCounter;
    }
    snap->n = (int)got;
    free(arr);
    return true;
}

bool task_load_sample(task_load_result_t *out, uint32_t *window_ms)
{
    lock_init();
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    int64_t now = esp_timer_get_time();

    if (s_have_last && (now - s_last_us) < 1000000) {
        *out = s_last;
        *window_ms = s_last_window_ms;
        xSemaphoreGive(s_mtx);
        return true;
    }

    if (!take_snapshot(&s_cur)) {
        xSemaphoreGive(s_mtx);
        return false;
    }

    bool ok = false;
    if (s_have_prev && (now - s_prev_us) <= 3600LL * 1000000LL) {
        ok = task_load_compute(&s_prev, &s_cur, &s_last);
        if (ok) {
            s_have_last = true;
            s_last_us = now;
            s_last_window_ms = (uint32_t)((now - s_prev_us) / 1000);
            *out = s_last;
            *window_ms = s_last_window_ms;
        }
    }

    s_prev = s_cur;
    s_prev_us = now;
    s_have_prev = true;
    xSemaphoreGive(s_mtx);
    return ok;
}

cJSON *task_load_json(void)
{
    task_load_result_t *r = malloc(sizeof *r);
    if (r == NULL) {
        return NULL;
    }
    uint32_t wms = 0;
    bool valid = task_load_sample(r, &wms);

    cJSON *o = cJSON_CreateObject();
    if (o == NULL) {
        free(r);
        return NULL;
    }

    cJSON_AddBoolToObject(o, "valid", valid);
    cJSON_AddNumberToObject(o, "cores", 2);

    if (!valid) {
        free(r);
        return o;
    }

    cJSON_AddNumberToObject(o, "window_ms", wms);

    cJSON *ca = cJSON_AddArrayToObject(o, "core");
    if (ca != NULL) {
        for (int i = 0; i < 2; i++) {
            cJSON *item;
            if (r->core_ok[i]) {
                double val = (double)((int)(r->core[i] * 10.0f + 0.5f)) / 10.0;
                item = cJSON_CreateNumber(val);
            } else {
                item = cJSON_CreateNull();
            }
            if (item != NULL) {
                cJSON_AddItemToArray(ca, item);
            }
        }
    }

    cJSON *ta = cJSON_AddArrayToObject(o, "tasks");
    if (ta != NULL) {
        for (int i = 0; i < r->ntop; i++) {
            cJSON *obj = cJSON_CreateObject();
            if (obj == NULL) {
                break;
            }
            if (cJSON_AddStringToObject(obj, "n", r->top[i].name) == NULL) {
                cJSON_Delete(obj);
                break;
            }
            double pct = (double)((int)(r->top[i].pct * 10.0f + 0.5f)) / 10.0;
            if (cJSON_AddNumberToObject(obj, "p", pct) == NULL) {
                cJSON_Delete(obj);
                break;
            }
            if (!cJSON_AddItemToArray(ta, obj)) {
                cJSON_Delete(obj);
                break;
            }
        }
    }

    free(r);
    return o;
}
