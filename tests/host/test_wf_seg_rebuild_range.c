#include "wf_seg_rebuild_range.h"
#include "wf_export_plan.h"
#include "test_util.h"
#include <string.h>
#include <stdio.h>

// Вспомогательная функция для создания записи реестра
static wf_seg_reg_t n3_R(uint32_t idx, uint32_t rows, uint32_t g0, uint32_t g1) {
    wf_seg_reg_t r;
    memset(&r, 0, sizeof(r));
    r.idx = idx;
    r.rows = rows;
    r.g0 = g0;
    r.g1 = g1;
    r.finalized = true;
    r.valid = true;
    return r;
}

// Покрытие строк планом экспорта. reg — ИСТИННОЕ содержимое сегментов (truth), не то, что
// записано в реестре: иначе неверно размеченный сегмент «покрывает» чужие строки и дыра не видна.
static int n3_rows_out(const wf_exp_step_t *p, int n, const wf_seg_reg_t *reg, int nreg,
                       uint32_t *cover, uint32_t total) {
    int old_segs = 0;

    for (int i = 0; i < n; ++i) {
        if (p[i].kind == WF_EXP_STEP_RING) {
            // Шаги кольца: покрываем диапазон [a, b)
            uint32_t start = p[i].a;
            uint32_t end = p[i].b;
            for (uint32_t k = start; k < end && k < total; ++k) {
                cover[k]++;
            }
        } else if (p[i].kind == WF_EXP_STEP_SEG) {
            // Шаги сегмента: ищем сегмент по idx
            uint32_t idx = p[i].a;
            for (int j = 0; j < nreg; ++j) {
                if (reg[j].idx == idx) {
                    if (reg[j].g1 > reg[j].g0) {
                        // Текущая сессия: покрываем [g0, g0 + rows)
                        uint32_t start = reg[j].g0;
                        uint32_t end = reg[j].g0 + reg[j].rows;
                        for (uint32_t k = start; k < end && k < total; ++k) {
                            cover[k]++;
                        }
                    } else {
                        // Прежняя сессия: не покрываем, но считаем как старый сегмент
                        old_segs++;
                    }
                    break;
                }
            }
        }
    }
    return old_segs;
}

static uint32_t cover[512];

void wf_seg_rebuild_range_suite(void) {
    // Сценарий A: Арифметика восстановления диапазонов
    {
        wf_seg_range_snap_t prev[] = {{5, 64, 100}, {7, 0, 0}};
        int n = 2;
        uint32_t g0, g1;

        // idx=5, rows=36 -> должно восстановить [64, 100)
        bool res = wf_seg_restore_range(prev, n, 5, 36, &g0, &g1);
        CHECK(res == true);
        CHECK(g0 == 64);
        CHECK(g1 == 100);

        // idx=7, rows=100 -> сегмент прежней сессии (g1<=g0 в снимке)
        res = wf_seg_restore_range(prev, n, 7, 100, &g0, &g1);
        CHECK(res == false);
        CHECK(g0 == 0);
        CHECK(g1 == 0);

        // idx=9 -> нет в снимке
        res = wf_seg_restore_range(prev, n, 9, 10, &g0, &g1);
        CHECK(res == false);
        CHECK(g0 == 0);
        CHECK(g1 == 0);

        // idx=5, rows=0 -> rows == 0
        res = wf_seg_restore_range(prev, n, 5, 0, &g0, &g1);
        CHECK(res == false);
        CHECK(g0 == 0);
        CHECK(g1 == 0);
    }

    // Сценарий B: Н3 (а) - уцелели сегменты прежней и текущей сессии
    {
        wf_seg_range_snap_t prev[] = {{1, 0, 0}, {2, 0, 64}, {3, 64, 100}};
        int n_prev = 3;

        uint32_t g0a, g1a, g0c, g1c;
        wf_seg_restore_range(prev, n_prev, 1, 100, &g0a, &g1a); // idx 1: прежняя сессия
        wf_seg_restore_range(prev, n_prev, 3, 36, &g0c, &g1c); // idx 3: текущая сессия

        wf_seg_reg_t reg[] = {n3_R(1, 100, g0a, g1a), n3_R(3, 36, g0c, g1c)};
        wf_seg_reg_t truth[] = {n3_R(1, 100, 0, 0), n3_R(3, 36, 64, 100)};
        int nreg = 2;

        wf_exp_step_t plan[16];
        int n = wf_exp_plan(reg, nreg, 0, 100, plan, 16);
        CHECK(n > 0);

        memset(cover, 0, sizeof(cover));
        int old_segs = n3_rows_out(plan, n, truth, nreg, cover, 100);

        // Проверка покрытия: каждая строка 0..99 покрыта ровно один раз
        int errors = 0;
        for (uint32_t i = 0; i < 100; ++i) {
            if (cover[i] != 1) {
                printf("Error at row %u: cover=%u\n", i, cover[i]);
                errors++;
            }
        }
        CHECK(errors == 0);

        // Проверка старых сегментов: должен быть выдан 1 старый сегмент (idx 1)
        CHECK(old_segs == 1);
    }

    // Сценарий C: Н3 (б) - уцелели только хвостовые сегменты текущей сессии
    {
        wf_seg_range_snap_t prev[] = {{4, 236, 300}, {5, 300, 400}, {6, 400, 450}};
        int n_prev = 3;

        uint32_t g0_5, g1_5, g0_6, g1_6;
        wf_seg_restore_range(prev, n_prev, 5, 100, &g0_5, &g1_5); // idx 5: [300, 400)
        wf_seg_restore_range(prev, n_prev, 6, 50, &g0_6, &g1_6); // idx 6: [400, 450)

        wf_seg_reg_t reg[] = {n3_R(5, 100, g0_5, g1_5), n3_R(6, 50, g0_6, g1_6)};
        wf_seg_reg_t truth[] = {n3_R(5, 100, 300, 400), n3_R(6, 50, 400, 450)};
        int nreg = 2;

        wf_exp_step_t plan[16];
        int n = wf_exp_plan(reg, nreg, 194, 450, plan, 16);
        CHECK(n > 0);

        memset(cover, 0, sizeof(cover));
        int old_segs = n3_rows_out(plan, n, truth, nreg, cover, 450);

        // Проверка покрытия: каждая строка 194..449 покрыта ровно один раз
        int errors = 0;
        for (uint32_t i = 194; i < 450; ++i) {
            if (cover[i] != 1) {
                printf("Error at row %u: cover=%u\n", i, cover[i]);
                errors++;
            }
        }
        CHECK(errors == 0);

        // Проверка старых сегментов: должно быть 0
        CHECK(old_segs == 0);
    }

    // Сценарий D: Прежний случай М3 - уцелела вся текущая сессия одним сегментом
    {
        wf_seg_range_snap_t prev[] = {{0, 0, 100}};
        int n_prev = 1;

        uint32_t g0_0, g1_0;
        wf_seg_restore_range(prev, n_prev, 0, 100, &g0_0, &g1_0); // idx 0: [0, 100)

        wf_seg_reg_t reg[] = {n3_R(0, 100, g0_0, g1_0)};
        wf_seg_reg_t truth[] = {n3_R(0, 100, 0, 100)};
        int nreg = 1;

        wf_exp_step_t plan[16];
        int n = wf_exp_plan(reg, nreg, 0, 100, plan, 16);
        CHECK(n > 0);

        memset(cover, 0, sizeof(cover));
        int old_segs = n3_rows_out(plan, n, truth, nreg, cover, 100);

        // Проверка покрытия: каждая строка 0..99 покрыта ровно один раз
        int errors = 0;
        for (uint32_t i = 0; i < 100; ++i) {
            if (cover[i] != 1) {
                printf("Error at row %u: cover=%u\n", i, cover[i]);
                errors++;
            }
        }
        CHECK(errors == 0);

        // Проверка старых сегментов: должно быть 0
        CHECK(old_segs == 0);
    }
}
