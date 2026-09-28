// М3 (release-gate-firmware-v1.2.28-code.md): host-тест на wf_seg_rebuild_range.h
// (арифметика) + wf_export_plan.h (последствие для плана выдачи n42).
#include "wf_seg_rebuild_range.h"
#include "wf_export_plan.h"
#include "test_util.h"
#include <string.h>

static wf_seg_reg_t m3_R(uint32_t idx, uint32_t rows, uint32_t g0, uint32_t g1)
{
    wf_seg_reg_t r; memset(&r, 0, sizeof r);
    r.idx = idx; r.rows = rows; r.finalized = true; r.valid = true;
    r.g0 = g0; r.g1 = g1;
    return r;
}

void wf_seg_rebuild_range_suite(void)
{
    // Арифметика: 3 сегмента по возрастанию idx, кумулятивная сумма строк.
    uint32_t rows[3] = { 40, 100, 16 };
    uint32_t g0[3], g1[3];
    wf_seg_rebuild_ranges(rows, 3, g0, g1);
    CHECK(g0[0] == 0   && g1[0] == 40);
    CHECK(g0[1] == 40  && g1[1] == 140);
    CHECK(g0[2] == 140 && g1[2] == 156);

    // n == 0 (нет сегментов) — не падает, ничего не пишет.
    wf_seg_rebuild_ranges(rows, 0, g0, g1);

    // М3, симптом ДО фикса: reg_add() даёт g0=g1=0 — сегмент "старый", план
    // держит и SEG (rows из файла), и RING [0,100) того же диапазона — дубль.
    wf_exp_step_t plan[16];
    wf_seg_reg_t buggy = m3_R(0, 100, 0, 0);
    int n = wf_exp_plan(&buggy, 1, 0, 100, plan, 16);
    CHECK(n == 2);
    CHECK(plan[0].kind == WF_EXP_STEP_SEG && plan[0].a == 0);
    CHECK(plan[1].kind == WF_EXP_STEP_RING && plan[1].a == 0 && plan[1].b == 100);

    // После фикса (g0/g1 из wf_seg_rebuild_ranges для одного сегмента 100 строк):
    // сегмент "текущий", кольцевой разрыв перед ним пуст — дубля нет.
    uint32_t one_row[1] = { 100 };
    wf_seg_rebuild_ranges(one_row, 1, g0, g1);
    wf_seg_reg_t fixed = m3_R(0, 100, g0[0], g1[0]);
    n = wf_exp_plan(&fixed, 1, 0, 100, plan, 16);
    CHECK(n == 1);
    CHECK(plan[0].kind == WF_EXP_STEP_SEG && plan[0].a == 0);
}
