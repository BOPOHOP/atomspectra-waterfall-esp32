// #RST-TAIL (1.2.30): решение «писать ли строку водопада на этом тике» (main/wf_tail_plan.h).
#include "wf_tail_plan.h"
#include "test_util.h"

static void test_tail_normal_tick(void);
static void test_tail_forced(void);

void test_wf_tail_plan(void)
{
    test_tail_normal_tick();
    test_tail_forced();
}

static void test_tail_normal_tick(void)
{
    CHECK(!wf_tail_should_row(false, 102, 100, 5));   // 2 с из 5 — ждём
    CHECK(!wf_tail_should_row(false, 104, 100, 5));   // 4 с — ждём
    CHECK( wf_tail_should_row(false, 105, 100, 5));   // ровно iv — пишем
    CHECK( wf_tail_should_row(false, 110, 100, 5));   // больше iv — пишем
    CHECK( wf_tail_should_row(false, 3, 100, 5));     // время прибора откатилось (Сброс) — пишем
    CHECK(!wf_tail_should_row(false, 100, 100, 5));   // время стоит — ждём
}

static void test_tail_forced(void)
{
    CHECK( wf_tail_should_row(true, 101, 100, 5));    // прошла 1 с — хвост есть, пишем вне очереди
    CHECK( wf_tail_should_row(true, 104, 100, 5));    // 4 с — пишем
    CHECK(!wf_tail_should_row(true, 100, 100, 5));    // время не шло — хвоста нет (dur=0 не пишем)
    CHECK(!wf_tail_should_row(true, 3, 100, 5));      // время откатилось — строка от нуля, не хвост
    CHECK( wf_tail_should_row(true, 105, 100, 1));    // iv не влияет на форсированную строку
}
