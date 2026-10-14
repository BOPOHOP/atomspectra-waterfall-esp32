#include "live_time.h"
#include "test_util.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>

#define NCHECK(name, cond) do { if (!(cond)) { printf("FAIL %s %s:%d: %s\n", name, __FILE__, __LINE__, #cond); g_failures++; } } while (0)

/* дословная копия static compute_live_time из web_server.c / spectrum_http_cache.c на f97ad6b (тело после чтения device_info) */
static float old_live_time(uint32_t total_time_sec, uint32_t total_counts, uint32_t lost, bool di_valid, uint16_t rise, uint16_t fall, float freq)
{
    float total = (float)total_time_sec;
    if (!di_valid || freq <= 0.0f) return total;
    float tau = ((float)rise + (float)fall + 1.0f) / freq;
    float dead = (float)(total_counts + lost) * tau;
    if (dead < 0.0f) dead = 0.0f;
    if (dead > total) dead = total;
    return total - dead;
}

static int same_bits(float a, float b) { return memcmp(&a, &b, sizeof(float)) == 0; }

static void test_live_random_eq(void)
{
    uint32_t seed = 12345u;
    int n = 0;
    int differ = 0;
    uint32_t tries = 0;
    const uint32_t MAX_TRIES = 5000000u;

    while (n < 10000 && tries < MAX_TRIES)
    {
        tries++;
        seed = seed * 1103515245u + 12345u;
        uint32_t r1 = (seed >> 16) & 0xFFFFu;
        seed = seed * 1103515245u + 12345u;
        uint32_t r2 = (seed >> 16) & 0xFFFFu;
        seed = seed * 1103515245u + 12345u;
        uint32_t r3 = (seed >> 16) & 0xFFFFu;
        seed = seed * 1103515245u + 12345u;
        uint32_t r4 = (seed >> 16) & 0xFFFFu;
        seed = seed * 1103515245u + 12345u;
        uint32_t r5 = (seed >> 16) & 0xFFFFu;
        seed = seed * 1103515245u + 12345u;
        uint32_t r6 = (seed >> 16) & 0xFFFFu;

        uint32_t total_time_sec = 100u + (r1 % 99901u);
        uint16_t rise = (uint16_t)(1u + (r2 % 500u));
        uint16_t fall = (uint16_t)(1u + (r3 % 500u));
        float freq = 1.0e5f + (float)(r4 % 10000u) / 10000.0f * 9.9e6f;
        uint32_t total_counts = r5 % 1000001u;
        uint32_t lost = r6 % 1001u;

        double dd = ((double)total_counts + (double)lost) * (((double)rise + (double)fall + 1.0) / (double)freq);
        if (dd > 0.001 * (double)total_time_sec && dd < 0.9 * (double)total_time_sec)
        {
            float new_res = live_time_calc(total_time_sec, total_counts, lost, true, rise, fall, freq);
            float old_res = old_live_time(total_time_sec, total_counts, lost, true, rise, fall, freq);
            if (!same_bits(new_res, old_res))
            {
                differ++;
            }
            n++;
        }
    }

    printf("INFO live_random_eq: samples=%d differ=%d\n", n, differ);
    NCHECK("live_random_eq", n == 10000 && differ == 0);
}

static void test_live_clamp_hi(void)
{
    float r1 = live_time_calc(10u, 1000000u, 0u, true, 100u, 100u, 1.0e6f);
    float o1 = old_live_time(10u, 1000000u, 0u, true, 100u, 100u, 1.0e6f);
    NCHECK("live_clamp_hi", r1 == 0.0f);
    NCHECK("live_clamp_hi", same_bits(r1, o1));

    float r2 = live_time_calc(1u, 0xFFFFFFFFu, 0u, true, 1u, 1u, 1.0e3f);
    float o2 = old_live_time(1u, 0xFFFFFFFFu, 0u, true, 1u, 1u, 1.0e3f);
    NCHECK("live_clamp_hi", r2 == 0.0f);
    NCHECK("live_clamp_hi", same_bits(r2, o2));

    float r3 = live_time_calc(60u, 5000000u, 5000000u, true, 300u, 300u, 1.0e6f);
    float o3 = old_live_time(60u, 5000000u, 5000000u, true, 300u, 300u, 1.0e6f);
    NCHECK("live_clamp_hi", r3 == 0.0f);
    NCHECK("live_clamp_hi", same_bits(r3, o3));

    printf("INFO live_clamp_hi: cases=3\n");
}

static void test_live_freq_nonpos(void)
{
    uint32_t total = 1000u;
    uint32_t counts = 1000000u;
    uint32_t lost = 100u;
    uint16_t rise = 10u;
    uint16_t fall = 10u;

    float r1 = live_time_calc(total, counts, lost, true, rise, fall, 0.0f);
    NCHECK("live_freq_nonpos", r1 == (float)total);

    float r2 = live_time_calc(total, counts, lost, true, rise, fall, -1.0f);
    NCHECK("live_freq_nonpos", r2 == (float)total);

    float r3 = live_time_calc(total, counts, lost, true, rise, fall, -1.0e9f);
    NCHECK("live_freq_nonpos", r3 == (float)total);
}

static void test_live_invalid(void)
{
    uint32_t total = 1000u;
    uint32_t counts = 1000000u;
    uint32_t lost = 100u;
    uint16_t rise = 10u;
    uint16_t fall = 10u;
    float freq = 1.0e6f;

    float r = live_time_calc(total, counts, lost, false, rise, fall, freq);
    NCHECK("live_invalid", r == (float)total);
}

static void test_live_overflow(void)
{
    uint32_t total = 1000u;
    uint16_t rise = 100u;
    uint16_t fall = 100u;
    float freq = 1.0e6f;

    float r1 = live_time_calc(total, 0xFFFFFFFFu, 1u, true, rise, fall, freq);
    NCHECK("live_overflow", r1 == 1000.0f);

    float r2 = live_time_calc(total, 0x80000000u, 0x80000000u, true, rise, fall, freq);
    NCHECK("live_overflow", r2 == (float)total);
}

static void test_live_zero_counts(void)
{
    uint32_t total = 12345u;
    uint32_t counts = 0u;
    uint32_t lost = 0u;
    uint16_t rise = 10u;
    uint16_t fall = 10u;
    float freq = 1.0e6f;

    float r = live_time_calc(total, counts, lost, true, rise, fall, freq);
    NCHECK("live_zero_counts", r == 12345.0f);
}

void live_time_suite(void)
{
    test_live_random_eq();
    test_live_clamp_hi();
    test_live_freq_nonpos();
    test_live_invalid();
    test_live_overflow();
    test_live_zero_counts();
}
