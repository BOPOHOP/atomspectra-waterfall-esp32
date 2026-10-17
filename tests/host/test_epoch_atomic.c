#include "spectrum_epoch.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>

#define NCHECK(name, cond) do { if (!(cond)) { printf("FAIL %s %s:%d: %s\n", name, __FILE__, __LINE__, #cond); g_failures++; } } while (0)

static int g_failures;

#define NW 64
typedef struct { uint32_t w[NW]; } sp_t;

static volatile uint32_t g_seq;
static sp_t g_sp;
static volatile uint32_t g_dev, g_spec;

static uint32_t pat(uint32_t d, uint32_t s) { return d * 65537u + s * 2654435761u + 1u; }

static void writer_step(void) {
    uint32_t d = g_dev + 1;
    uint32_t s = g_spec + 3;
    g_seq++;
    __sync_synchronize();
    for (int i = 0; i < NW; i++) g_sp.w[i] = pat(d, s);
    g_dev = d;
    g_spec = s;
    __sync_synchronize();
    g_seq++;
}

static void world_init(void) {
    g_seq = 0;
    g_dev = 10;
    g_spec = 20;
    for (int i = 0; i < NW; i++) g_sp.w[i] = pat(10, 20);
}

static void read_live(spec_epoch_t *o) {
    o->dev_resets = g_dev;
    o->spec_resets = g_spec;
}

static void wait_noop(void) {}

static bool consistent(const sp_t *a, const spec_epoch_t *e) {
    for (int i = 0; i < NW; i++) {
        if (a->w[i] != pat(e->dev_resets, e->spec_resets)) return false;
    }
    return true;
}

static int g_samples, g_inconsistent;
static int g_inject_mode;
static int g_inject_done;

static void copy_hook(void *d, const void *s, size_t n) {
    size_t half = n / 2;
    memcpy(d, s, half);
    if (g_inject_mode == 1 && !g_inject_done) {
        g_inject_done = 1;
        writer_step();
    }
    memcpy((char *)d + half, (const char *)s + half, n - half);
}

static void read_hook(spec_epoch_t *o) {
    if (g_inject_mode == 2 && !g_inject_done) {
        g_inject_done = 1;
        writer_step();
    }
    read_live(o);
}

static bool snap(spec_epoch_t *ep, sp_t *out, uint32_t *retries) {
    return spec_epoch_snapshot_seq(&g_seq, out, &g_sp, sizeof *out, ep, read_hook, 3, copy_hook, wait_noop, retries);
}

static volatile int g_stop;
static int g_rejected;

static void *writer_thread(void *arg) {
    (void)arg;
    for (int i = 0; i < 200000; i++) {
        writer_step();
        for (volatile int k = 0; k < 20; k++);
    }
    g_stop = 1;
    return NULL;
}

int main(void) {
    sp_t out;
    spec_epoch_t ep;
    uint32_t retries;
    bool ok;

    /* Scenario 0: no writer */
    world_init();
    g_inject_mode = 0;
    g_inject_done = 0;
    retries = 0;
    ok = snap(&ep, &out, &retries);
    NCHECK("epoch_atomic", ok && consistent(&out, &ep) && retries == 0 && ep.dev_resets == 10 && ep.spec_resets == 20);
    g_samples++;
    if (ok && !consistent(&out, &ep)) g_inconsistent++;

    /* Scenario 1: writer inside spectrum copy */
    world_init();
    g_inject_mode = 1;
    g_inject_done = 0;
    retries = 0;
    ok = snap(&ep, &out, &retries);
    NCHECK("epoch_atomic", ok && retries == 1 && consistent(&out, &ep) && ep.dev_resets == 11 && ep.spec_resets == 23);
    g_samples++;
    if (ok && !consistent(&out, &ep)) g_inconsistent++;

    /* Scenario 2: writer inside read_epoch */
    world_init();
    g_inject_mode = 2;
    g_inject_done = 0;
    retries = 0;
    ok = snap(&ep, &out, &retries);
    NCHECK("epoch_atomic", ok && retries == 1 && consistent(&out, &ep) && ep.dev_resets == 11 && ep.spec_resets == 23);
    g_samples++;
    if (ok && !consistent(&out, &ep)) g_inconsistent++;

    /* Scenario 3: stress with concurrent writer */
    world_init();
    g_inject_mode = 0;
    g_inject_done = 0;
    g_stop = 0;
    g_rejected = 0;

    pthread_t tid;
    if (pthread_create(&tid, NULL, writer_thread, NULL) != 0) {
        printf("FAIL epoch_atomic pthread_create\n");
        g_failures++;
        return 1;
    }

    while (!g_stop) {
        retries = 0;
        ok = snap(&ep, &out, &retries);
        if (ok) {
            g_samples++;
            if (!consistent(&out, &ep)) g_inconsistent++;
        } else {
            g_rejected++;
        }
    }

    pthread_join(tid, NULL);

    NCHECK("epoch_atomic", g_inconsistent == 0);
    NCHECK("epoch_atomic", g_samples > 100);

    printf("INFO k2: test=epoch_atomic samples=%d differ_from_ref=%d rejected=%d\n", g_samples, g_inconsistent, g_rejected);

    if (g_failures) {
        printf("test_epoch_atomic: %d CHECK(S) FAILED\n", g_failures);
        return 1;
    }
    printf("test_epoch_atomic: OK\n");
    return 0;
}
