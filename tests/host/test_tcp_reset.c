/* Хост-тест tcp_text_frame_is_reset. Новая версия: -I<src>/main; старая: -Itcp_reset_old -I<src>/main (подмена tcp_reset_plan.h) */
#include <stdio.h>
#include <string.h>
#include "tcp_reset_plan.h"
static const struct { const char *name; const char *f; size_t len; bool want; } T[] = {
    {"rst_with_nul", "-rst\0", 5, true},  {"rst_no_nul", "-rst", 4, true},   {"rsx_nul", "-rsx\0", 5, false},
    {"sta_no_r", "-sta\0", 5, false},     {"empty", "", 0, false},           {"sta_r_no_nul", "-sta -r", 7, true},
    {"rsx_no_nul", "-rsx", 4, false},
};
int main(void)
{
    int fails = 0;
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        uint8_t buf[512]; memset(buf, 0x55, sizeof buf); memcpy(buf, T[i].f, T[i].len);   /* 0x55 вокруг: нет скрытого \0 */
        bool got = tcp_text_frame_is_reset(buf, T[i].len, sizeof buf);
        printf("%-14s want=%d got=%d %s\n", T[i].name, T[i].want, got, got == T[i].want ? "ok" : "FAIL");
        fails += got != T[i].want;
    }
    printf(fails ? "FAILED: %d\n" : "ALL PASSED\n", fails);
    return fails ? 1 : 0;
}
