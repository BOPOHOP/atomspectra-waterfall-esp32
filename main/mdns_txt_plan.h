#pragma once
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define MDNS_TXT_PROTO "1"
#define MDNS_TXT_MAX   4
#define MDNS_TXT_VLEN  72

typedef struct { const char *key; char val[MDNS_TXT_VLEN]; } mdns_txt_kv_t;

static inline size_t mdns_txt_plan(const char *sn, const char *fw, int rest_port, mdns_txt_kv_t out[MDNS_TXT_MAX]) {
    size_t n = 0;
    if (sn != NULL && sn[0] != 0) {
        out[n].key = "sn";
        snprintf(out[n].val, MDNS_TXT_VLEN, "%s", sn);
        n++;
    }
    out[n].key = "fw";
    snprintf(out[n].val, MDNS_TXT_VLEN, "%s", (fw != NULL && fw[0] != 0) ? fw : "?");
    n++;
    out[n].key = "rest";
    snprintf(out[n].val, MDNS_TXT_VLEN, "%d", rest_port);
    n++;
    out[n].key = "proto";
    snprintf(out[n].val, MDNS_TXT_VLEN, "%s", MDNS_TXT_PROTO);
    n++;
    return n;
}

static inline int mdns_txt_sn_changed(const char *published, const char *current) {
    const char *a = (published != NULL) ? published : "";
    const char *b = (current != NULL) ? current : "";
    return (strcmp(a, b) != 0) ? 1 : 0;
}

