#pragma once
#include <stdbool.h>
#include <stdint.h>
/* 1.2.31: новая сессия платы после Сброса спектра. Решения без ESP-IDF — tests/host/test_session_plan.c */
typedef struct { uint32_t sess, seq, seen_req; int64_t due_us; int fail_streak; } sess_sched_t;
/* Сброс открывает сессию, только если было что сбрасывать: досылка -rst, очистка пустого, двойной клик — нет. */
static inline bool session_reset_opens(bool valid, uint32_t total_time_sec)
{ return valid || total_time_sec != 0; }
static inline bool session_need_bump(const sess_sched_t *s, uint32_t req_now)
{ return s->sess != 0 && req_now != s->seen_req; }
/* new_sess — результат boot_config_bump_session(s->sess); 0 = NVS не записан. */
static inline void session_apply_bump(sess_sched_t *s, uint32_t req_now, uint32_t new_sess)
{
    s->seen_req = req_now;                /* погашен и при отказе: не писать NVS каждые 10 с */
    if (new_sess <= s->sess) return;      /* отказ: прежние sess и seq, имя не повторится */
    s->sess = new_sess; s->seq = 0; s->due_us = 0; s->fail_streak = 0;
}
/* Снимок годен для текущей сессии, если между решением и memcpy не было Сброса. */
static inline bool session_snap_current(uint32_t req_at_snap, uint32_t req_expected)
{ return req_at_snap == req_expected; }
