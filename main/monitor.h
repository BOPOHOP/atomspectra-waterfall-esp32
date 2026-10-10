// #MON-1 (#FW-40): мониторинг CPS на плате — кольцо базовых 1с-сэмплов
// (дельты по коммитам свипов). Браузер больше НЕ копит историю сам (фоновая
// вкладка троттлит таймер до ~60 с → рваные Δt, закрытая — дыра в данных),
// а только дорисовывает серию через GET /api/monitor/series?since=<seq>.
#pragma once

#include <stdint.h>
#include <stddef.h>

// Базовый сэмпл серии: дельта одного (или нескольких слитых) коммитов свипа.
// 12 Б в упаковке (с 1.2.31 — с температурой); кольцо — см. MON_RING_PSRAM в monitor.c.
typedef struct __attribute__((packed)) {
    uint32_t end_sec;   // total_time_sec прибора на момент коммита (наборное время)
    uint32_t counts;    // прирост total_counts за dur
    uint16_t dur;       // прирост total_time_sec, с (обычно 1; больше при пропусках свипов)
    int16_t  t_dc;      // #MX-12 (1.2.31): температура T1 прибора, 0,1 °C; MON_T_NONE — нет данных
} monitor_sample_t;     // 12 Б

#define MON_T_NONE INT16_MIN

// Поднять кольцо + задачу-подписчика на коммиты свипов. Вызывать из app_main()
// ПОСЛЕ spectrum_init() (spectrum_get_totals ходит под SPEC_LOCK).
void monitor_init(void);

// Снимок хвоста серии под внутренним локом (атомарно: сэмплы + курсоры + epoch
// одной точки времени). Копирует в out до max сэмплов с seq >= since+1
// (клампится к старейшему в кольце). Возвращает число скопированных.
// *out_first_seq — seq сэмпла out[0] (= *out_next_seq, если отдать нечего);
// *out_next_seq  — seq будущего сэмпла (последний записанный + 1);
// *out_epoch     — эпоха серии (меняется при откате счётчиков прибора и
//                  рандомизирована на старте платы — перезапуск виден клиенту).
size_t monitor_copy_since(uint32_t since, monitor_sample_t *out, size_t max,
                          uint32_t *out_first_seq, uint32_t *out_next_seq,
                          uint32_t *out_epoch);

// 1.2.36 #MX-14: SMA «отношение сумм за окно» прямо по кольцу, под s_lock, без копии (monitor_window.h).
// w_sec клампится к [1, ёмкость кольца]. *sum_counts / *sum_dur — Σcounts / Σdur последних сэмплов, пока Σdur < W
// (сэмпл, на котором Σdur достигает W, включается); *n — число сэмплов. Пустое кольцо / монитор отключён — нули.
void monitor_window(uint32_t w_sec, uint64_t *sum_counts, uint32_t *sum_dur, uint32_t *n);
