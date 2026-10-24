#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include "test_util.h"
#include "task_load.h"

#define NCHECK(name, cond) do { if (!(cond)) { printf("FAIL %s %s:%d: %s\n", name, __FILE__, __LINE__, #cond); g_failures++; } } while (0)

static void add(task_load_snap_t *s, const char *name, uint32_t rt) {
    snprintf(s->e[s->n].name, sizeof s->e[s->n].name, "%s", name);
    s->e[s->n].runtime = rt;
    s->n++;
}

static task_load_snap_t a, b;
static task_load_result_t r;

static void test_tl_basic(void) {
    memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b));
    a.total = 1000000; b.total = 2000000;
    add(&a, "IDLE0", 400000); add(&a, "IDLE1", 100000); add(&a, "wifi", 100000); add(&a, "httpd", 50000);
    add(&b, "IDLE0", 900000); add(&b, "IDLE1", 1000000); add(&b, "wifi", 300000); add(&b, "httpd", 60000);
    
    bool ok = task_load_compute(&a, &b, &r);
    NCHECK("tl_basic", ok);
    NCHECK("tl_basic", r.window_us == 1000000);
    NCHECK("tl_basic", r.core_ok[0] && r.core_ok[1]);
    NCHECK("tl_basic", fabsf(r.core[0] - 50.0f) < 0.01f);
    NCHECK("tl_basic", fabsf(r.core[1] - 10.0f) < 0.01f);
    NCHECK("tl_basic", r.ntop == 2);
    NCHECK("tl_basic", strcmp(r.top[0].name, "wifi") == 0 && fabsf(r.top[0].pct - 20.0f) < 0.01f);
    NCHECK("tl_basic", strcmp(r.top[1].name, "httpd") == 0 && fabsf(r.top[1].pct - 1.0f) < 0.01f);
}

static void test_tl_zero_window(void) {
    memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b));
    a.total = 1000; b.total = 1000;
    bool ok = task_load_compute(&a, &b, &r);
    NCHECK("tl_zero_window", !ok);
}

static void test_tl_wrap(void) {
    memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b));
    a.total = 4294967000u; b.total = 704u;
    add(&a, "IDLE0", 4294967100u); add(&a, "busy", 4294967200u);
    add(&b, "IDLE0", 400u); add(&b, "busy", 500u);
    
    bool ok = task_load_compute(&a, &b, &r);
    NCHECK("tl_wrap", ok);
    NCHECK("tl_wrap", r.window_us == 1000);
    NCHECK("tl_wrap", fabsf(r.core[0] - 40.4f) < 0.01f);
    NCHECK("tl_wrap", r.ntop >= 1 && strcmp(r.top[0].name, "busy") == 0 && fabsf(r.top[0].pct - 59.6f) < 0.01f);
}

static void test_tl_new_task(void) {
    memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b));
    a.total = 0; b.total = 1000000;
    add(&a, "old", 100000);
    add(&b, "newt", 250000);
    
    bool ok = task_load_compute(&a, &b, &r);
    NCHECK("tl_new_task", ok);
    bool found = false;
    for (int i = 0; i < r.ntop; i++) {
        if (strcmp(r.top[i].name, "newt") == 0) {
            found = true;
            NCHECK("tl_new_task", fabsf(r.top[i].pct - 25.0f) < 0.01f);
        }
    }
    NCHECK("tl_new_task", found);
}

static void test_tl_top_limit(void) {
    memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b));
    a.total = 0; b.total = 1000000;
    for (int i = 0; i < 12; i++) {
        char name[16];
        snprintf(name, sizeof(name), "t%d", i);
        uint32_t delta = (i + 1) * 10000;
        add(&b, name, delta);
    }
    add(&b, "tiny", 500);
    
    bool ok = task_load_compute(&a, &b, &r);
    NCHECK("tl_top_limit", ok);
    NCHECK("tl_top_limit", r.ntop == 8);
    NCHECK("tl_top_limit", strcmp(r.top[0].name, "t11") == 0 && fabsf(r.top[0].pct - 12.0f) < 0.01f);
    NCHECK("tl_top_limit", strcmp(r.top[7].name, "t4") == 0 && fabsf(r.top[7].pct - 5.0f) < 0.01f);
    
    bool desc = true;
    for (int i = 1; i < r.ntop; i++) {
        if (r.top[i].pct >= r.top[i-1].pct) desc = false;
    }
    NCHECK("tl_top_limit", desc);
    
    bool tiny_found = false;
    for (int i = 0; i < r.ntop; i++) {
        if (strcmp(r.top[i].name, "tiny") == 0) tiny_found = true;
    }
    NCHECK("tl_top_limit", !tiny_found);
}

static void test_tl_idle_missing(void) {
    memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b));
    a.total = 0; b.total = 1000000;
    add(&b, "IDLE0", 500000);
    // No IDLE1
    
    bool ok = task_load_compute(&a, &b, &r);
    NCHECK("tl_idle_missing", ok);
    NCHECK("tl_idle_missing", !r.core_ok[1]);
    NCHECK("tl_idle_missing", r.core_ok[0]);
    NCHECK("tl_idle_missing", r.core[1] == 0.0f);
}

static void test_tl_clamp(void) {
    memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b));
    a.total = 0; b.total = 1000000;
    add(&b, "IDLE0", 1500000); // 150%
    
    bool ok = task_load_compute(&a, &b, &r);
    NCHECK("tl_clamp", ok);
    NCHECK("tl_clamp", r.core[0] == 0.0f);
}

void task_load_suite(void) {
    test_tl_basic();
    test_tl_zero_window();
    test_tl_wrap();
    test_tl_new_task();
    test_tl_top_limit();
    test_tl_idle_missing();
    test_tl_clamp();
}
