#pragma once
// #GZ-T (1.2.37-rc90, ТЕСТ): чистые решения сжатия gzip тела /api/spectrum.json?channels=N (v2), без IDF.
// Host-тест: tests/host/test_spectrum_gz_plan.c (свой main, make test). Прошивка: main/spectrum_gz.c, boot_config.c, web_server.c.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SPEC_GZ_MIN_BYTES       1024u              // тело короче — не сжимается
#define SPEC_GZ_HDR_BYTES       10u                // заголовок gzip (RFC 1952)
#define SPEC_GZ_TRL_BYTES       8u                 // хвост gzip: CRC32 + ISIZE
#define SPEC_GZ_PSRAM_RESERVE   (192u * 1024u)     // PSRAM, которая остаётся свободной после выделения (consilium 02-embedded §6 п.2)
#define SPEC_GZ_SLOT_ALIGN      4096u              // ёмкость слота растёт шагами по 4 КБ и держится до выключения
#define SPEC_GZ_TDEFL_GREEDY    0x04000            // = TDEFL_GREEDY_PARSING_FLAG (ROM miniz.h); сверка _Static_assert в spectrum_gz.c
#define SPEC_GZ_BOOTCFG_TAIL    ",\"gzip_level\":%u}"   // GET /api/boot-config: дописывается вместо закрывающей '}' ответа

#define SPEC_GZ_PLAIN 0   // отдать как раньше (несжатое), промахом не считается
#define SPEC_GZ_HIT   1   // отдать готовое сжатое тело
#define SPEC_GZ_MISS  2   // клиент просил gzip, готового тела этого поколения нет: несжатое + gz_miss + запрос задаче

// Уровень сжатия: допустимы 0 (выкл), 1, 6.
static inline bool spec_gz_level_valid(int level)
{
    return level == 0 || level == 1 || level == 6;
}

// Уровень из NVS (ключ "gz_lvl"): нет ключа, ошибка чтения или значение не 1/6 = 0 (сжатие выключено).
static inline uint8_t spec_gz_level_from_nvs(bool key_present, uint8_t raw_value)
{
    return (key_present && (raw_value == 1 || raw_value == 6)) ? raw_value : 0;
}

// Флаги tdefl_init (как tdefl_create_comp_flags_from_zip_params miniz, которой нет в ROM): 1 — 1 проба и жадный разбор,
// 6 — 128 проб; сырой deflate без обёртки zlib (рамку gzip пишет spec_gz_frame). Иной уровень — 0.
static inline int spec_gz_tdefl_flags(int level)
{
    if (level == 1) return 1 | SPEC_GZ_TDEFL_GREEDY;
    if (level == 6) return 128;
    return 0;
}

static inline bool spec_gz_ci_eq(const char *s, size_t n, const char *name)
{
    size_t m = strlen(name);
    if (n != m) return false;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != name[i]) return false;
    }
    return true;
}

// Параметры элемента Accept-Encoding (текст после имени, n байт): есть q, равный нулю (q=0, q=0.0, q=0.00, q=0.000)?
static inline bool spec_gz_q_zero(const char *p, size_t n)
{
    for (size_t i = 0; i + 1 < n; i++) {
        if ((p[i] == 'q' || p[i] == 'Q') && p[i + 1] == '=') {
            size_t j = i + 2;
            if (j >= n || p[j] != '0') return false;
            for (j++; j < n && p[j] != ';' && p[j] != ' ' && p[j] != '\t'; j++) {
                if (p[j] != '.' && p[j] != '0') return false;
            }
            return true;
        }
    }
    return false;
}

// Клиент принимает gzip: в Accept-Encoding есть элемент "gzip" (регистр не важен) без q=0. "*", "x-gzip", "deflate" и прочие
// не считаются. NULL / пусто — false.
static inline bool spec_gz_accepts(const char *ae)
{
    if (!ae) return false;
    const char *p = ae;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        const char *s = p;
        while (*p && *p != ',') p++;
        size_t item = (size_t)(p - s);
        size_t tl = 0;
        while (tl < item && s[tl] != ';' && s[tl] != ' ' && s[tl] != '\t') tl++;
        bool zero = spec_gz_q_zero(s + tl, item - tl);
        if (!zero && spec_gz_ci_eq(s, tl, "gzip")) return true;
    }
    return false;
}

// Решение обработчика: level — применённый уровень, client_gzip — spec_gz_accepts(); слот — готовое тело этого вида
// (slot_len 0 = тело того поколения не сжимается); cur_gen — текущее поколение кэша спектра (render count).
static inline int spec_gz_pick(uint8_t level, bool client_gzip, bool slot_valid, uint32_t slot_gen, size_t slot_len,
                               uint32_t cur_gen)
{
    if (level == 0 || !client_gzip) return SPEC_GZ_PLAIN;
    if (!slot_valid || slot_gen != cur_gen) return SPEC_GZ_MISS;
    if (slot_len == 0) return SPEC_GZ_PLAIN;
    return SPEC_GZ_HIT;
}

// Сжимать ли тело этой длины.
static inline bool spec_gz_worth(size_t len)
{
    return len >= SPEC_GZ_MIN_BYTES;
}

// Место под сырой deflate: сжатое тело с рамкой gzip не длиннее несжатого (иначе сжатие не выгодно, ответ прежний).
static inline size_t spec_gz_deflate_room(size_t in_len)
{
    return in_len > SPEC_GZ_HDR_BYTES + SPEC_GZ_TRL_BYTES ? in_len - SPEC_GZ_HDR_BYTES - SPEC_GZ_TRL_BYTES : 0;
}

// Ёмкость буфера слота для тела in_len: in_len, округлённая вверх до SPEC_GZ_SLOT_ALIGN.
static inline size_t spec_gz_slot_cap(size_t in_len)
{
    return (in_len + SPEC_GZ_SLOT_ALIGN - 1) / SPEC_GZ_SLOT_ALIGN * SPEC_GZ_SLOT_ALIGN;
}

// Можно ли выделить need байт PSRAM: есть блок не меньше need и после выделения свободно не меньше SPEC_GZ_PSRAM_RESERVE.
static inline bool spec_gz_mem_ok(size_t need, size_t free_now, size_t largest)
{
    return largest >= need && free_now >= need && free_now - need >= SPEC_GZ_PSRAM_RESERVE;
}

// Рамка gzip вокруг сырого deflate длиной deflate_len, уже лежащего в buf с отступом SPEC_GZ_HDR_BYTES: заголовок
// (ID1 ID2 CM=8 FLG=0 MTIME=0 XFL=0 OS=255) и хвост (CRC32, ISIZE — little-endian). Возврат — длина тела; 0 — не помещается в cap.
static inline size_t spec_gz_frame(uint8_t *buf, size_t cap, size_t deflate_len, uint32_t crc, uint32_t isize)
{
    static const uint8_t h[SPEC_GZ_HDR_BYTES] = { 0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff };
    size_t total = SPEC_GZ_HDR_BYTES + deflate_len + SPEC_GZ_TRL_BYTES;
    if (deflate_len > cap || total > cap) return 0;
    memcpy(buf, h, sizeof h);
    uint8_t *t = buf + SPEC_GZ_HDR_BYTES + deflate_len;
    for (int i = 0; i < 4; i++) {
        t[i] = (uint8_t)(crc >> (8 * i));
        t[4 + i] = (uint8_t)(isize >> (8 * i));
    }
    return total;
}

// ---- Ф4.2 (D3, 1.2.38): асинхронная отдача сжатого тела (задача spec_gz, очередь глубины 2) ----
#define SPEC_GZ_ASYNC          3
#define SPEC_GZ_QUEUE_DEPTH    2u
#define SPEC_GZ_DEADLINE_MS    1500u
#define SPEC_GZ_RING_N         16u
#define SPEC_GZ_ASYNC_MEM_PROBE (64u * 1024u)

static inline bool spec_gz_decide(bool accepts, bool recording, uint8_t level, bool mem_ok, bool queue_full) {
    if (!accepts) return false;
    if (recording) return false;
    if (level != 1 && level != 6) return false;
    if (!mem_ok) return false;
    if (queue_full) return false;
    return true;
}

static inline int spec_gz_route(int pick, bool decide_ok) {
    if (pick == SPEC_GZ_HIT) return SPEC_GZ_HIT;
    if (pick == SPEC_GZ_MISS && decide_ok) return SPEC_GZ_ASYNC;
    return SPEC_GZ_PLAIN;
}

static inline bool spec_gz_expired(uint32_t now_ms, uint32_t enq_ms) {
    return (uint32_t)(now_ms - enq_ms) > SPEC_GZ_DEADLINE_MS;
}

typedef struct { uint8_t head; uint8_t count; } spec_gz_q_t;

static inline bool spec_gz_q_full(const spec_gz_q_t *q) {
    return q->count >= SPEC_GZ_QUEUE_DEPTH;
}

static inline int spec_gz_q_reserve(spec_gz_q_t *q) {
    if (q->count >= SPEC_GZ_QUEUE_DEPTH) return -1;
    uint8_t idx = (q->head + q->count) % SPEC_GZ_QUEUE_DEPTH;
    q->count++;
    return idx;
}

static inline int spec_gz_q_pop(spec_gz_q_t *q) {
    if (q->count == 0) return -1;
    uint8_t idx = q->head;
    q->head = (q->head + 1) % SPEC_GZ_QUEUE_DEPTH;
    q->count--;
    return idx;
}

static inline void spec_gz_q_unreserve(spec_gz_q_t *q) {
    if (q->count > 0) q->count--;
}

typedef struct { uint32_t v[SPEC_GZ_RING_N]; uint32_t n; } spec_gz_ring_t;

static inline void spec_gz_ring_put(spec_gz_ring_t *r, uint32_t us) {
    r->v[r->n % SPEC_GZ_RING_N] = us;
    r->n++;
}

static inline size_t spec_gz_ring_get(const spec_gz_ring_t *r, uint32_t *out) {
    size_t cnt = r->n < SPEC_GZ_RING_N ? r->n : SPEC_GZ_RING_N;
    for (size_t i = 0; i < cnt; i++) {
        out[i] = r->v[(r->n - cnt + i) % SPEC_GZ_RING_N];
    }
    return cnt;
}
