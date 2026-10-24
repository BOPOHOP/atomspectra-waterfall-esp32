/* ===== FILE: test_mdns_txt_plan.c ===== */
#include "mdns_txt_plan.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int t_with_sn(void) {
    mdns_txt_kv_t out[MDNS_TXT_MAX];
    size_t n = mdns_txt_plan("00000001", "1.2.39", 80, out);
    if (n != 4) return 0;
    if (strcmp(out[0].key, "sn") != 0) return 0;
    if (strcmp(out[1].key, "fw") != 0) return 0;
    if (strcmp(out[2].key, "rest") != 0) return 0;
    if (strcmp(out[3].key, "proto") != 0) return 0;
    if (strcmp(out[0].val, "00000001") != 0) return 0;
    if (strcmp(out[1].val, "1.2.39") != 0) return 0;
    if (strcmp(out[2].val, "80") != 0) return 0;
    if (strcmp(out[3].val, "1") != 0) return 0;
    return 1;
}

static int t_no_sn_null(void) {
    mdns_txt_kv_t out[MDNS_TXT_MAX];
    size_t n = mdns_txt_plan(NULL, "1.2.39", 80, out);
    if (n != 3) return 0;
    if (strcmp(out[0].key, "fw") != 0) return 0;
    for (size_t i = 0; i < n; i++) {
        if (strcmp(out[i].key, "sn") == 0) return 0;
    }
    return 1;
}

static int t_no_sn_empty(void) {
    mdns_txt_kv_t out[MDNS_TXT_MAX];
    size_t n = mdns_txt_plan("", "1.2.39", 80, out);
    if (n != 3) return 0;
    for (size_t i = 0; i < n; i++) {
        if (strcmp(out[i].key, "sn") == 0) return 0;
    }
    return 1;
}

static int t_fw_default(void) {
    mdns_txt_kv_t out[MDNS_TXT_MAX];
    size_t n = mdns_txt_plan("00000001", NULL, 80, out);
    if (n != 4) return 0;
    if (strcmp(out[1].val, "?") != 0) return 0;
    n = mdns_txt_plan("00000001", "", 80, out);
    if (n != 4) return 0;
    if (strcmp(out[1].val, "?") != 0) return 0;
    return 1;
}

static int t_long_sn(void) {
    char sn[101];
    memset(sn, 'A', 100);
    sn[100] = '\0';
    mdns_txt_kv_t out[MDNS_TXT_MAX];
    size_t n = mdns_txt_plan(sn, "1.2.39", 80, out);
    if (n != 4) return 0;
    if (strlen(out[0].val) != MDNS_TXT_VLEN - 1) return 0;
    if (out[0].val[MDNS_TXT_VLEN - 1] != '\0') return 0;
    return 1;
}

static int t_changed(void) {
    if (mdns_txt_sn_changed("", NULL) != 0) return 0;
    if (mdns_txt_sn_changed(NULL, "") != 0) return 0;
    if (mdns_txt_sn_changed("", "00000001") != 1) return 0;
    if (mdns_txt_sn_changed("00000001", "00000001") != 0) return 0;
    if (mdns_txt_sn_changed("00000001", "00000002") != 1) return 0;
    if (mdns_txt_sn_changed("00000001", "") != 1) return 0;
    return 1;
}

static int t_sn_63(void) {
    char sn[64];
    memset(sn, 'B', 63);
    sn[63] = '\0';
    mdns_txt_kv_t out[MDNS_TXT_MAX];
    size_t n = mdns_txt_plan(sn, "1.2.39", 80, out);
    return n == 4 && strlen(out[0].val) == 63 && strcmp(out[0].val, sn) == 0;
}

int main(void) {
    int failures = 0;
    if (t_sn_63()) printf("PASS t_sn_63\n"); else { printf("FAIL t_sn_63\n"); failures++; }
    if (t_with_sn()) printf("PASS t_with_sn\n"); else { printf("FAIL t_with_sn\n"); failures++; }
    if (t_no_sn_null()) printf("PASS t_no_sn_null\n"); else { printf("FAIL t_no_sn_null\n"); failures++; }
    if (t_no_sn_empty()) printf("PASS t_no_sn_empty\n"); else { printf("FAIL t_no_sn_empty\n"); failures++; }
    if (t_fw_default()) printf("PASS t_fw_default\n"); else { printf("FAIL t_fw_default\n"); failures++; }
    if (t_long_sn()) printf("PASS t_long_sn\n"); else { printf("FAIL t_long_sn\n"); failures++; }
    if (t_changed()) printf("PASS t_changed\n"); else { printf("FAIL t_changed\n"); failures++; }
    return failures;
}
