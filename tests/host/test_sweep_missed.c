#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "test_util.h"
#include "sweep_missed.h"

#define NCHECK(name, cond) do { if (!(cond)) { printf("FAIL %s %s:%d: %s\n", name, __FILE__, __LINE__, #cond); g_failures++; } } while (0)

static void test_sweep_gap(void) {
    struct { uint32_t in; uint32_t out; } cases[] = {
        {0, 0}, {999, 0}, {1000, 0}, {1499, 0}, {1500, 1}, {1999, 1},
        {2000, 1}, {2499, 1}, {2500, 2}, {3000, 2}, {10000, 9}, {4294967295u, 4294966}
    };
    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        NCHECK("sweep_gap", sweep_gap_intervals(cases[i].in) == cases[i].out);
    }
}

static void test_sweep_normal(void) {
    sweep_missed_t s = {0};
    sweep_missed_commit(&s, 1000);
    sweep_missed_commit(&s, 2000);
    sweep_missed_commit(&s, 3000);
    sweep_missed_commit(&s, 4000);
    NCHECK("sweep_normal", sweep_missed_read(&s, 4000) == 0);
    NCHECK("sweep_normal", sweep_missed_read(&s, 4400) == 0);
}

static void test_sweep_one_missed(void) {
    sweep_missed_t s = {0};
    sweep_missed_commit(&s, 1000);
    sweep_missed_commit(&s, 2000);
    sweep_missed_commit(&s, 4000);
    NCHECK("sweep_one_missed", sweep_missed_read(&s, 4000) == 1);
    sweep_missed_commit(&s, 5000);
    NCHECK("sweep_one_missed", sweep_missed_read(&s, 5000) == 1);
}

static void test_sweep_no_double_count(void) {
    sweep_missed_t s = {0};
    sweep_missed_commit(&s, 1000);
    sweep_missed_commit(&s, 2000);
    /* фаза 1: до коммита - только pending, значение не убывает */
    uint32_t prev = 0;
    bool ok = true;
    for (uint32_t t = 2000; t <= 5000; t += 100) {
        uint32_t cur = sweep_missed_read(&s, t);
        if (cur < prev) ok = false;
        prev = cur;
    }
    NCHECK("sweep_no_double_count", sweep_missed_read(&s, 5000) == 2);
    sweep_missed_commit(&s, 5000);
    /* фаза 2: разрыв свёрнут в done, pending обнулён; значение не убывает и не удваивается */
    for (uint32_t t = 5000; t <= 9000; t += 100) {
        uint32_t cur = sweep_missed_read(&s, t);
        if (cur < prev) ok = false;
        prev = cur;
    }
    NCHECK("sweep_no_double_count", sweep_missed_read(&s, 5000) == 2);
    NCHECK("sweep_no_double_count", sweep_missed_read(&s, 5400) == 2);
    NCHECK("sweep_no_double_count", ok);
}

static void test_sweep_none_before_first(void) {
    sweep_missed_t s = {0};
    NCHECK("sweep_none_before_first", sweep_missed_read(&s, 1000) == 0);

    sweep_missed_commit(&s, 1000);
    sweep_missed_commit(&s, 3000);   /* один пропуск: done == 1 */
    NCHECK("sweep_none_before_first", s.done == 1);

    sweep_missed_forget(&s);         /* сброс спектра: база забыта, накопленное остаётся */
    NCHECK("sweep_none_before_first", sweep_missed_read(&s, 10000) == 1);

    sweep_missed_commit(&s, 20000);  /* пауза до первого свипа после сброса не считается */
    NCHECK("sweep_none_before_first", sweep_missed_read(&s, 20000) == 1);
}

static void test_sweep_wrap(void) {
    sweep_missed_t s = {0};
    sweep_missed_commit(&s, 4294967000u);
    sweep_missed_commit(&s, 1500u);
    NCHECK("sweep_wrap", sweep_missed_read(&s, 1500u) == 1);
}

void sweep_missed_suite(void) {
    test_sweep_gap();
    test_sweep_normal();
    test_sweep_one_missed();
    test_sweep_no_double_count();
    test_sweep_none_before_first();
    test_sweep_wrap();
}
