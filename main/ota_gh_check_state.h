#pragma once
// LK-07 (1.2.30): проверка релиза на GitHub не выполняется в задаче httpd (HTTPS до 15 с на операцию блокировал веб-сервер).
// GET /api/ota/github/check запускает фоновую задачу и отвечает {"state":"checking"}; страница опрашивает тот же адрес,
// пока не придёт готовый ответ ("state":"done"). Решение «запустить / ждать / отдать готовое» — здесь (host-pure,
// tests/host/test_ota_gh_check_state.c).
#include <stdint.h>

typedef enum { OTA_CHK_IDLE = 0, OTA_CHK_RUNNING = 1, OTA_CHK_DONE = 2 } ota_chk_state_t;
typedef enum { OTA_CHK_ACT_START = 0, OTA_CHK_ACT_WAIT = 1, OTA_CHK_ACT_SERVE = 2 } ota_chk_act_t;

// st — текущее состояние; done_age_ms — сколько прошло с готовности результата (для DONE); keep_ms — срок жизни готового ответа
// (несколько вкладок/опросов получают один результат, потом следующий GET запускает новую проверку).
static inline ota_chk_act_t ota_chk_decide(ota_chk_state_t st, uint32_t done_age_ms, uint32_t keep_ms)
{
    if (st == OTA_CHK_RUNNING) return OTA_CHK_ACT_WAIT;
    if (st == OTA_CHK_DONE && done_age_ms < keep_ms) return OTA_CHK_ACT_SERVE;
    return OTA_CHK_ACT_START;
}
