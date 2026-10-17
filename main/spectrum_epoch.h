#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "seq_copy.h"

typedef struct { uint32_t dev_resets; uint32_t spec_resets; } spec_epoch_t;

static inline uint32_t spec_fmix32(uint32_t h)
{
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return h;
}

static inline uint32_t spec_boot_id_mix(uint32_t rnd, uint32_t mac_lo32, uint32_t timer_lo32)
{
    uint32_t id = spec_fmix32(rnd ^ mac_lo32 ^ timer_lo32);
    return id == 0u ? 1u : id;
}

static inline void spec_epoch_on_fold(uint32_t *dev_resets, uint32_t *spec_resets)
{
    (void)spec_resets;
    (*dev_resets)++;
}

static inline void spec_epoch_on_user_reset(uint32_t *dev_resets, uint32_t *spec_resets)
{
    (void)dev_resets;
    (*spec_resets)++;
}

typedef struct {
    void *sp_dst;
    const void *sp_src;
    size_t sp_n;
    void (*copy)(void *, const void *, size_t);
    void (*read_epoch)(spec_epoch_t *out);
    spec_epoch_t *ep_dst;
} spec_epoch_copy_ctx_t;

static inline void spec_epoch_copy_cb(void *dst, const void *src, size_t n)
{
    (void)src;
    (void)n;
    spec_epoch_copy_ctx_t *c = (spec_epoch_copy_ctx_t *)dst;
    c->copy(c->sp_dst, c->sp_src, c->sp_n);
    c->read_epoch(c->ep_dst);   /* EPOCH_IN_WINDOW */
}

static inline bool spec_epoch_snapshot_seq(const volatile uint32_t *seq, void *sp_dst, const void *sp_src, size_t sp_n,
                                           spec_epoch_t *ep_dst, void (*read_epoch)(spec_epoch_t *out),
                                           int tries, void (*copy)(void *, const void *, size_t),
                                           void (*wait)(void), uint32_t *retries)
{
    spec_epoch_copy_ctx_t c = { sp_dst, sp_src, sp_n, copy, read_epoch, ep_dst };
    bool ok = seq_copy(seq, &c, NULL, 0, tries, spec_epoch_copy_cb, wait, retries);
    return ok;   /* EPOCH_RET */
}
