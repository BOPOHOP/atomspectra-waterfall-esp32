// 1.2.31: новая сессия платы после Сброса непустого спектра (main/session_plan.h).
#include "session_plan.h"
#include "test_util.h"
#include <stdint.h>
#include <stdbool.h>

static void test_reset_opens_on_data(void);
static void test_reset_empty_no_session(void);
static void test_need_bump(void);
static void test_need_bump_disabled(void);
static void test_apply_success(void);
static void test_apply_failure_keeps(void);
static void test_apply_not_monotonic(void);
static void test_snap_stale(void);
static void test_scenario_operator(void);

void test_session_plan(void)
{
    test_reset_opens_on_data();
    test_reset_empty_no_session();
    test_need_bump();
    test_need_bump_disabled();
    test_apply_success();
    test_apply_failure_keeps();
    test_apply_not_monotonic();
    test_snap_stale();
    test_scenario_operator();
}

static void test_reset_opens_on_data(void)
{
    CHECK(session_reset_opens(true, 0));
    CHECK(session_reset_opens(false, 5));
    CHECK(session_reset_opens(true, 3600));
}

static void test_reset_empty_no_session(void)
{
    CHECK(!session_reset_opens(false, 0));
}

static void test_need_bump(void)
{
    sess_sched_t s = { .sess = 7, .seen_req = 2 };
    CHECK(session_need_bump(&s, 3));
    CHECK(!session_need_bump(&s, 2));
}

static void test_need_bump_disabled(void)
{
    sess_sched_t s = { .sess = 0, .seen_req = 0 };
    CHECK(!session_need_bump(&s, 1));
}

static void test_apply_success(void)
{
    sess_sched_t s = { .sess = 7, .seq = 5, .seen_req = 1, .due_us = 123456, .fail_streak = 4 };
    session_apply_bump(&s, 2, 8);
    CHECK(s.sess == 8);
    CHECK(s.seq == 0);
    CHECK(s.due_us == 0);
    CHECK(s.fail_streak == 0);
    CHECK(s.seen_req == 2);
}

static void test_apply_failure_keeps(void)
{
    sess_sched_t s = { .sess = 7, .seq = 5, .seen_req = 1, .due_us = 123456, .fail_streak = 4 };
    session_apply_bump(&s, 2, 0);
    CHECK(s.sess == 7);
    CHECK(s.seq == 5);
    CHECK(s.seen_req == 2);
    CHECK(!session_need_bump(&s, 2));
}

static void test_apply_not_monotonic(void)
{
    sess_sched_t s = { .sess = 7, .seq = 5, .seen_req = 1 };
    session_apply_bump(&s, 2, 7);
    CHECK(s.sess == 7 && s.seq == 5);
    session_apply_bump(&s, 3, 6);
    CHECK(s.sess == 7 && s.seq == 5);
}

static void test_snap_stale(void)
{
    CHECK(!session_snap_current(4, 3));
    CHECK(session_snap_current(3, 3));
}

/* Сквозной сценарий оператора: загрузка sess=9, два снимка, Стоп, Сброс, Старт. */
static void test_scenario_operator(void)
{
    sess_sched_t s = { .sess = 9 };
    uint32_t req = 0;           /* s_sess_req в spectrum.c */
    s.seq += 2;                 /* bk_9_1, bk_9_2 */
    if (session_reset_opens(true, 120)) req++;      /* Сброс непустого */
    CHECK(session_need_bump(&s, req));
    session_apply_bump(&s, req, 10);
    CHECK(s.sess == 10 && s.seq == 0);
    s.seq++;                    /* следующий снимок: bk_10_1 */
    CHECK(s.sess == 10 && s.seq == 1);
    /* досылка -rst на уже ПУСТОМ спектре: второго бампа нет */
    if (session_reset_opens(false, 0)) req++;
    CHECK(!session_need_bump(&s, req));
    CHECK(s.sess == 10);
    /* повторный Сброс непустого даёт ещё ровно одну сессию */
    if (session_reset_opens(true, 5)) req++;
    CHECK(session_need_bump(&s, req));
    session_apply_bump(&s, req, 11);
    CHECK(s.sess == 11 && s.seq == 0);
    CHECK(!session_need_bump(&s, req));
}
