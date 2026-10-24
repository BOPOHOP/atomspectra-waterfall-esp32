/* ===== FILE: test_cmdlog.c ===== */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "cmdlog.h"

static int failures = 0;

static void t_first_seq(void) {
    cmdlog_t l; cmdlog_init(&l);
    uint32_t a = cmdlog_push(&l, 100, 1, "a", 1, 1);
    uint32_t b = cmdlog_push(&l, 200, 1, "b", 1, 1);
    if (a == 1 && b == 2) printf("PASS t_first_seq\n");
    else { printf("FAIL t_first_seq\n"); failures++; }
}

static void t_wrap(void) {
    cmdlog_t l; cmdlog_init(&l);
    for (int i = 0; i < 40; i++) cmdlog_push(&l, (uint32_t)i, 1, "x", 1, 1);
    char buf[8192];
    size_t n = cmdlog_json(&l, 0, buf, sizeof(buf));
    if (n == 0) { printf("FAIL t_wrap\n"); failures++; return; }
    int ok = 1;
    if (strstr(buf, "\"dropped\":8") == NULL) ok = 0;
    int cnt = 0;
    for (const char *p = buf; (p = strstr(p, "\"seq\":")) != NULL; p++) cnt++;
    if (cnt != 32) ok = 0;
    if (strstr(buf, "\"seq\":1,") != NULL) ok = 0;
    if (strstr(buf, "\"seq\":8,") != NULL) ok = 0;
    if (strstr(buf, "\"seq\":9,") == NULL) ok = 0;
    if (strstr(buf, "\"seq\":40,") == NULL) ok = 0;
    if (ok) printf("PASS t_wrap\n");
    else { printf("FAIL t_wrap\n"); failures++; }
}

static void t_since(void) {
    cmdlog_t l; cmdlog_init(&l);
    for (int i = 0; i < 40; i++) cmdlog_push(&l, (uint32_t)i, 1, "x", 1, 1);
    char buf[8192];
    size_t n = cmdlog_json(&l, 35, buf, sizeof(buf));
    if (n == 0) { printf("FAIL t_since\n"); failures++; return; }
    int ok = 1;
    if (strstr(buf, "\"dropped\":0") == NULL) ok = 0;
    int cnt = 0;
    for (const char *p = buf; (p = strstr(p, "\"seq\":")) != NULL; p++) cnt++;
    if (cnt != 5) ok = 0;
    if (strstr(buf, "\"seq\":36,") == NULL) ok = 0;
    if (strstr(buf, "\"seq\":40,") == NULL) ok = 0;
    if (strstr(buf, "\"seq\":35,") != NULL) ok = 0;
    if (ok) printf("PASS t_since\n");
    else { printf("FAIL t_since\n"); failures++; }
}

static void t_set_result(void) {
    cmdlog_t l; cmdlog_init(&l);
    uint32_t s1 = cmdlog_push(&l, 10, 1, "a", 1, 1);
    cmdlog_set_result(&l, s1, 1, 0);
    char buf[8192];
    size_t n = cmdlog_json(&l, 0, buf, sizeof(buf));
    if (n == 0) { printf("FAIL t_set_result\n"); failures++; return; }
    int ok = strstr(buf, "\"delivered\":true") != NULL && strstr(buf, "\"rc\":0") != NULL;
    cmdlog_set_result(&l, s1, 0, -5);
    n = cmdlog_json(&l, 0, buf, sizeof(buf));
    if (n == 0) { printf("FAIL t_set_result\n"); failures++; return; }
    if (strstr(buf, "\"delivered\":false") == NULL || strstr(buf, "\"rc\":-5") == NULL) ok = 0;
    for (int i = 0; i < 40; i++) cmdlog_push(&l, (uint32_t)i, 1, "x", 1, 1);
    cmdlog_set_result(&l, s1, 1, 99);
    n = cmdlog_json(&l, 0, buf, sizeof(buf));
    if (n == 0) { printf("FAIL t_set_result\n"); failures++; return; }
    if (strstr(buf, "\"rc\":99") != NULL) ok = 0;
    if (ok) printf("PASS t_set_result\n");
    else { printf("FAIL t_set_result\n"); failures++; }
}

static void t_trunc(void) {
    cmdlog_t l; cmdlog_init(&l);
    char big[101];
    for (int i = 0; i < 100; i++) big[i] = 'a';
    big[100] = '\0';
    cmdlog_push(&l, 10, 1, big, 100, 1);
    if (strlen(l.e[0].text) == 47) printf("PASS t_trunc\n");
    else { printf("FAIL t_trunc\n"); failures++; }
}

static void t_nonprint(void) {
    cmdlog_t l; cmdlog_init(&l);
    char t[3] = { 0x01, 0xFF, '\0' };
    cmdlog_push(&l, 10, 1, t, 2, 0);
    if (l.e[0].text[0] == '.' && l.e[0].text[1] == '.') printf("PASS t_nonprint\n");
    else { printf("FAIL t_nonprint\n"); failures++; }
}

static void t_escape(void) {
    cmdlog_t l; cmdlog_init(&l);
    const char *t = "a\"b\\c";
    cmdlog_push(&l, 10, 1, t, 5, 1);
    char buf[8192];
    size_t n = cmdlog_json(&l, 0, buf, sizeof(buf));
    if (n == 0) { printf("FAIL t_escape\n"); failures++; return; }
    if (strstr(buf, "\"cmd\":\"a\\\"b\\\\c\"") != NULL) printf("PASS t_escape\n");
    else { printf("FAIL t_escape\n"); failures++; }
}

static void t_null_delivered(void) {
    cmdlog_t l; cmdlog_init(&l);
    cmdlog_push(&l, 10, 1, "a", 1, 1);
    char buf[8192];
    size_t n = cmdlog_json(&l, 0, buf, sizeof(buf));
    if (n == 0) { printf("FAIL t_null_delivered\n"); failures++; return; }
    if (strstr(buf, "\"delivered\":null") != NULL) printf("PASS t_null_delivered\n");
    else { printf("FAIL t_null_delivered\n"); failures++; }
}

static void t_cap(void) {
    cmdlog_t l; cmdlog_init(&l);
    cmdlog_push(&l, 10, 1, "a", 1, 1);
    size_t need = cmdlog_json(&l, 0, NULL, 0);
    char buf[8192];
    size_t r = cmdlog_json(&l, 0, buf, need);
    if (r == 0 && buf[0] == 0) {
        r = cmdlog_json(&l, 0, buf, need + 1);
        if (r == need && buf[need] == '\0') printf("PASS t_cap\n");
        else { printf("FAIL t_cap\n"); failures++; }
    } else { printf("FAIL t_cap\n"); failures++; }
}

static void t_src_names(void) {
    cmdlog_t l; cmdlog_init(&l);
    cmdlog_push(&l, 10, 3, "a", 1, 1);
    cmdlog_push(&l, 20, 9, "b", 1, 1);
    char buf[8192];
    size_t n = cmdlog_json(&l, 0, buf, sizeof(buf));
    if (n == 0) { printf("FAIL t_src_names\n"); failures++; return; }
    if (strstr(buf, "\"src\":\"tcp_bridge\"") != NULL && strstr(buf, "\"src\":\"unknown\"") != NULL)
        printf("PASS t_src_names\n");
    else { printf("FAIL t_src_names\n"); failures++; }
}

static void t_nul_flag(void) {
    cmdlog_t l; cmdlog_init(&l);
    cmdlog_push(&l, 10, 1, "a", 1, 1);
    char buf[8192];
    size_t n = cmdlog_json(&l, 0, buf, sizeof(buf));
    if (n == 0) { printf("FAIL t_nul_flag\n"); failures++; return; }
    if (strstr(buf, "\"nul\":true") != NULL) printf("PASS t_nul_flag\n");
    else { printf("FAIL t_nul_flag\n"); failures++; }
}

static void t_trunc48(void) {
    cmdlog_t l; cmdlog_init(&l);
    char t48[49];
    memset(t48, 'b', 48); t48[48] = '\0';
    cmdlog_push(&l, 10, 1, t48, 48, 1);
    if (strlen(l.e[0].text) == 47) printf("PASS t_trunc48\n");
    else { printf("FAIL t_trunc48\n"); failures++; }
}

int main(void) {
    t_trunc48();
    t_first_seq();
    t_wrap();
    t_since();
    t_set_result();
    t_trunc();
    t_nonprint();
    t_escape();
    t_null_delivered();
    t_cap();
    t_src_names();
    t_nul_flag();
    return failures;
}
