// С-2 (release-gate-1.2.36-code-pass1): склейка monitor.c на хосте (сам .c, заглушки IDF в tests/host/glue): monitor_window() поверх кольца. Мутант Y5 (предел окна 60 вместо ёмкости кольца) обязан краснить glue_mon_window.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <math.h>

#include "../../main/monitor.c"
#include "fixtures/stand_capture.h"

void spectrum_add_commit_listener(void *freertos_binary_sem) { (void)freertos_binary_sem; }
void spectrum_get_totals(uint32_t *counts, uint32_t *time_sec, uint32_t *resync_seq) { *counts = 0; *time_sec = 0; *resync_seq = 0; }
float spectrum_get_t1(void) { return NAN; }

static int g_failures;
#define NCHECK(name, cond) do { if (!(cond)) { printf("FAIL %s %s:%d: %s\n", name, __FILE__, __LINE__, #cond); g_failures++; } } while (0)

#define NS 700
static uint32_t g_cnt[NS]; static uint16_t g_dur[NS];
static uint32_t g_seed = 0x5eed0081u;
static uint32_t rnd(void) { g_seed = g_seed * 1103515245u + 12345u; return g_seed >> 8; }

/* independent expectation: walk the pushed samples from the newest back while Σdur < w (the sample that reaches w is included) */
static void expect_window(uint32_t w, uint64_t *sc, uint32_t *sd, uint32_t *n) {
  uint64_t c = 0; uint32_t d = 0, k = 0;
  for (int i = NS - 1; i >= 0 && d < w; i--) { c += g_cnt[i]; d += g_dur[i]; k++; }
  *sc = c; *sd = d; *n = k;
}

int main(void) {
  monitor_init();
  NCHECK("glue_mon_window", s_ring != NULL && s_cap == 43200);
  /* samples of the live stand form: dur 1 (90%), 2 (7%), 3 (3%); counts = dur*STAND_CPS +- 16 */
  uint32_t end = 72546, dsum = 0;
  for (int i = 0; i < NS; i++) {
    uint32_t r = rnd() % 100;
    uint16_t d = (uint16_t)(r < 90 ? 1 : (r < 97 ? 2 : 3));
    uint32_t c = d * STAND_CPS + (rnd() % 33) - 16;
    end += d; dsum += d;
    g_cnt[i] = c; g_dur[i] = d;
    ring_push(end, c, d, MON_T_NONE);
  }
  /* windows in seconds: inside the old 60 s, just above it, minutes, longer than all data (clamped to ring capacity 43200 s, which is above dsum) */
  const uint32_t ws[] = {1, 10, 60, 61, 120, 300, 600, 700, 100000};
  int nw = (int)(sizeof ws / sizeof ws[0]), differ_y5 = 0;
  for (int k = 0; k < nw; k++) {
    uint64_t sc = 0, esc = 0, ysc = 0; uint32_t sd = 0, n = 0, esd = 0, en = 0, ysd = 0, yn = 0;
    monitor_window(ws[k], &sc, &sd, &n);
    uint32_t wexp = ws[k] > 43200 ? 43200 : ws[k];
    expect_window(wexp, &esc, &esd, &en);
    NCHECK("glue_mon_window", sc == esc && sd == esd && n == en);
    expect_window(ws[k] > 60 ? 60 : ws[k], &ysc, &ysd, &yn);   /* what mutant Y5 would return */
    if (ysc != esc || ysd != esd || yn != en) differ_y5++;
  }
  /* longest window covers every pushed sample */
  { uint64_t sc = 0; uint32_t sd = 0, n = 0; monitor_window(100000, &sc, &sd, &n); NCHECK("glue_mon_window", n == NS && sd == dsum); }
  /* w = 0 is clamped to 1 s: exactly the newest sample */
  { uint64_t sc = 0; uint32_t sd = 0, n = 0; monitor_window(0, &sc, &sd, &n); NCHECK("glue_mon_window", n == 1 && sc == g_cnt[NS - 1] && sd == g_dur[NS - 1]); }
  NCHECK("glue_mon_window", glue_sem_depth == 0);
  printf("INFO glue_monitor: samples=%d dur_sum=%" PRIu32 " windows=%d differ_y5=%d\n", NS, dsum, nw, differ_y5);
  if (g_failures) { printf("test_glue_monitor: %d CHECK(S) FAILED\n", g_failures); return 1; }
  printf("test_glue_monitor: OK\n");
  return 0;
}
