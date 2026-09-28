// sweep-A 1.2.28: P-009 (бюджет HEAVY-гейта), #REC-12 (пин чтения), P-042 (seg_seq).
#include "http_gate_budget.h"
#include "wf_seg_pin.h"
#include "wf_seg_seq.h"
#include "test_util.h"
#include <stdint.h>
#include <stdbool.h>

/* Набор тестов для функции http_gate_wait_budget_ms и макросов */
void http_gate_budget_suite(void) {
    /* Проверка ограничения по (timeout - slack) */
    CHECK(http_gate_wait_budget_ms(700, 3, 3000, 1000) == 2000);

    /* Проверка применения маржи, если результат меньше ограничения */
    CHECK(http_gate_wait_budget_ms(500, 3, 3000, 1000) == 1500);

    /* Маржа равна 1, результат равен bg_hold_ms */
    CHECK(http_gate_wait_budget_ms(700, 1, 3000, 1000) == 700);

    /* Timeout равен slack, ограничение равно 0 */
    CHECK(http_gate_wait_budget_ms(700, 3, 1000, 1000) == 0);

    /* Timeout меньше slack, ограничение равно 0 (без переполнения беззнакового типа) */
    CHECK(http_gate_wait_budget_ms(700, 3, 900, 1000) == 0);

    /* Проверка отсутствия переполнения при умножении: 3 * 0x55555556 должно вычисляться в 64 бита */
    CHECK(http_gate_wait_budget_ms(0x55555556u, 3, 3000, 1000) == 2000);

    /* Проверка значения макроса HTTP_GATE_WAIT_MS */
    CHECK(HTTP_GATE_WAIT_MS == 2000);

    /* P-009: время ожидания должно быть больше времени сбоя предыдущего запроса (328 мс) */
    CHECK(HTTP_GATE_WAIT_MS > 328);

    /* Время ожидания должно быть меньше таймаута сокета */
    CHECK(HTTP_GATE_WAIT_MS < HTTPD_SOCK_TIMEOUT_MS);
}

/* Набор тестов для механизма pin/unlink сегментов */
void wf_seg_pin_suite(void) {
    /* Блок 1: Инициализация и проверка начального состояния */
    {
        wf_pull_pin_t p;
        wf_pin_init(&p);
        CHECK(p.pull == WF_PIN_NONE);
        for (uint32_t i = 0; i < WF_PIN_UNLINK_SLOTS; ++i) {
            CHECK(p.unlinking[i] == WF_PIN_NONE);
        }
        CHECK(!wf_pin_pull_held(&p, 5));
        CHECK(!wf_pin_is_unlinking(&p, 5));
    }

    /* Блок 2: Pin/Unpin логика */
    {
        wf_pull_pin_t p;
        wf_pin_init(&p);

        /* Попытка зафиксировать слот 7 должна пройти */
        CHECK(wf_pin_pull_try(&p, 7));
        CHECK(wf_pin_pull_held(&p, 7));
        CHECK(!wf_pin_pull_held(&p, 8));

        /* Повторная фиксация того же слота должна пройти */
        CHECK(wf_pin_pull_try(&p, 7));

        /* Попытка зафиксировать другой слот (8) должна провалиться, так как 7 занят */
        CHECK(!wf_pin_pull_try(&p, 8));
        CHECK(wf_pin_pull_held(&p, 7));

        /* Попытка освободить слот 8 не должна повлиять на слот 7 */
        wf_pin_pull_release(&p, 8);
        CHECK(wf_pin_pull_held(&p, 7));

        /* Освобождение слота 7 должно сделать его свободным */
        wf_pin_pull_release(&p, 7);
        CHECK(!wf_pin_pull_held(&p, 7));
        CHECK(p.pull == WF_PIN_NONE);

        /* Теперь слот 8 должен быть доступен для фиксации */
        CHECK(wf_pin_pull_try(&p, 8));
    }

    /* Блок 3: WF_PIN_NONE не может быть зафиксирован или отсоединен */
    {
        wf_pull_pin_t p;
        wf_pin_init(&p);
        CHECK(!wf_pin_pull_try(&p, WF_PIN_NONE));
        CHECK(!wf_pin_unlink_begin(&p, WF_PIN_NONE));
        CHECK(!wf_pin_pull_held(&p, WF_PIN_NONE));
        CHECK(!wf_pin_is_unlinking(&p, WF_PIN_NONE));
    }

    /* Блок 4: Зафиксированный слот блокирует отсоединение */
    {
        wf_pull_pin_t p;
        wf_pin_init(&p);

        /* Фиксируем слот 3 */
        wf_pin_pull_try(&p, 3);

        /* Отсоединение слота 3 должно провалиться */
        CHECK(!wf_pin_unlink_begin(&p, 3));
        CHECK(!wf_pin_is_unlinking(&p, 3));

        /* Отсоединение другого слота (4) должно пройти */
        CHECK(wf_pin_unlink_begin(&p, 4));

        /* Освобождаем слот 3 */
        wf_pin_pull_release(&p, 3);

        /* Теперь отсоединение слота 3 должно пройти */
        CHECK(wf_pin_unlink_begin(&p, 3));
    }

    /* Блок 5: Отсоединяемый слот блокирует фиксацию */
    {
        wf_pull_pin_t p;
        wf_pin_init(&p);

        /* Начинаем отсоединение слота 9 */
        CHECK(wf_pin_unlink_begin(&p, 9));
        CHECK(wf_pin_is_unlinking(&p, 9));

        /* Попытка зафиксировать слот 9 должна провалиться */
        CHECK(!wf_pin_pull_try(&p, 9));
        CHECK(p.pull == WF_PIN_NONE);

        /* Завершаем отсоединение слота 9 */
        wf_pin_unlink_end(&p, 9);
        CHECK(!wf_pin_is_unlinking(&p, 9));

        /* Теперь фиксация слота 9 должна пройти */
        CHECK(wf_pin_pull_try(&p, 9));
    }

    /* Блок 6: Управление слотами отсоединения (максимум WF_PIN_UNLINK_SLOTS) */
    {
        wf_pull_pin_t p;
        wf_pin_init(&p);

        /* Начинаем отсоединение 4 слотов */
        CHECK(wf_pin_unlink_begin(&p, 10));
        CHECK(wf_pin_unlink_begin(&p, 11));
        CHECK(wf_pin_unlink_begin(&p, 12));
        CHECK(wf_pin_unlink_begin(&p, 13));

        /* Пятый слот должен провалиться (слотов больше нет) */
        CHECK(!wf_pin_unlink_begin(&p, 14));

        /* Повторное начало отсоединения уже отмеченного слота должно пройти (не занимает новый слот) */
        CHECK(wf_pin_unlink_begin(&p, 11));

        /* Завершаем отсоединение слота 12, освобождая слот */
        wf_pin_unlink_end(&p, 12);

        /* Теперь пятый слот (14) должен быть доступен */
        CHECK(wf_pin_unlink_begin(&p, 14));

        /* Проверяем статус отсоединения */
        CHECK(wf_pin_is_unlinking(&p, 10));
        CHECK(wf_pin_is_unlinking(&p, 11));
        CHECK(!wf_pin_is_unlinking(&p, 12));
        CHECK(wf_pin_is_unlinking(&p, 13));
        CHECK(wf_pin_is_unlinking(&p, 14));
    }

    /* Блок 7: Завершение отсоединения неотмеченного слота не влияет на другие */
    {
        wf_pull_pin_t p;
        wf_pin_init(&p);

        /* Начинаем отсоединение слота 20 */
        CHECK(wf_pin_unlink_begin(&p, 20));

        /* Завершаем отсоединение слота 21 (который не был отмечен) */
        wf_pin_unlink_end(&p, 21);

        /* Слот 20 должен оставаться в состоянии отсоединения */
        CHECK(wf_pin_is_unlinking(&p, 20));
    }
}

/* Набор тестов для функций работы с последовательностями сегментов */
void wf_seg_seq_suite(void) {
    /* Сворачивание списка заголовков: {1981, 0, 1983, 1982, 5}, начальный acc = 0 */
    uint32_t acc = 0;
    acc = wf_seg_seq_fold_max(acc, 1981);
    acc = wf_seg_seq_fold_max(acc, 0);
    acc = wf_seg_seq_fold_max(acc, 1983);
    acc = wf_seg_seq_fold_max(acc, 1982);
    acc = wf_seg_seq_fold_max(acc, 5);
    CHECK(acc == 1983);

    /* Сворачивание списка из нулей: {0, 0} */
    acc = 0;
    acc = wf_seg_seq_fold_max(acc, 0);
    acc = wf_seg_seq_fold_max(acc, 0);
    CHECK(acc == 0);

    /* Восстановление при стертых NVS (P-042): берем flash_max_seq */
    CHECK(wf_seg_seq_resume(0, 1983) == 1983);

    /* Восстановление при актуальных NVS: оставляем nvs_seq */
    CHECK(wf_seg_seq_resume(2000, 1983) == 2000);

    /* Восстановление при устаревших NVS: берем flash_max_seq */
    CHECK(wf_seg_seq_resume(5, 1983) == 1983);

    /* Восстановление при пустой флешке: оставляем nvs_seq */
    CHECK(wf_seg_seq_resume(7, 0) == 7);

    /* Восстановление при обоих нулях */
    CHECK(wf_seg_seq_resume(0, 0) == 0);

    /* Проверка следующего номера после восстановления (seg_open_new делает ++ перед созданием заголовка) */
    uint32_t next = wf_seg_seq_resume(0, 1983) + 1;
    CHECK(next == 1984);
    CHECK(next != 1);
}
