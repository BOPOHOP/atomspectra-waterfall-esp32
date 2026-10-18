#!/usr/bin/env bash
# Ф4.2 мутанты: каждый красит РОВНО названный тест (decide_*, route_miss_async, job_timeout, epoch_atomic_k2), baseline - 0 FAIL.
set -u
cd "$(dirname "$0")"
. ./mutlib.sh
export MT_TARGET=test-gz-keepgoing
P=../../main/spectrum_gz_plan.h; J=../../main/spectrum_gz_job.h; C=../../main/spectrum_http_cache.c
mt_init "$P" "$J" "$C"
mt_mut MG_accepts   decide_accepts   "$P" 's/if (!accepts) return false;/(void)accepts;/'
mt_mut MG_recording decide_recording "$P" 's/if (recording) return false;/(void)recording;/'
mt_mut MG_level     decide_level     "$P" 's/if (level != 1 && level != 6) return false;/(void)level;/'
mt_mut MG_mem       decide_mem       "$P" 's/if (!mem_ok) return false;/(void)mem_ok;/'
mt_mut MG_queue     decide_queue     "$P" 's/if (queue_full) return false;/(void)queue_full;/'
mt_mut MR_old_rc90  route_miss_async "$P" 's/decide_ok) return SPEC_GZ_ASYNC;/decide_ok) return SPEC_GZ_PLAIN;/'
mt_mut MD_no_deadline job_timeout    "$J" 's/if (spec_gz_expired(o->now_ms(c), j->enq_ms)) {/if (0) {/'
mt_mut MD2_never_expired "expired_over job_timeout" "$P" 's/> SPEC_GZ_DEADLINE_MS;/> 0xFFFFFFFFu;/'
mt_mut ME_epoch_outside epoch_atomic_k2 "$C" '/^    ep = s_ep;/d;s/^    size_t cap = (size_t)n \* 6 + 2048;/    ep = s_ep;\n&/'
mt_mut MF1_no_grow acquire_big_bins "$C" 's|^    if (l >= cap) {   // F1|    if (0) {   // F1|'
mt_mut MF2_no_close_gz job_send_err "$J" '\|o->close_session(c, j);   // F2|d'
mt_mut MF2_no_close_plain job_plain_err "$J" '/^        o->close_session(c, j);$/d'
mt_mut MF7_no_have acquire_stale "$C" 's/if (!s_have || !s_sp/if (!s_sp/'
mt_base
mt_done
