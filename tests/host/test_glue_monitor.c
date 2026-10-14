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

/* #DOSE-A: заглушки ядра дозы (spectrogram.c / spectrum.c не линкуются на хосте) */
static int g_stub_mode;
static float g_stub_k = 2.0f;
int spectrogram_dose_mode(void) { return g_stub_mode; }
uint32_t spectrogram_dose_gen(void) { return 0; }
bool spectrogram_dose_delta_sum(const uint32_t *prev, const uint32_t *cur, float *out_wsum) { (void)prev; (void)cur; *out_wsum = 0.0f; return true; }
float spectrogram_get_dose_k(void) { return g_stub_k; }
bool spectrum_get_snapshot_wf(spectrum_data_t *out, uint32_t *resync_seq) { (void)out; *resync_seq = 0; return false; }

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
  /* #DOSE-A: кольцо доз - окно дозы берёт те же сэмплы, что SMA; NAN-сэмплы не входят ни в сумму доз, ни в Σdur_dose */
  {
    g_stub_mode = 2;
    NCHECK("glue_mon_dose", dose_buffers_ready() && s_dose_ring != NULL && !s_dose_alloc_failed);
    enum { ND = 300 };
    static float dws[ND];
    static uint16_t ddur[ND];
    for (int i = 0; i < ND; i++) {
      uint16_t d = (uint16_t)(1 + rnd() % 3);
      float v = (i % 5 == 3) ? NAN : (float)d * 0.25f * (float)(1 + rnd() % 8);
      dws[i] = v; ddur[i] = d; end += d;
      ring_push_dose(end, d * STAND_CPS, d, MON_T_NONE, v);
    }
    const uint32_t wd[] = {1, 7, 10, 60, 150};
    int dose_cases = 0;
    for (int k = 0; k < 5; k++) {
      float es = 0.0f, gs = 0.0f;
      uint32_t td = 0, ed = 0, gd = 0, gn = 0;
      for (int i = ND - 1; i >= 0 && td < wd[k]; i--) { td += ddur[i]; if (isfinite(dws[i])) { es += dws[i]; ed += ddur[i]; } }
      monitor_window_dose(wd[k], &gs, &gd, &gn);
      NCHECK("glue_mon_dose", gd == ed && fabsf(gs - es) <= 1e-3f * (1.0f + fabsf(es)));
      spec_dose_t dz;
      monitor_dose_live(wd[k], &dz);
      NCHECK("glue_mon_dose", strcmp(dz.mode, "curve") == 0 && dz.w == ed && (ed > 0) == dz.have &&
             (ed == 0 || fabsf(dz.rate - es / (float)ed) <= 1e-3f * (1.0f + dz.rate)));
      dose_cases++;
    }
    g_stub_mode = 1;
    {
      uint64_t sc = 0; uint32_t sd = 0, n = 0; spec_dose_t dz;
      monitor_window(60, &sc, &sd, &n);
      monitor_dose_live(60, &dz);
      NCHECK("glue_mon_dose", strcmp(dz.mode, "scalar") == 0 && dz.have && dz.w == sd &&
             fabsf(dz.rate - ((float)sc / (float)sd) * 2.0f) <= 1e-3f * (1.0f + dz.rate));
    }
    g_stub_mode = 0;
    { spec_dose_t dz; monitor_dose_live(60, &dz); NCHECK("glue_mon_dose", strcmp(dz.mode, "none") == 0 && !dz.have && dz.w == 0); }
    /* ring_push() без дозы пишет NAN: сэмпл входит в окно, но не в Σdur_dose */
    ring_push(end + 1, STAND_CPS, 1, MON_T_NONE);
    { float gs = 1.0f; uint32_t gd = 7, gn = 7; monitor_window_dose(1, &gs, &gd, &gn); NCHECK("glue_mon_dose", gd == 0 && gn == 0 && gs == 0.0f); }
    printf("INFO glue_mon_dose: cases=%d\n", dose_cases);
  }
  NCHECK("glue_mon_window", glue_sem_depth == 0);
  printf("INFO glue_monitor: samples=%d dur_sum=%" PRIu32 " windows=%d differ_y5=%d\n", NS, dsum, nw, differ_y5);
  if (g_failures) { printf("test_glue_monitor: %d CHECK(S) FAILED\n", g_failures); return 1; }
  printf("test_glue_monitor: OK\n");
  return 0;
}
