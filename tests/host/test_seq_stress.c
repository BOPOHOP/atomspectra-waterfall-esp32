#define _POSIX_C_SOURCE 200809L
#include "seq_copy.h"
#include "atomspectra.h"
#include "fixtures/imp_real_bins.h"
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#define N_READERS 2
#define SNAPS_PER_READER 4000
#define SEQ_TRIES 3

static spectrum_data_t g_spec;
static volatile uint32_t g_seq;
static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;
static volatile int g_stop;
static volatile uint32_t g_writer_sections;

#define S_LOCK()   do { pthread_mutex_lock(&g_mtx); g_seq++; __sync_synchronize(); } while (0)
#define S_UNLOCK() do { __sync_synchronize(); g_seq++; pthread_mutex_unlock(&g_mtx); } while (0)

typedef struct {
    uint32_t snaps;
    uint32_t retries;
    uint32_t fallbacks;
    uint32_t torn;
    uint32_t bad_first;
} rd_t;

static void copy_mem(void *d, const void *s, size_t n) {
    memcpy(d, s, n);
}

static void wait_yield(void) {
    sched_yield();
}

static uint32_t bins_sum(const spectrum_data_t *s) {
    uint32_t sum = 0;
    for (int i = 0; i < SPECTRUM_CHANNELS; i++) {
        sum += s->bins[i];
    }
    return sum;
}

static void fill_sweep(spectrum_data_t *s, uint32_t k) {
    uint32_t sum = 0;
    for (int i = 0; i < SPECTRUM_CHANNELS; i++) {
        uint32_t val = (uint32_t)IMP_REAL_BINS[i] * (k % 7u + 1u) + ((k + (uint32_t)i) % 5u);
        s->bins[i] = val;
        sum += val;
    }
    s->total_counts = sum;
    s->total_time_sec = k;
}

static void *writer(void *arg) {
    (void)arg;
    uint32_t k = 1;
    while (!g_stop) {
        S_LOCK();
        fill_sweep(&g_spec, k);
        S_UNLOCK();

        if (k % 7 == 0) {
            S_LOCK();
            memset(g_spec.bins, 0, sizeof(g_spec.bins));
            g_spec.total_counts = 0;
            g_spec.total_time_sec = 0;
            S_UNLOCK();
        }

        S_LOCK();
        volatile uint32_t t = g_spec.total_counts;
        (void)t;
        S_UNLOCK();

        g_writer_sections++;
        k++;
    }
    return NULL;
}

static void *reader(void *arg) {
    rd_t *r = (rd_t *)arg;
    spectrum_data_t *dst = malloc(sizeof(*dst));
    if (!dst) {
        r->torn = 0xFFFFFFFF;
        return NULL;
    }

    for (uint32_t n = 0; n < SNAPS_PER_READER; n++) {
        bool ok = seq_copy(&g_seq, dst, &g_spec, sizeof(*dst), SEQ_TRIES, copy_mem, wait_yield, &r->retries);
        if (!ok) {
            S_LOCK();
            memcpy(dst, &g_spec, sizeof(*dst));
            S_UNLOCK();
            r->fallbacks++;
        }
        r->snaps++;
        if (bins_sum(dst) != dst->total_counts) {
            r->torn++;
            if (r->bad_first == 0) {
                r->bad_first = n + 1;
            }
        }
    }

    free(dst);
    return NULL;
}

int main(void) {
    (void)IMP_REAL_CALIB;
    fill_sweep(&g_spec, 0);
    g_seq = 0;

    pthread_t wtid;
    if (pthread_create(&wtid, NULL, writer, NULL) != 0) {
        printf("FAIL seq_stress test_seq_stress.c:%d: pthread_create\n", __LINE__);
        return 1;
    }

    pthread_t rtids[N_READERS];
    rd_t rds[N_READERS];
    for (int i = 0; i < N_READERS; i++) {
        memset(&rds[i], 0, sizeof(rds[i]));
        if (pthread_create(&rtids[i], NULL, reader, &rds[i]) != 0) {
            printf("FAIL seq_stress test_seq_stress.c:%d: pthread_create\n", __LINE__);
            return 1;
        }
    }

    for (int i = 0; i < N_READERS; i++) {
        pthread_join(rtids[i], NULL);
    }

    g_stop = 1;
    pthread_join(wtid, NULL);

    uint32_t snaps = 0, retries = 0, fallbacks = 0, torn = 0;
    for (int i = 0; i < N_READERS; i++) {
        snaps += rds[i].snaps;
        retries += rds[i].retries;
        fallbacks += rds[i].fallbacks;
        torn += rds[i].torn;
    }

    printf("INFO seq_stress: readers=%d snapshots=%u retries=%u fallbacks=%u torn=%u writer_sections=%u\n",
           N_READERS, snaps, retries, fallbacks, torn, (unsigned)g_writer_sections);

    int fails = 0;
    if (torn != 0) {
        printf("FAIL seq_stress %s:%d: torn snapshot accepted: %u of %u\n", __FILE__, __LINE__, torn, snaps);
        fails++;
    }
    if (retries + fallbacks == 0) {
        printf("FAIL seq_stress %s:%d: no contention observed (test is vacuous)\n", __FILE__, __LINE__);
        fails++;
    }
    if (snaps != (uint32_t)N_READERS * SNAPS_PER_READER) {
        printf("FAIL seq_stress %s:%d: snapshot count\n", __FILE__, __LINE__);
        fails++;
    }

    if (fails == 0) {
        printf("seq_stress: OK\n");
        return 0;
    }
    return 1;
}
