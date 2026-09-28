#define _POSIX_C_SOURCE 200809L
/* Раунд 3: У3 (лок писателя в тихом окне), У4 (слепок DSP через временный файл), У5 (докачка */
/* GitHub-OTA от исходного адреса ассета), У6/О5 (запись реестра при пересборке) — поведение, не порядок подстрок. */

#include "test_util.h"
#include "flash_quiet_lock_plan.h"
#include "snapshot_file.h"
#include "ota_github_download_retry.h"
#include "wf_seg_rebuild_range.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>

/* --- У3: Тихое окно и захват лока --- */

typedef struct {
    int t;
    bool usb;
    bool locked;
    int lock_delay[4];
    int lock_fail;
    int nlock, nunlock, npoll;
    int window_mode;
} r3l_t;

static bool r3l_usb_live(void *ctx) {
    r3l_t *x = (r3l_t *)ctx;
    return x->usb;
}

static bool r3l_can_start(void *ctx) {
    r3l_t *x = (r3l_t *)ctx;
    if (x->window_mode == 1) return false;
    return (x->t % 20) < 2;
}

static bool r3l_lock(void *ctx) {
    r3l_t *x = (r3l_t *)ctx;
    if (x->lock_fail) return false;
    int d = x->nlock < 4 ? x->lock_delay[x->nlock] : 0;
    x->t += d;
    x->nlock++;
    x->locked = true;
    return true;
}

static void r3l_unlock(void *ctx) {
    r3l_t *x = (r3l_t *)ctx;
    x->locked = false;
    x->nunlock++;
}

static void r3l_sleep_poll(void *ctx) {
    r3l_t *x = (r3l_t *)ctx;
    x->t += 1;
    x->npoll++;
}

static const flash_quiet_lock_ops_t R3L_OPS = {
    .usb_live = r3l_usb_live,
    .can_start = r3l_can_start,
    .lock = r3l_lock,
    .unlock = r3l_unlock,
    .sleep_poll = r3l_sleep_poll
};

static bool r3l_can(r3l_t *x) {
    if (x->window_mode == 1) return false;
    return (x->t % 20) < 2;
}

void round3_flash_lock_suite(void) {
    /* Случай 1: Окно открыто, но лок держит другой писатель */
    {
        r3l_t x;
        memset(&x, 0, sizeof x);
        x.lock_delay[0] = 5;
        x.usb = true;
        x.t = 0;
        bool ok = flash_quiet_lock_in_window(&R3L_OPS, &x, 25);
        CHECK(ok);
        CHECK(x.locked);
        CHECK(r3l_can(&x));
        CHECK(x.nunlock == 1);
        CHECK(x.t == 20);
    }

    /* Случай 2: Прибор не на USB */
    {
        r3l_t x;
        memset(&x, 0, sizeof x);
        x.usb = false;
        x.t = 5;
        bool ok = flash_quiet_lock_in_window(&R3L_OPS, &x, 25);
        CHECK(ok);
        CHECK(x.npoll == 0);
        CHECK(x.nlock == 1);
        CHECK(x.locked);
    }

    /* Случай 3: Окно не открывается */
    {
        r3l_t x;
        memset(&x, 0, sizeof x);
        x.window_mode = 1;
        x.usb = true;
        bool ok = flash_quiet_lock_in_window(&R3L_OPS, &x, 25);
        CHECK(ok);
        CHECK(x.npoll == 25);
        CHECK(x.nlock == 1);
        CHECK(x.nunlock == 0);
    }

    /* Случай 4: Захват лока не удаётся */
    {
        r3l_t x;
        memset(&x, 0, sizeof x);
        x.lock_fail = 1;
        x.usb = true;
        x.t = 0;
        bool ok = flash_quiet_lock_in_window(&R3L_OPS, &x, 25);
        CHECK(!ok);
        CHECK(!x.locked);
    }

    /* Случай 5: Лок каждый раз отдают за окном */
    {
        r3l_t x;
        memset(&x, 0, sizeof x);
        x.lock_delay[0] = 5; x.lock_delay[1] = 5; x.lock_delay[2] = 5; x.lock_delay[3] = 5;
        x.usb = true;
        x.t = 0;
        bool ok = flash_quiet_lock_in_window(&R3L_OPS, &x, 100);
        CHECK(ok);
        CHECK(x.locked);
        CHECK(x.nunlock == 3);
        CHECK(x.nlock == 4);
    }
}

/* --- У4: Слепок DSP через временный файл --- */

static void r3s_put(const char *p, const char *s) {
    FILE *f = fopen(p, "w");
    if (f) { fprintf(f, "%s", s); fclose(f); }
}

static int r3s_get(const char *p, char *out, int cap) {
    FILE *f = fopen(p, "r");
    if (!f) return -1;
    size_t n = fread(out, 1, cap - 1, f);
    out[n] = '\0';
    fclose(f);
    return (int)n;
}

static bool r3s_exists(const char *p) {
    struct stat st;
    return lstat(p, &st) == 0;
}

void round3_snapshot_file_suite(void) {
    char dir[] = "/tmp/r3snapXXXXXX";
    if (!mkdtemp(dir)) { CHECK(0); return; }
    char fin[128], tmp[128], sub[160];
    snprintf(fin, sizeof fin, "%s/dsp_snapshot.txt", dir);
    snprintf(tmp, sizeof tmp, "%s/dsp_snapshot.tmp", dir);

    /* Случай 1: Успех */
    {
        r3s_put(fin, "OLD");
        bool ok = snapshot_file_write_atomic(tmp, fin, "S1", "INFO", "TC");
        CHECK(ok);
        char buf[256];
        int len = r3s_get(fin, buf, sizeof buf);
        const char *expected = "# AtomSpectra DSP snapshot S1\r\nINFO\r\nTC\r\n";
        CHECK(len == (int)strlen(expected));
        CHECK(memcmp(buf, expected, strlen(expected)) == 0);
        CHECK(!r3s_exists(tmp));
    }

    /* Случай 2: Сбой записи посреди файла */
    {
        if (r3s_exists("/dev/full")) {
            r3s_put(fin, "OLD");
            CHECK(symlink("/dev/full", tmp) == 0);
            bool ok = snapshot_file_write_atomic(tmp, fin, "S1", "INFO", "TC");
            CHECK(!ok);
            char buf[256];
            int len = r3s_get(fin, buf, sizeof buf);
            CHECK(len == 3); /* "OLD" */
            CHECK(memcmp(buf, "OLD", 3) == 0);
            CHECK(!r3s_exists(tmp));
            CHECK(r3s_exists("/dev/full"));
        } else {
            printf("snapshot: /dev/full нет, случай пропущен\n");
        }
    }

    /* Случай 3: Временный файл не создаётся */
    {
        snprintf(sub, sizeof sub, "%s/nodir/x.tmp", dir);
        r3s_put(fin, "OLD");
        bool ok = snapshot_file_write_atomic(sub, fin, "S1", "INFO", "TC");
        CHECK(!ok);
        char buf[256];
        int len = r3s_get(fin, buf, sizeof buf);
        CHECK(len == 3);
        CHECK(memcmp(buf, "OLD", 3) == 0);
    }

    /* Случай 4: rename не удаётся */
    {
        char fdir[160];
        snprintf(fdir, sizeof fdir, "%s/asdir", dir);
        CHECK(mkdir(fdir, 0700) == 0);
        bool ok = snapshot_file_write_atomic(tmp, fdir, "S1", "INFO", "TC");
        CHECK(!ok);
        CHECK(!r3s_exists(tmp));
        rmdir(fdir);
    }

    remove(fin);
    remove(tmp);
    rmdir(dir);
}

/* --- У5: Докачка GitHub-OTA --- */

#define R3O_ASSET  "https://github.com/o/r/releases/download/v1/fw.bin"
#define R3O_SIGNED "https://objects.example/fw.bin?sig=1"

typedef struct {
    char url[160];
    uint32_t range;
    int now;
    int signed_at;
    int last_status;
    int set_url_fail;
    int net_down;
} r3o_t;

static int r3o_set_url(void *cl, const char *url) {
    r3o_t *c = (r3o_t *)cl;
    if (c->set_url_fail) return -1;
    snprintf(c->url, sizeof c->url, "%s", url);
    return 0;
}

static void r3o_set_range(void *cl, uint32_t from) {
    r3o_t *c = (r3o_t *)cl;
    c->range = from;
}

static int r3o_open(void *cl, int64_t *clen, int *fail_status) {
    r3o_t *c = (r3o_t *)cl;
    if (c->net_down) return -1;
    if (strcmp(c->url, R3O_ASSET) == 0) {
        c->signed_at = c->now;
        snprintf(c->url, sizeof c->url, "%s", R3O_SIGNED);
    }
    if (strcmp(c->url, R3O_SIGNED) != 0 || c->signed_at < 0 || c->now - c->signed_at > 10) {
        *fail_status = 403;
        return -1;
    }
    c->last_status = c->range ? 206 : 200;
    *clen = 1000 - (int64_t)c->range;
    return 0;
}

static int r3o_status(void *cl) {
    r3o_t *c = (r3o_t *)cl;
    return c->last_status;
}

static const ota_gh_dl_io_t R3O_IO = {
    .set_url = r3o_set_url,
    .set_range = r3o_set_range,
    .open = r3o_open,
    .status = r3o_status
};

void round3_ota_reopen_suite(void) {
    /* Случай 1: Первое открытие */
    {
        r3o_t c;
        memset(&c, 0, sizeof c);
        c.signed_at = -1;
        snprintf(c.url, sizeof c.url, "%s", R3O_ASSET);
        int64_t clen = 0;
        CHECK(ota_gh_dl_open_from(&R3O_IO, &c, R3O_ASSET, 0, &clen) == 200);
        CHECK(clen == 1000);
        CHECK(strcmp(c.url, R3O_SIGNED) == 0);

        /* Случай 2 (тот же клиент): обрыв и ожидание дольше срока подписи */
        c.now = 100;
        CHECK(ota_gh_dl_open_from(&R3O_IO, &c, R3O_ASSET, 500, &clen) == 206);
        CHECK(clen == 500);
        CHECK(c.range == 500);
        CHECK(c.signed_at == 100);   /* подпись получена заново */

        /* Случай 3 (тот же клиент): рестарт с нуля после ожидания */
        c.now = 200;
        CHECK(ota_gh_dl_open_from(&R3O_IO, &c, R3O_ASSET, 0, &clen) == 200);
        CHECK(c.range == 0);
    }

    /* Случай 4: Сеть недоступна */
    {
        r3o_t c;
        memset(&c, 0, sizeof c);
        c.signed_at = -1;
        snprintf(c.url, sizeof c.url, "%s", R3O_ASSET);
        int64_t clen = 0;
        c.net_down = 1;
        CHECK(ota_gh_dl_open_from(&R3O_IO, &c, R3O_ASSET, 500, &clen) == -1);
        CHECK(ota_gh_dl_decide(-1, 1, 5) == OTA_GH_DL_REOPEN_LATER);
        c.net_down = 0;
    }

    /* Случай 5: Адрес не принят клиентом */
    {
        r3o_t c;
        memset(&c, 0, sizeof c);
        c.signed_at = -1;
        snprintf(c.url, sizeof c.url, "%s", R3O_ASSET);
        int64_t clen = 0;
        c.set_url_fail = 1;
        CHECK(ota_gh_dl_open_from(&R3O_IO, &c, R3O_ASSET, 500, &clen) == -1);
        c.set_url_fail = 0;
    }

    /* Случай 6: Ответ сервера 403 */
    {
        r3o_t c;
        memset(&c, 0, sizeof c);
        c.now = 300;
        snprintf(c.url, sizeof c.url, "%s", R3O_SIGNED);
        c.signed_at = 0;
        int fs = 0;
        int64_t cl2 = 0;
        CHECK(R3O_IO.open(&c, &cl2, &fs) == -1);
        CHECK(fs == 403);
        CHECK(ota_gh_dl_decide(403, 1, 5) == OTA_GH_DL_GIVE_UP);
    }
}

/* --- У6/О5: Запись реестра при пересборке --- */

static const wf_seg_range_snap_t prev[] = { {7, 64, 100}, {3, 0, 0}, {9, 10, 10} };
static const uint32_t defb = 4608;

void round3_seg_rebuild_entry_suite(void) {
    /* Случай 1: Сегмент текущей сессии */
    {
        wf_seg_reg_t e;
        memset(&e, 0xAB, sizeof e);
        wf_seg_rebuild_entry(&e, 7, 11, 1234, 50, 9000, defb, prev, 3);
        CHECK(e.idx == 7);
        CHECK(e.seg_seq == 11);
        CHECK(e.started_at == 1234);
        CHECK(e.rows == 50);
        CHECK(e.bytes == 9000);
        CHECK(e.g0 == 64);
        CHECK(e.g1 == 114);
        CHECK(e.finalized);
        CHECK(e.valid);
    }

    /* Случай 2: Сегмент прежней сессии */
    {
        wf_seg_reg_t e;
        memset(&e, 0xAB, sizeof e);
        wf_seg_rebuild_entry(&e, 3, 2, 55, 20, 7000, defb, prev, 3);
        CHECK(e.g0 == 0);
        CHECK(e.g1 == 0);
        CHECK(e.bytes == 7000);
        CHECK(e.rows == 20);
        CHECK(e.finalized);
        CHECK(e.valid);
    }

    /* Случай 3: Сегмента нет в снимке */
    {
        wf_seg_reg_t e;
        memset(&e, 0xAB, sizeof e);
        wf_seg_rebuild_entry(&e, 42, 5, 0, 8, 0, defb, prev, 3);
        CHECK(e.g0 == 0);
        CHECK(e.g1 == 0);
        CHECK(e.bytes == defb);
        CHECK(e.rows == 8);
    }

    /* Случай 4: Пустой диапазон в снимке */
    {
        wf_seg_reg_t e;
        memset(&e, 0xAB, sizeof e);
        wf_seg_rebuild_entry(&e, 9, 1, 0, 5, 100, defb, prev, 3);
        CHECK(e.g0 == 0);
        CHECK(e.g1 == 0);
    }

    /* Случай 5: Снимок пуст */
    {
        wf_seg_reg_t e;
        memset(&e, 0xAB, sizeof e);
        wf_seg_rebuild_entry(&e, 7, 1, 0, 50, 9000, defb, prev, 0);
        CHECK(e.g0 == 0);
        CHECK(e.g1 == 0);
    }
}
