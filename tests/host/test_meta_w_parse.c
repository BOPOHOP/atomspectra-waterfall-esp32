// З-5 (release-gate-1.2.36-code-pass1) + З-1 (pass2): разбор ?w= запроса meta.json (main/meta_w_parse.h). Образцы - запросы главной (index.html: w=win*iv, кэш-бастер _=<ms>);
// meta_w_cut - строка обрезана буфером (trunc=1): 414 только если не уместился сам w.
#include "meta_w_parse.h"
#include "test_util.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define NCHECK(name, cond) do { if (!(cond)) { printf("FAIL %s %s:%d: %s\n", name, __FILE__, __LINE__, #cond); g_failures++; } } while (0)

static int g_n;

static void ok_case_t(const char *name, const char *q, int trunc, uint32_t want) {
    uint32_t w = 0xDEADBEEFu;
    meta_w_t r = meta_w_parse(q, trunc, &w);
    NCHECK(name, r == META_W_OK);
    NCHECK(name, w == want);
    g_n++;
}

static void res_case_t(const char *name, const char *q, int trunc, meta_w_t want) {
    uint32_t w = 0xDEADBEEFu;
    meta_w_t r = meta_w_parse(q, trunc, &w);
    NCHECK(name, r == want);
    NCHECK(name, w == 0xDEADBEEFu);
    g_n++;
}

static void ok_case(const char *name, const char *q, uint32_t want) { ok_case_t(name, q, 0, want); }
static void res_case(const char *name, const char *q, meta_w_t want) { res_case_t(name, q, 0, want); }

static void test_ok(void) {
    ok_case("meta_w_ok", "w=100", 100);
    ok_case("meta_w_ok", "w=2000", 2000);
    ok_case("meta_w_ok", "w=1", 1);
    ok_case("meta_w_ok", "w=0", 0);
    ok_case("meta_w_ok", "w=100&_=1760140800123", 100);
    ok_case("meta_w_ok", "_=1760140800123&w=300", 300);
    ok_case("meta_w_ok", "a=1&w=43200&b=2", 43200);
    ok_case("meta_w_ok", "w=17280000", 17280000);
}

static void test_none(void) {
    res_case("meta_w_none", "", META_W_NONE);
    res_case("meta_w_none", "_=1760140800123", META_W_NONE);
    res_case("meta_w_none", "W=5", META_W_NONE);
    res_case("meta_w_none", "win=5", META_W_NONE);
}

static void test_key(void) {
    res_case("meta_w_key", "xw=9", META_W_NONE);
    res_case("meta_w_key", "ww=5", META_W_NONE);
    ok_case("meta_w_key", "aw=5&w=7", 7);
    ok_case("meta_w_key", "_=1760140800123&sw=1&w=60", 60);
}

static void test_bad(void) {
    const char *bad[] = {"w=", "w=&_=1760140800123", "w=-5", "w=+5", "w= 5", "w=5 ", "w=1e3", "w=12.5", "w=abc", "w=5x&_=1",
                         "w=1e+21&_=1760140800123", "w=1e+21", "_=1760140800123&utm_source=telegram&w="};   /* JS w*iv при огромном интервале (monitor.html, parseInt) */
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        res_case("meta_w_bad", bad[i], META_W_BAD);
    }
}

static void test_sat(void) {
    ok_case("meta_w_sat", "w=4294967295", 4294967295u);
    ok_case("meta_w_sat", "w=4294967296", 4294967295u);
    ok_case("meta_w_sat", "w=99999999999999999999999", 4294967295u);
    ok_case("meta_w_sat", "w=18446744073709551617", 4294967295u);
}

static void test_cut(void) {
    /* обрезанная строка (trunc=1): значение w доходит до места обрезки или обрезка прошла по ключу - CUT (414) */
    res_case_t("meta_w_cut", "_=1760140800123&utm_source=telegram&fbclid=IwAR2xYz&w=1728", 1, META_W_CUT);
    res_case_t("meta_w_cut", "_=1760140800123&utm_source=telegram&w=", 1, META_W_CUT);
    res_case_t("meta_w_cut", "_=1&w=600", 1, META_W_CUT);        /* Z-2: digits reach the cut - the value may go on */
    ok_case_t("meta_w_cut", "_=1&w=600&x=1", 1, 600);            /* Z-2: '&' ends the digits inside the buffer - fits */
    /* w уместился целиком (за ним '&') - обычный разбор; w в уместившейся части нет - NONE (как 3c73979) */
    ok_case_t("meta_w_cut", "w=600&_=1760140800123&utm_source=telegram&fbclid=IwAR2xYz", 1, 600);
    ok_case_t("meta_w_cut", "_=1760140800123&w=100&fbclid=IwAR2xYz", 1, 100);
    res_case_t("meta_w_cut", "_=1760140800123&utm_source=telegram&fbclid=IwAR2xYz", 1, META_W_NONE);
    res_case_t("meta_w_cut", "_=1760140800123&utm_source=telegram&aw", 1, META_W_NONE);
    /* gate pass3 S-1: the cut after "&w" - the key is not seen (may be "&width=", "&wait=", "&wq"): NONE, meta without SMA as in 3c73979 */
    res_case_t("meta_w_cut_key", "_=1760140800123&utm_source=telegram&w", 1, META_W_NONE);
    res_case_t("meta_w_cut_key", "w", 1, META_W_NONE);
    res_case_t("meta_w_cut_key", "_=1&wq", 1, META_W_NONE);
    res_case_t("meta_w_cut_key", "_=1&wait=1&x", 1, META_W_NONE);
    res_case_t("meta_w_cut_key", "_=1&width=1", 1, META_W_NONE);
    /* не целое уже в уместившейся части - BAD (meta без SMA), обрезка не важна */
    res_case_t("meta_w_cut", "_=1760140800123&w=1e+2", 1, META_W_BAD);
    /* те же строки без обрезки */
    ok_case_t("meta_w_cut", "_=1760140800123&utm_source=telegram&fbclid=IwAR2xYz&w=1728", 0, 1728);
    res_case_t("meta_w_cut", "_=1760140800123&utm_source=telegram&w", 0, META_W_NONE);
}

static void test_len(void) {
    const char *q = "w=17280000&_=1760140800123&tab=sma&lang=ru";
    NCHECK("meta_w_len", strlen(q) < (size_t)META_Q_MAX);
    NCHECK("meta_w_len", META_Q_MAX >= 64);
    g_n++;
}

void meta_w_parse_suite(void) {
    g_n = 0;
    test_ok();
    test_none();
    test_key();
    test_bad();
    test_sat();
    test_cut();
    test_len();
    printf("INFO meta_w_parse: samples=%d\n", g_n);
}
