#ifndef TASK_LOAD_H
#define TASK_LOAD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define TASK_LOAD_MAX_TASKS 48
#define TASK_LOAD_TOP 8
#define TASK_LOAD_NAME 16

typedef struct {
    char name[TASK_LOAD_NAME];
    uint32_t runtime;
} task_load_entry_t;

typedef struct {
    uint32_t total;
    int n;
    task_load_entry_t e[TASK_LOAD_MAX_TASKS];
} task_load_snap_t;

typedef struct {
    char name[TASK_LOAD_NAME];
    float pct;
} task_load_top_t;

typedef struct {
    uint32_t window_us;
    float    core[2];
    bool     core_ok[2];
    int      ntop;
    task_load_top_t top[TASK_LOAD_TOP];
} task_load_result_t;

static inline bool task_load_compute(const task_load_snap_t *a, const task_load_snap_t *b, task_load_result_t *r) {
    memset(r, 0, sizeof *r);
    uint32_t dt = b->total - a->total;
    if (dt == 0) return false;
    r->window_us = dt;

    int n = b->n;
    if (n > TASK_LOAD_MAX_TASKS) n = TASK_LOAD_MAX_TASKS;

    for (int i = 0; i < n; i++) {
        const task_load_entry_t *eb = &b->e[i];
        const task_load_entry_t *ea = NULL;
        for (int j = 0; j < a->n; j++) {
            if (strncmp(a->e[j].name, eb->name, TASK_LOAD_NAME) == 0) {
                ea = &a->e[j];
                break;
            }
        }
        uint32_t d;
        if (ea) d = eb->runtime - ea->runtime;
        else d = eb->runtime;

        float pct = 100.0f * (float)d / (float)dt;

        if (strncmp(eb->name, "IDLE0", TASK_LOAD_NAME) == 0) {
            r->core[0] = 100.0f - pct;
            if (r->core[0] < 0.0f) r->core[0] = 0.0f;
            if (r->core[0] > 100.0f) r->core[0] = 100.0f;
            r->core_ok[0] = true;
        } else if (strncmp(eb->name, "IDLE1", TASK_LOAD_NAME) == 0) {
            r->core[1] = 100.0f - pct;
            if (r->core[1] < 0.0f) r->core[1] = 0.0f;
            if (r->core[1] > 100.0f) r->core[1] = 100.0f;
            r->core_ok[1] = true;
        } else if (pct >= 0.1f) {
            int pos = r->ntop;
            if (pos >= TASK_LOAD_TOP) {
                if (pct <= r->top[TASK_LOAD_TOP - 1].pct) continue;
                pos = TASK_LOAD_TOP - 1;   /* вытесняем последний */
            } else {
                r->ntop++;
            }
            while (pos > 0 && r->top[pos - 1].pct < pct) {
                r->top[pos] = r->top[pos - 1];
                pos--;
            }
            strncpy(r->top[pos].name, eb->name, TASK_LOAD_NAME - 1);
            r->top[pos].name[TASK_LOAD_NAME - 1] = '\0';
            r->top[pos].pct = pct;
        }
    }
    return true;
}

#endif
