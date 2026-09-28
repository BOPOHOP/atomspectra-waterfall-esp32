// R7 (sweep-A 1.2.28): признак «калибровка есть» в файловых выводах (main/calib_export.h)
// + сканер исходников: экспорты XML/N42/SPE, n42/ASWF водопада и шапка сегмента обязаны
// решать через calib_export_present(), голый calib_valid в их телах — провал.
#include "test_util.h"
#include "calib_export.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

/* Глобальная структура для тестов, чтобы избежать переполнения стека */
static spectrum_data_t g_sp;

/* -------------------------------------------------------------------------- */
/* Сьют 1: Тесты функции calib_export_present                                  */
/* -------------------------------------------------------------------------- */

void calib_export_suite(void) {
    /* Сброс структуры перед каждым тестом */
    memset(&g_sp, 0, sizeof(g_sp));
    
    /* 1. NULL pointer -> false */
    CHECK(!calib_export_present(NULL));

    /* 2. calib_valid=false, coeffs {0,0,0,0,0} -> false */
    g_sp.calib_valid = false;
    g_sp.calib_order = 2;
    memset(g_sp.calibration, 0, sizeof(g_sp.calibration));
    CHECK(!calib_export_present(&g_sp));

    /* 3. calib_valid=false, coeffs {3.65, 0.41, 4.5e-05, 0, 0} -> false */
    g_sp.calib_valid = false;
    g_sp.calibration[0] = 3.65;
    g_sp.calibration[1] = 0.41;
    g_sp.calibration[2] = 4.5e-05;
    CHECK(!calib_export_present(&g_sp));

    /* 4. calib_valid=true, all zero -> false (R7: нулевой полином == "не установлен") */
    g_sp.calib_valid = true;
    memset(g_sp.calibration, 0, sizeof(g_sp.calibration));
    CHECK(!calib_export_present(&g_sp));

    /* 5. calib_valid=true, реальные коэффициенты -> true */
    g_sp.calibration[0] = 3.65433919686524;
    g_sp.calibration[1] = 0.410925651361521;
    g_sp.calibration[2] = 4.48813599969944e-05;
    g_sp.calibration[3] = -1.27668057645201e-08;
    g_sp.calibration[4] = 1.20234609621895e-12;
    CHECK(calib_export_present(&g_sp));

    /* 6. calib_valid=true, {0, 0.41, 0, 0, 0} -> true (c0 == 0 допустимо) */
    memset(g_sp.calibration, 0, sizeof(g_sp.calibration));
    g_sp.calibration[1] = 0.41;
    CHECK(calib_export_present(&g_sp));

    /* 7. calib_valid=true, {0,0,0,0,1e-300} -> true (только последний коэффициент не нулевой) */
    memset(g_sp.calibration, 0, sizeof(g_sp.calibration));
    g_sp.calibration[4] = 1e-300;
    CHECK(calib_export_present(&g_sp));

    /* 8. calib_valid=true, {NAN, 0.41, 0, 0, 0} -> false */
    memset(g_sp.calibration, 0, sizeof(g_sp.calibration));
    g_sp.calibration[0] = NAN;
    g_sp.calibration[1] = 0.41;
    CHECK(!calib_export_present(&g_sp));

    /* 9. calib_valid=true, {3.65, INFINITY, 0, 0, 0} -> false */
    memset(g_sp.calibration, 0, sizeof(g_sp.calibration));
    g_sp.calibration[0] = 3.65;
    g_sp.calibration[1] = INFINITY;
    CHECK(!calib_export_present(&g_sp));
}

/* -------------------------------------------------------------------------- */
/* Сьют 2: Проверка использования calib_export_present в исходном коде         */
/* -------------------------------------------------------------------------- */

/* Чтение всего файла в буфер */
static char *slurp(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0) { fclose(f); return NULL; }
    char *buf = malloc(len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t nread = fread(buf, 1, len, f);
    buf[nread] = '\0';
    fclose(f);
    return buf;
}

/* Поиск определения функции: имя( ... ) { */
static const char *find_def(const char *src, const char *name) {
    size_t nlen = strlen(name);
    const char *p = src;
    
    while ((p = strstr(p, name)) != NULL) {
        /* Проверка символа перед именем: должен быть пробелом, '*' или началом строки */
        bool valid_prefix = false;
        if (p == src) {
            valid_prefix = true;
        } else {
            char prev = p[-1];
            if (prev == ' ' || prev == '\t' || prev == '\n' || prev == '\r' || prev == '*') {
                valid_prefix = true;
            }
        }

        if (!valid_prefix) {
            p += nlen;
            continue;
        }

        /* Проверка символа после имени: должна быть '(' */
        const char *after_name = p + nlen;
        if (*after_name != '(') {
            p += nlen;
            continue;
        }

        /* Поиск закрывающей ')' для списка параметров */
        int paren_count = 1;
        const char *param_start = after_name + 1;
        const char *curr = param_start;
        
        while (*curr && paren_count > 0) {
            if (*curr == '(') paren_count++;
            else if (*curr == ')') paren_count--;
            curr++;
        }

        if (paren_count != 0) {
            p += nlen;
            continue;
        }

        /* Пропуск пробелов после ')' */
        const char *after_params = curr;
        /* '\r' — CRLF-чекаут на Windows (core.autocrlf) */
        while (*after_params == ' ' || *after_params == '\t' || *after_params == '\n' ||
               *after_params == '\r') {
            after_params++;
        }

        /* Если следующий символ '{', это определение */
        if (*after_params == '{') {
            return after_params;
        }

        /* Иначе это прототип или вызов, ищем дальше */
        p += nlen;
    }
    
    return NULL;
}

/* Вычисление длины тела функции от '{' до соответствующего '}' */
static size_t body_len(const char *brace) {
    if (!brace || *brace != '{') return 0;
    
    int brace_count = 1;
    const char *curr = brace + 1;
    bool in_string = false;
    bool in_char = false;
    
    while (*curr && brace_count > 0) {
        char c = *curr;
        
        if (in_string) {
            if (c == '\\' && *(curr+1)) {
                curr++; /* Пропуск экранированного символа */
            } else if (c == '"') {
                in_string = false;
            }
        } else if (in_char) {
            if (c == '\\' && *(curr+1)) {
                curr++; /* Пропуск экранированного символа */
            } else if (c == '\'') {
                in_char = false;
            }
        } else {
            /* Н12 (раунд 2): комментарии пропускаются целиком — апостроф в
               русском комментарии ("commit'ом") раньше открывал символьный
               литерал, и тело функции обрывалось. */
            if (c == '/' && curr[1] == '/') {
                while (curr[1] && curr[1] != '\n') curr++;
            } else if (c == '/' && curr[1] == '*') {
                curr += 2;
                while (*curr && !(curr[0] == '*' && curr[1] == '/')) curr++;
                if (!*curr) break;   /* незакрытый комментарий — не читать за '\0' */
                curr++;
            } else if (c == '"') {
                in_string = true;
            } else if (c == '\'') {
                in_char = true;
            } else if (c == '{') {
                brace_count++;
            } else if (c == '}') {
                brace_count--;
            }
        }
        
        curr++;
    }
    
    if (brace_count != 0) return 0;
    
    /* Возвращаем длину от начального '{' до закрывающего '}' включительно */
    return (size_t)(curr - brace);
}

/* Поиск подстроки needle в диапазоне [start, start+len) */
static bool body_has(const char *start, size_t len, const char *needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0 || len < nlen) return false;
    
    for (size_t i = 0; i <= len - nlen; i++) {
        bool match = true;
        for (size_t j = 0; j < nlen; j++) {
            if (start[i + j] != needle[j]) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    
    return false;
}

/* Проверка конкретной функции в файле */
static void check_fn(const char *file, const char *fn) {
    char *src = slurp(file);
    CHECK(src != NULL);
    if (!src) {
        printf("calib sites: cannot read %s\n", file);
        return;
    }

    const char *def = find_def(src, fn);
    CHECK(def != NULL);
    if (!def) {
        printf("calib sites: %s not found in %s\n", fn, file);
        free(src);
        return;
    }

    size_t len = body_len(def);
    CHECK(len > 0);

    /* Проверка на прямое использование calib_valid */
    bool raw = body_has(def, len, "calib_valid");
    if (raw) {
        printf("calib sites: raw calib_valid in %s (%s)\n", fn, file);
    }
    CHECK(!raw);

    /* Проверка на использование calib_export_present */
    bool uses = body_has(def, len, "calib_export_present(");
    if (!uses) {
        printf("calib sites: %s does not use calib_export_present (%s)\n", fn, file);
    }
    CHECK(uses);

    free(src);
}

void calib_export_sites_suite(void) {
    /* Проверка функций в web_server.c */
    check_fn("../../main/web_server.c", "render_spectrum_xml");
    check_fn("../../main/web_server.c", "render_spectrum_n42");
    check_fn("../../main/web_server.c", "render_spectrum_spe");

    /* Проверка функций в web_waterfall.c */
    check_fn("../../main/web_waterfall.c", "h_export_aswf");
    check_fn("../../main/web_waterfall.c", "h_export_n42");

    /* Проверка функций в spectrogram.c */
    check_fn("../../main/spectrogram.c", "seg_header_build");
    check_fn("../../main/spectrogram.c", "seg_open_new");

    /* Самодиагностика сканера */
    static const char *T = 
        "static int f(int a);\n"
        "static int g(void) { return f(1); }\n"
        "static int f(int a)\n"
        "{\n"
        "  const char *s = \"}\";\n"
        "  if (a) { return 1; }\n"
        "  return x.calib_valid;\n"
        "}\n";

    const char *def_f = find_def(T, "f");
    CHECK(def_f != NULL);
    
    /* Проверка, что def_f указывает на '{' */
    if (def_f) {
        CHECK(*def_f == '{');
        
        /* Проверка, что перед '{' стоит ')' (с учетом пробелов/переносов строк) */
        const char *prev = def_f - 1;
        while (prev > T && (*prev == ' ' || *prev == '\t' || *prev == '\n')) {
            prev--;
        }
        CHECK(*prev == ')');

        /* Проверка наличия calib_valid в теле */
        size_t len_f = body_len(def_f);
        CHECK(len_f > 0);
        CHECK(body_has(def_f, len_f, "calib_valid"));
    }

    /* Проверка, что функция h не найдена (ее нет) */
    const char *def_h = find_def(T, "h");
    CHECK(def_h == NULL);
}

/* #REC-12 (sweep-A): тем же сканером — баланс пина в h_segment (web_waterfall.c). После
   ветки «пин не взят» КАЖДЫЙ return обязан идти со снятием пина: число "return " в хвосте
   тела == числу вызовов spectrogram_seg_unpin_read(. Забытый unpin = вечный пин. */
static size_t count_in(const char *s, size_t len, const char *needle)
{
    size_t n = 0, k = strlen(needle);
    for (size_t i = 0; i + k <= len; i++) if (memcmp(s + i, needle, k) == 0) n++;
    return n;
}

void seg_pin_sites_suite(void)
{
    char *src = slurp("../../main/web_waterfall.c");
    CHECK(src != NULL);
    const char *def = src ? find_def(src, "h_segment") : NULL;
    size_t len = def ? body_len(def) : 0;
    const char *pin = def ? strstr(def, "if (!spectrogram_seg_pin_read(idx)) {") : NULL;
    CHECK(pin != NULL && pin < def + len);
    if (pin && pin < def + len) {
        const char *blk = strchr(pin, '{');
        const char *tail = blk + body_len(blk);
        size_t rest = (size_t)(def + len - tail);
        size_t nret = count_in(tail, rest, "return "), nun = count_in(tail, rest, "spectrogram_seg_unpin_read(");
        if (nret != nun) printf("pin sites: h_segment return=%zu unpin=%zu\n", nret, nun);
        CHECK(nret > 0 && nret == nun);
    }
    free(src);
}

/* Н12 (раунд 2) — проводка исправлений в прошивочных .c, которые host-сборка не */
/* компилирует: тот же сканер определений; удаление вызова или перестановка порядка */
/* красит тест. */

static const char *w_in(const char *s, size_t len, const char *needle)
{
    size_t nlen = strlen(needle);
    if (nlen == 0 || nlen > len) return NULL;
    for (size_t i = 0; i + nlen <= len; ++i) {
        if (memcmp(s + i, needle, nlen) == 0) return s + i;
    }
    return NULL;
}

static void w_site(const char *file, const char *fn, const char *first, const char *second)
{
    char *src = slurp(file);
    CHECK(src != NULL);
    if (!src) { printf("wiring: no file %s\n", file); return; }

    const char *def = find_def(src, fn);
    CHECK(def != NULL);
    if (!def) { printf("wiring: no def %s in %s\n", fn, file); free(src); return; }

    size_t len = body_len(def);
    const char *p1 = w_in(def, len, first);
    CHECK(p1 != NULL);
    if (!p1) { printf("wiring: %s: missing '%s'\n", fn, first); free(src); return; }

    if (second) {
        const char *p2 = w_in(def, len, second);
        CHECK(p1 && p2 && p1 < p2);
        if (!(p1 && p2 && p1 < p2)) {
            printf("wiring: %s: '%s' must precede '%s'\n", fn, first, second);
        }
    }
    free(src);
}

static void w_absent(const char *file, const char *fn, const char *needle)
{
    char *src = slurp(file);
    CHECK(src != NULL);
    if (!src) { printf("wiring: no file %s\n", file); return; }

    const char *def = find_def(src, fn);
    CHECK(def != NULL);
    if (!def) { printf("wiring: no def %s in %s\n", fn, file); free(src); return; }

    size_t len = body_len(def);
    CHECK(w_in(def, len, needle) == NULL);
    if (w_in(def, len, needle)) {
        printf("wiring: %s: must not contain '%s'\n", fn, needle);
    }
    free(src);
}

void round2_wiring_sites_suite(void)
{
    /* Н1 разбор -cal по отрезку; У6 раунда 3: отрезок/снятие/хвост — text_accum_dispatch */
    /* (поведение — test_text_accum_r3.c), здесь только то, что прошивка идёт через неё */
    w_site("../../main/usb_host_cdc.c", "usb_host_cdc_apply_text_accum_result", "text_accum_dispatch(", "&s_text_marks");
    w_site("../../main/usb_host_cdc.c", "handle_rx_packet", "text_accum_feed_m(", "usb_host_cdc_apply_text_accum_result(tar)");
    w_site("../../main/usb_host_cdc.c", "usb_rx_worker", "TEXT_ACCUM_QUIET_MS", "text_accum_flush(");
    /* Н3 снимок диапазонов до очистки */
    w_site("../../main/spectrogram.c", "seg_rebuild_counters_from_disk", "rb_prev[n_prev].g0", "reg_clear_all()");
    /* Н3 восстановление; У6 раунда 3: разметка — wf_seg_rebuild_entry (test_round3_misc.c) */
    w_site("../../main/spectrogram.c", "seg_rebuild_counters_from_disk", "wf_seg_rebuild_entry(", "rb_prev, n_prev");
    /* Н4 переоткрытие до решения */
    w_site("../../main/ota_github_client.c", "ota_gh_download_retry", "ota_gh_dl_is_already_complete(", "ota_gh_dl_reopen_until_decided(");
    /* Н4 без прямого decide */
    w_absent("../../main/ota_github_client.c", "ota_gh_download_retry", "ota_gh_dl_decide(");
    /* Н4 код отказа */
    w_site("../../main/ota_github_client.c", "ota_gh_dl_reopen_cb", "http_open_with_redirects_st(", "ota_gh_dl_reopen_status(");
    /* Н4 код отказа наружу */
    w_site("../../main/ota_github_client.c", "http_open_with_redirects_st", "*out_fail_status = status", NULL);
    /* М4 сц.2 выход по EOF */
    w_site("../../main/ota_github_client.c", "install_task", "if (done) break;", NULL);
    /* Н7 тихое окно до лока; У3/У4 раунда 3: окно до и после лока, запись tmp + rename */
    w_site("../../main/web_server.c", "settings_snapshot_write_file", "flash_quiet_writer_lock_in_window(", "snapshot_file_write_atomic(");
    w_site("../../main/flash_quiet.c", "flash_quiet_writer_lock_in_window", "flash_quiet_lock_in_window(&ops", NULL);
    w_site("../../main/web_server.c", "handle_settings_snapshot_get", "ferror(f)", "httpd_resp_send_chunk(req, NULL, 0)");
    /* М7 заголовок */
    w_site("../../main/web_server.c", "handle_settings_snapshot", "X-Dsp-Snapshot-Saved", NULL);
    /* М5 поколение кэша */
    w_site("../../main/spectrum_http_cache.c", "spectrum_http_cache_finish_build", "spec_cache_gen_stale(", NULL);
    /* М6 гейт OTA в автосохранении */
    w_site("../../main/main.c", "app_main", "if (ota_busy_is_busy())", "spectrum_autosave_fail_streak()");
}
