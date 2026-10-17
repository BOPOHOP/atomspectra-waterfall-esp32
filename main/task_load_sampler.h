#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"
#include "task_load.h"      /* pure part: task_load_snap_t, task_load_result_t, task_load_compute() (already written) */

/* Загрузка за интервал между двумя ВЫЗОВАМИ (не за секунду): возвращает true и заполняет *out, *window_ms - длительность окна по часам esp_timer.
   false - окна ещё нет (первый вызов после загрузки только запоминает базу) или оно старше 3600 с. Вызовы чаще 1 раза в секунду
   возвращают результат прошлого вычисления (окно не дробится). Потокобезопасно (мьютекс). */
bool task_load_sample(task_load_result_t *out, uint32_t *window_ms);

/* Объект для /api/status: {"valid":bool,"window_ms":N,"cores":2,"core":[p0,p1],"tasks":[{"n":"name","p":pct},...]}.
   core[i] = null, если простаивающая задача ядра не найдена; при valid=false остаются только "valid":false и "cores":2.
   Возвращает новый объект cJSON (владелец - вызывающий) или NULL при нехватке памяти. */
cJSON *task_load_json(void);
