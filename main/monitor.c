// #MON-1 (#FW-40): сбор серии CPS на плате вместо браузера.
//
// Проблема: web/monitor.html копил дельты по live-poll /api/spectrum.json в JS.
// Вкладка в фоне → браузер троттлит setInterval до ~60 с (интервалы скачут,
// диагонали в графике), вкладка закрыта → дыра в данных.
//
// Решение: monitor_task подписан на коммиты свипов (spectrum_add_commit_listener,
// как wf_task/autosave) и на каждом коммите снимает атомарную пару
// (total_counts, total_time_sec) через spectrum_get_totals() — дёшево, без
// 32 КБ snapshot. Дельта против прошлого замера кладётся базовым сэмплом в
// кольцо PSRAM (6 ч при 1 Гц); httpd отдаёт хвост серии по seq
// (GET /api/monitor/series?since=<seq>, web_server.c). Точность не зависит от
// фазы задачи: dur считается по НАБОРНОМУ времени прибора — проспали два
// коммита → один честный сэмпл dur=2, а не искажение.

#include "monitor.h"
#include "monitor_window.h"
#include "dose_api.h"       // #DOSE-A: spec_dose_t, spectrogram_dose_*
#include "atomspectra.h"

#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "monitor";

// #MON-3: 12 ч вместо 6. Кольцо — единственное, что переживает F5 страницы:
// вкладка держит свою историю в памяти браузера, но при перезагрузке
// восстанавливает ровно то, что осталось на плате. При 6 ч суточное наблюдение
// теряло данные от одного случайного F5. Цена: 506 КБ PSRAM вместо 253 из 8 МБ (12 Б/сэмпл).
// Не хватит — monitor_init штатно падает в internal-fallback (1 ч), не в отказ.
#define MON_RING_PSRAM     43200   // 12 ч при 1 Гц: 43200 × 12 Б ≈ 506 КБ PSRAM (с 1.2.31 — с температурой)
#define MON_RING_INTERNAL   3600   // fallback без PSRAM: 1 ч ≈ 42 КБ internal

static monitor_sample_t *s_ring;
static uint32_t s_cap;       // фактическая ёмкость кольца (см. monitor_init)
static uint32_t s_head;      // индекс, куда ляжет следующий сэмпл
static uint32_t s_count;     // занято сэмплов (<= s_cap)
static uint32_t s_last_seq;  // seq последнего записанного (0 = ещё ничего → первый сэмпл seq=1)
static uint32_t s_epoch;     // эпоха серии; растёт при откате счётчиков прибора

// Писатель — monitor_task (1 сэмпл/с), читатель — httpd-воркер. Лок держится
// микросекунды (запись 12 Б) / доли мс (копирование чанка <= ~24 КБ).
static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_commit_sig;   // отдаёт spectrum.c на каждом коммите свипа

// #DOSE-A: живая мощность дозы по кривой. Кольцо доз параллельно кольцу сэмплов (тот же индекс, тот же s_lock):
// s_dose_ring[i] = Σ(Δbins[k]·lut[k]) за сэмпл i, µSv/h·с; NAN — доза неизвестна (нет кривой, разрыв, гонка снимка).
// Выделяется лениво в PSRAM при первой загруженной кривой; нет памяти — s_dose_alloc_failed, клиентам режим "none".
static float            *s_dose_ring;
static volatile bool     s_dose_alloc_failed;
static uint32_t         *s_dose_prev_bins;   // 32 КБ PSRAM, только monitor_task
static spectrum_data_t  *s_dose_snap;        // ~33 КБ PSRAM, только monitor_task
static bool              s_dose_prev_ok;     // s_dose_prev_bins относится к s_dose_prev_t
static uint32_t          s_dose_prev_t;      // total_time_sec снимка, лежащего в s_dose_prev_bins
static uint32_t          s_dose_gen_seen;    // последнее увиденное поколение кривой

static void ring_push_dose(uint32_t end_sec, uint32_t dcounts, uint16_t dur, int16_t t_dc, float dose_ws)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_ring[s_head].end_sec = end_sec;
    s_ring[s_head].counts  = dcounts;
    s_ring[s_head].dur     = dur;
    s_ring[s_head].t_dc    = t_dc;
    if (s_dose_ring) s_dose_ring[s_head] = dose_ws;   // #DOSE-A: тот же индекс и тот же лок, что у сэмпла
    s_head = (s_head + 1) % s_cap;
    if (s_count < s_cap) s_count++;   // при переполнении старые вытесняются, seq растёт дальше
    s_last_seq++;
    xSemaphoreGive(s_lock);
}

// #DOSE-A: прежняя сигнатура (4 аргумента) — сэмпл без дозы (NAN); используют host-тесты (test_glue_monitor.c).
__attribute__((unused)) static void ring_push(uint32_t end_sec, uint32_t dcounts, uint16_t dur, int16_t t_dc)
{
    ring_push_dose(end_sec, dcounts, dur, t_dc, NAN);
}
// Откат счётчиков (сброс спектра -rst / рестарт прибора): новая эпоха, кольцо
// очищается. seq НЕ обнуляется — монотонен через эпохи, клиенту так проще.
static void ring_new_epoch(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_epoch++;
    s_count = 0;
    s_head  = 0;
    xSemaphoreGive(s_lock);
    ESP_LOGW(TAG, "counters rollback -> epoch %" PRIu32 ", ring cleared", s_epoch);
}

size_t monitor_copy_since(uint32_t since, monitor_sample_t *out, size_t max,
                          uint32_t *out_first_seq, uint32_t *out_next_seq,
                          uint32_t *out_epoch)
{
    if (!s_lock || !s_ring) {   // монитор отключён (alloc failed) — пустая серия
        if (out_first_seq) *out_first_seq = 1;
        if (out_next_seq)  *out_next_seq  = 1;
        if (out_epoch)     *out_epoch     = 0;
        return 0;
    }
    size_t n = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint32_t next  = s_last_seq + 1;
    uint32_t first = next;                        // «отдать нечего» по умолчанию
    uint32_t epoch = s_epoch;                     // строго под локом — когерентно с сэмплами
    if (s_count > 0) {
        uint32_t oldest = s_last_seq - s_count + 1;
        uint64_t from   = (uint64_t)since + 1;    // uint64 — нет wrap на since=UINT32_MAX
        if (from < oldest) from = oldest;
        if (from <= s_last_seq) {
            size_t avail = (size_t)(s_last_seq - (uint32_t)from) + 1;
            n = (avail > max) ? max : avail;
            first = (uint32_t)from;
            for (size_t i = 0; i < n; i++) {
                uint32_t seq = first + (uint32_t)i;
                uint32_t idx = (s_head + s_cap - (s_last_seq - seq + 1)) % s_cap;
                out[i] = s_ring[idx];
            }
        }
    }
    xSemaphoreGive(s_lock);
    if (out_first_seq) *out_first_seq = first;
    if (out_next_seq)  *out_next_seq  = next;
    if (out_epoch)     *out_epoch     = epoch;
    return n;
}

// 1.2.36 #MX-14: SMA окна W секунд наборного времени прямо по кольцу (без копии и без выделений) — для meta.json?w=.
// Лок кольца — единственный, что держится внутри; вызывающий не держит других замков.
void monitor_window(uint32_t w_sec, uint64_t *sum_counts, uint32_t *sum_dur, uint32_t *n)
{
    uint64_t sc = 0;
    uint32_t sd = 0, k = 0;
    if (s_lock && s_ring) {
        if (w_sec < 1) w_sec = 1;
        if (w_sec > s_cap) w_sec = s_cap;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        k = mon_window_sum(s_ring, s_cap, s_head, s_count, w_sec, &sc, &sd);
        xSemaphoreGive(s_lock);
    }
    *sum_counts = sc;
    *sum_dur = sd;
    *n = k;
}

// ---- #DOSE-A: живая мощность дозы по кривой -------------------------------------------------------------------

// Залить кольцо доз NAN под s_lock (смена кривой/коэффициента: дозы прежней кривой в окно не попадают).
static void dose_ring_fill_nan(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_dose_ring) {
        for (uint32_t i = 0; i < s_cap; i++) s_dose_ring[i] = NAN;
    }
    xSemaphoreGive(s_lock);
}

// Ленивое выделение буферов (PSRAM) при первой загруженной кривой. false — нет памяти: один раз WARN, дальше режим "none".
static bool dose_buffers_ready(void)
{
    if (s_dose_ring && s_dose_snap && s_dose_prev_bins) return true;
    if (s_dose_alloc_failed) return false;
    float *r = heap_caps_malloc((size_t)s_cap * sizeof(float), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    spectrum_data_t *sn = heap_caps_malloc(sizeof(spectrum_data_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint32_t *pb = heap_caps_malloc((size_t)SPECTRUM_CHANNELS * sizeof(uint32_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!r || !sn || !pb) {
        free(r); free(sn); free(pb);
        s_dose_alloc_failed = true;
        ESP_LOGW(TAG, "dose ring alloc failed - live dose disabled (dose_mode=none)");
        return false;
    }
    for (uint32_t i = 0; i < s_cap; i++) r[i] = NAN;
    s_dose_snap = sn;
    s_dose_prev_bins = pb;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_dose_ring = r;                      // публикация под локом: ring_push_dose видит указатель только полностью залитым NAN
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "dose ring %" PRIu32 " samples (%" PRIu32 " KB PSRAM)", s_cap, (uint32_t)(s_cap * sizeof(float) / 1024));
    return true;
}

// Доза сэмпла = Σ(Δbins·lut) между снимком прошлого сэмпла и снимком этого коммита, µSv/h·с; NAN — дозы нет.
// Сэмпл годен, только если: кривая загружена и не пересобирается; снимок того же коммита, что пара totals
// (total_time_sec == tsec, тот же resync); прошлый снимок взят на предыдущем сэмпле (s_dose_prev_t == prev_time);
// ни одного канала с откатом; поколение кривой не менялось за время расчёта.
static float dose_sample(uint32_t tsec, uint32_t prev_time, uint32_t resync)
{
    uint32_t g0 = spectrogram_dose_gen();
    if (g0 != s_dose_gen_seen) {          // кривую/коэффициент загрузили, удалили или сменили на лету
        dose_ring_fill_nan();
        s_dose_gen_seen = g0;
        s_dose_prev_ok = false;
    }
    if ((g0 & 1u) || spectrogram_dose_mode() != 2) { s_dose_prev_ok = false; return NAN; }
    if (!dose_buffers_ready()) return NAN;
    uint32_t r2 = 0;
    if (!spectrum_get_snapshot_wf(s_dose_snap, &r2)) { s_dose_prev_ok = false; return NAN; }
    bool same = (r2 == resync) && (s_dose_snap->total_time_sec == tsec);
    float v = NAN;
    if (same && s_dose_prev_ok && s_dose_prev_t == prev_time) {
        float ws = 0.0f;
        if (spectrogram_dose_delta_sum(s_dose_prev_bins, s_dose_snap->bins, &ws) && spectrogram_dose_gen() == g0) v = ws;
    }
    if (same) {
        memcpy(s_dose_prev_bins, s_dose_snap->bins, (size_t)SPECTRUM_CHANNELS * sizeof(uint32_t));
        s_dose_prev_t = tsec;
        s_dose_prev_ok = true;
    } else {
        s_dose_prev_ok = false;
    }
    return v;
}

// Окно дозы: те же сэмплы, что monitor_window(); *sum_dur_dose — Σdur сэмплов с конечной дозой, *sum_ws — Σ их доз
// (µSv/h·с), *n_dose — их число. Нет кольца доз / монитор отключён — нули.
void monitor_window_dose(uint32_t w_sec, float *sum_ws, uint32_t *sum_dur_dose, uint32_t *n_dose)
{
    float s = 0.0f;
    uint32_t sdd = 0, nd = 0;
    if (s_lock && s_ring) {
        if (w_sec < 1) w_sec = 1;
        if (w_sec > s_cap) w_sec = s_cap;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_dose_ring) mon_window_dose_sum(s_ring, s_dose_ring, s_cap, s_head, s_count, w_sec, &s, &sdd, &nd);
        xSemaphoreGive(s_lock);
    }
    *sum_ws = s;
    *sum_dur_dose = sdd;
    *n_dose = nd;
}

// Живая МД за окно W (см. dose_api.h): curve — по кольцу доз, scalar — (Σcounts/Σdur)·dose_k, иначе none.
// dose_rate валиден (have) только при Σdur_dose > 0 и конечном значении; окно — то же, что у SMA.
void monitor_dose_live(uint32_t w_sec, spec_dose_t *out)
{
    out->have = false;
    out->rate = 0.0f;
    out->mode = "none";
    out->w = 0;
    int m = spectrogram_dose_mode();
    if (m == 2) {
        if (s_dose_alloc_failed) return;   // нет памяти под кольцо — клиентам "none"
        float ws = 0.0f;
        uint32_t sdd = 0, nd = 0;
        monitor_window_dose(w_sec, &ws, &sdd, &nd);
        out->mode = "curve";
        out->w = sdd;
        if (sdd > 0 && isfinite(ws)) { out->have = true; out->rate = ws / (float)sdd; }
    } else if (m == 1) {
        uint64_t sc = 0;
        uint32_t sd = 0, n = 0;
        monitor_window(w_sec, &sc, &sd, &n);
        out->mode = "scalar";
        out->w = sd;
        if (sd > 0) { out->have = true; out->rate = ((float)sc / (float)sd) * spectrogram_get_dose_k(); }
    }
}
static void monitor_task(void *arg)
{
    (void)arg;
    uint32_t prev_counts = 0, prev_time = 0, prev_resync = 0;
    bool prev_valid = false;
    for (;;) {
        // Будимся коммитом свипа (паттерн wf_task); 1500 мс — fallback-тик при
        // молчащем приборе (тогда totals не растут и сэмпл не эмитится).
        if (s_commit_sig) xSemaphoreTake(s_commit_sig, pdMS_TO_TICKS(1500));
        else vTaskDelay(pdMS_TO_TICKS(1000));

        uint32_t counts = 0, tsec = 0, resync = 0;
        spectrum_get_totals(&counts, &tsec, &resync);

        // Н-8 (release-gate 1.2.29): первый замер (в т.ч. restored-autosave) и
        // первая публикация без подтверждённого сброса (старт без current.bin,
        // принятие по таймауту #58) — только опорная точка: иначе сэмпл получил
        // бы весь накопленный прибором набор разом (как строка водопада, У-3).
        if (!prev_valid || resync != prev_resync) {
            prev_counts = counts; prev_time = tsec; prev_resync = resync; prev_valid = true;
            continue;
        }
        if (counts < prev_counts || tsec < prev_time) {
            // Откат счётчиков = сброс спектра или рестарт прибора → новая эпоха.
            ring_new_epoch();
            prev_counts = counts; prev_time = tsec;
            continue;
        }
        uint32_t dur = tsec - prev_time;
        if (dur == 0) continue;              // свип без прироста времени — ждём дальше
        if (dur > UINT16_MAX) {
            // Скачок крупнее формата сэмпла (>18 ч) — не данные, тихий ресинк.
            ESP_LOGW(TAG, "time jump %" PRIu32 "s > sample fmt, resync", dur);
            prev_counts = counts; prev_time = tsec;
            continue;
        }
        float t1 = spectrum_get_t1();
        int16_t t_dc = (isfinite(t1) && t1 > -100.0f && t1 < 200.0f) ? (int16_t)lroundf(t1 * 10.0f) : MON_T_NONE;
        float dose_ws = dose_sample(tsec, prev_time, resync);   // #DOSE-A: NAN, если кривой нет/разрыв/рассинхрон снимка
        ring_push_dose(tsec, counts - prev_counts, (uint16_t)dur, t_dc, dose_ws);
        prev_counts = counts; prev_time = tsec;
    }
}

void monitor_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        ESP_LOGE(TAG, "lock create failed - monitor disabled");
        return;
    }

    s_cap  = MON_RING_PSRAM;
    s_ring = heap_caps_malloc(s_cap * sizeof(monitor_sample_t), MALLOC_CAP_SPIRAM);
    if (!s_ring) {
        // Fallback: internal RAM, час истории — лучше, чем ничего.
        s_cap  = MON_RING_INTERNAL;
        s_ring = malloc(s_cap * sizeof(monitor_sample_t));
        if (s_ring)
            ESP_LOGW(TAG, "PSRAM alloc failed - internal ring %" PRIu32 " samples (1h)", s_cap);
    }
    if (!s_ring) {
        ESP_LOGE(TAG, "ring alloc failed - monitor disabled");
        return;
    }

    // Эпоха рандомизируется на старте: перезапуск платы (кольцо потеряно)
    // выглядит для клиента как смена эпохи → чистый ресинк, а не тихая дыра.
    s_epoch = esp_random() & 0x3FFFFFFF;

    s_commit_sig = xSemaphoreCreateBinary();
    if (s_commit_sig) spectrum_add_commit_listener(s_commit_sig);

    // Core 1 (как wf_task) — подальше от USB-приёма на core 0. Prio 2 хватает:
    // dur считается по времени прибора, опоздание задачи данные не искажает.
    xTaskCreatePinnedToCore(monitor_task, "mon", 4096, NULL, 2, NULL, 1);
    ESP_LOGI(TAG, "ring %" PRIu32 " samples (%" PRIu32 " KB, %s), epoch %" PRIu32,
             s_cap, (uint32_t)(s_cap * sizeof(monitor_sample_t) / 1024),
             s_cap == MON_RING_PSRAM ? "PSRAM" : "internal", s_epoch);
}
