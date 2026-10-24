#ifndef CMDLOG_H
#define CMDLOG_H

#include <stdint.h>
#include <string.h>

#define CMDLOG_N    32
#define CMDLOG_TEXT 48

typedef struct {
    uint32_t seq;
    uint32_t uptime_ms;
    uint8_t  src;
    int8_t   delivered;
    int16_t  rc;
    uint8_t  term_nul;
    char     text[CMDLOG_TEXT];
} cmdlog_entry_t;

typedef struct { cmdlog_entry_t e[CMDLOG_N]; uint32_t next_seq; } cmdlog_t;

static inline void cmdlog_init(cmdlog_t *l) {
    memset(l, 0, sizeof(*l));
    l->next_seq = 1;
}

static inline uint32_t cmdlog_push(cmdlog_t *l, uint32_t uptime_ms, uint8_t src, const char *text, size_t len, int term_nul) {
    uint32_t seq = l->next_seq++;
    uint32_t slot = (seq - 1) % CMDLOG_N;
    cmdlog_entry_t *e = &l->e[slot];
    e->seq = seq;
    e->uptime_ms = uptime_ms;
    e->src = src;
    e->delivered = -1;
    e->rc = 0;
    e->term_nul = (uint8_t)term_nul;
    size_t n = len;
    if (n > CMDLOG_TEXT - 1) n = CMDLOG_TEXT - 1;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)text[i];
        e->text[i] = (c < 0x20 || c > 0x7E) ? '.' : (char)c;
    }
    e->text[n] = '\0';
    return seq;
}

static inline void cmdlog_set_result(cmdlog_t *l, uint32_t seq, int delivered, int rc) {
    if (seq == 0) return;
    uint32_t slot = (seq - 1) % CMDLOG_N;
    if (l->e[slot].seq != seq) return;
    l->e[slot].delivered = (int8_t)delivered;
    l->e[slot].rc = (int16_t)rc;
}

static inline const char *cmdlog_src_name(uint8_t src) {
    switch (src) {
        case 1: return "rest_command";
        case 2: return "rest_reset";
        case 3: return "tcp_bridge";
        case 4: return "web_ui";
        case 5: return "autostart";
        default: return "unknown";
    }
}

static inline size_t cmdlog_json(const cmdlog_t *l, uint32_t since, char *out, size_t cap) {
    uint32_t present[CMDLOG_N];
    int np = 0;
    uint32_t first_present = 0;
    for (int i = 0; i < CMDLOG_N; i++) {
        uint32_t s = l->e[i].seq;
        if (s != 0 && s > since) {
            present[np++] = s;
            if (first_present == 0 || s < first_present) first_present = s;
        }
    }
    for (int i = 1; i < np; i++) {
        uint32_t v = present[i];
        int j = i - 1;
        while (j >= 0 && present[j] > v) { present[j+1] = present[j]; j--; }
        present[j+1] = v;
    }
    uint32_t dropped = 0;
    if (np > 0) {
        if (first_present > since + 1) dropped = first_present - since - 1;
    } else {
        uint32_t last = l->next_seq - 1;
        if (last > since) dropped = last - since;
    }

    size_t wlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
    size_t wnum(uint32_t v) {
        if (v == 0) return 1;
        size_t n = 0; while (v) { v /= 10; n++; } return n;
    }
    size_t wnumi(int v) {
        if (v < 0) { v = -v; return wnum((uint32_t)v) + 1; }
        return wnum((uint32_t)v);
    }
    size_t wcmd(const char *s) {
        size_t n = 0;
        for (const char *p = s; *p; p++) {
            if (*p == '"' || *p == '\\') n += 2;
            else n += 1;
        }
        return n;
    }

    size_t total = 0;
    total += wlen("{\"next\":");
    total += wnum(l->next_seq);
    total += wlen(",\"dropped\":");
    total += wnum(dropped);
    total += wlen(",\"entries\":[");
    for (int i = 0; i < np; i++) {
        uint32_t s = present[i];
        uint32_t slot = (s - 1) % CMDLOG_N;
        const cmdlog_entry_t *e = &l->e[slot];
        total += wlen("{\"seq\":");
        total += wnum(s);
        total += wlen(",\"t\":");
        total += wnum(e->uptime_ms);
        total += wlen(",\"src\":\"");
        total += wlen(cmdlog_src_name(e->src));
        total += wlen("\",\"delivered\":");
        if (e->delivered == -1) total += wlen("null");
        else if (e->delivered == 1) total += wlen("true");
        else total += wlen("false");
        total += wlen(",\"rc\":");
        total += wnumi(e->rc);
        total += wlen(",\"nul\":");
        total += (e->term_nul) ? wlen("true") : wlen("false");
        total += wlen(",\"cmd\":\"");
        total += wcmd(e->text);
        total += wlen("\"}");
        if (i < np - 1) total += 1;
    }
    total += wlen("]}");

    if (out == NULL) return total;
    if (total + 1 > cap) { out[0] = 0; return 0; }

    size_t pos = 0;
    #define W(s) do { size_t _n = strlen(s); memcpy(out + pos, s, _n); pos += _n; } while(0)
    #define WN(v) do { char _b[16]; int _i = 0; uint32_t _x = (v); if (_x == 0) _b[_i++] = '0'; else { char _t[16]; int _k = 0; while (_x) { _t[_k++] = (char)('0' + _x % 10); _x /= 10; } while (_k) _b[_i++] = _t[--_k]; } memcpy(out + pos, _b, _i); pos += _i; } while(0)
    #define WNI(v) do { int _v = (v); if (_v < 0) { out[pos++] = '-'; WN((uint32_t)(-_v)); } else WN((uint32_t)_v); } while(0)
    #define WC(s) do { for (const char *_p = s; *_p; _p++) { if (*_p == '"' || *_p == '\\') out[pos++] = '\\'; out[pos++] = *_p; } } while(0)

    W("{\"next\":");
    WN(l->next_seq);
    W(",\"dropped\":");
    WN(dropped);
    W(",\"entries\":[");
    for (int i = 0; i < np; i++) {
        uint32_t s = present[i];
        uint32_t slot = (s - 1) % CMDLOG_N;
        const cmdlog_entry_t *e = &l->e[slot];
        W("{\"seq\":");
        WN(s);
        W(",\"t\":");
        WN(e->uptime_ms);
        W(",\"src\":\"");
        W(cmdlog_src_name(e->src));
        W("\",\"delivered\":");
        if (e->delivered == -1) W("null");
        else if (e->delivered == 1) W("true");
        else W("false");
        W(",\"rc\":");
        WNI(e->rc);
        W(",\"nul\":");
        if (e->term_nul) W("true"); else W("false");
        W(",\"cmd\":\"");
        WC(e->text);
        W("\"}");
        if (i < np - 1) out[pos++] = ',';
    }
    W("]}");
    out[pos] = '\0';
    #undef W
    #undef WN
    #undef WNI
    #undef WC
    return pos;
}

#endif
