// #AUD-RST: неподтверждённый Сброс — строка против опоры до Сброса (main/wf_ref_plan.h, wf_rst_keeps_data).
// Сгенерировано gen_code.py (qwen3.6:27b) по scripts/ollama/spec_test_wf_rst_keep.md, проверено вручную.

#include "wf_ref_plan.h"
#include "test_util.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#define RN 4

static void test_rst_rejects(void);
static void test_rst_unconfirmed_no_loss(void);
static void test_rst_device_did_reset(void);
static void test_rst_after_device_restart_no_jump(void);

void test_wf_rst_keep(void)
{
    test_rst_rejects();
    test_rst_unconfirmed_no_loss();
    test_rst_device_did_reset();
    test_rst_after_device_restart_no_jump();
}

static void test_rst_rejects(void)
{
    uint32_t pre[RN] = {10, 20, 30, 40};
    uint32_t cur[RN] = {15, 25, 35, 45};

    CHECK(wf_rst_keeps_data(pre, 100, 50, cur, 120, 60, RN));
    CHECK(!wf_rst_keeps_data(NULL, 100, 50, cur, 120, 60, RN));
    CHECK(!wf_rst_keeps_data(pre, 100, 50, NULL, 120, 60, RN));
    CHECK(!wf_rst_keeps_data(pre, 100, 50, cur, 99, 60, RN));
    CHECK(!wf_rst_keeps_data(pre, 100, 50, cur, 120, 49, RN));

    uint32_t low[RN];
    memcpy(low, cur, sizeof(low));
    low[2] = 29;
    CHECK(!wf_rst_keeps_data(pre, 100, 50, low, 120, 60, RN));

    memcpy(low, cur, sizeof(low));   /* F-2 (разбор 1c57e98): последний канал тоже сравнивается */
    low[RN - 1] = pre[RN - 1] - 1;
    CHECK(!wf_rst_keeps_data(pre, 100, 50, low, 120, 60, RN));

    CHECK(wf_rst_keeps_data(pre, 100, 50, pre, 100, 50, RN));
}

static void test_rst_unconfirmed_no_loss(void)
{
    uint32_t prev[RN] = {100, 200, 300, 400};
    uint32_t prev_total = 1000;
    uint32_t prev_time = 100;

    uint32_t pre[RN];
    memcpy(pre, prev, sizeof(pre));
    uint32_t pre_total = prev_total;
    uint32_t pre_time = prev_time;

    uint32_t zero[RN] = {0};
    uint32_t row_zero[RN];
    for (size_t i = 0; i < RN; i++)
    {
        row_zero[i] = wf_row_delta(zero[i], prev[i], true);
        prev[i] = zero[i];
    }
    prev_total = 0;
    prev_time = 0;

    uint32_t dev[RN] = {130, 260, 390, 520};
    uint32_t dev_total = 1300;
    uint32_t dev_time = 130;

    bool keep = wf_rst_keeps_data(pre, pre_total, pre_time, dev, dev_total, dev_time, RN);

    uint32_t row1[RN] = {0};
    uint32_t dur = 0;
    bool wrote = false;

    if (keep)
    {
        for (size_t i = 0; i < RN; i++)
        {
            row1[i] = wf_row_delta(dev[i], pre[i], false);
        }
        dur = dev_time - pre_time;
        wrote = true;
    }

    uint32_t sum = 0;
    for (size_t i = 0; i < RN; i++)
    {
        sum += row_zero[i] + row1[i];
    }

    CHECK(keep);
    CHECK(wrote);
    CHECK(sum == dev_total - pre_total);

    for (size_t i = 0; i < RN; i++)
    {
        CHECK(row_zero[i] + row1[i] == dev[i] - pre[i]);
    }

    CHECK(dur == 30);
}

static void test_rst_device_did_reset(void)
{
    uint32_t pre[RN] = {100, 200, 300, 400};
    uint32_t dev[RN] = {3, 5, 2, 4};

    CHECK(!wf_rst_keeps_data(pre, 1000, 100, dev, 14, 2, RN));

    uint32_t dev2[RN] = {130, 260, 10, 520};
    CHECK(!wf_rst_keeps_data(pre, 1000, 100, dev2, 1300, 130, RN));
}

static void test_rst_after_device_restart_no_jump(void)
{
    uint32_t pre[RN] = {150, 250, 350, 450};
    uint32_t dev[RN] = {130, 260, 390, 520};

    CHECK(!wf_rst_keeps_data(pre, 1200, 160, dev, 1300, 130, RN));
    CHECK(!wf_rst_keeps_data(pre, 1200, 100, dev, 1300, 130, RN));
}
