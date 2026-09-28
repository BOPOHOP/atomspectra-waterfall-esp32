#include "ota_github_download_retry.h"
#include "test_util.h"

#define DL_MAX 5

static void dl_206_resumes(void)
{
    CHECK(ota_gh_dl_decide(206, 1, DL_MAX) == OTA_GH_DL_RESUME_RANGE);
    CHECK(ota_gh_dl_decide(206, DL_MAX, DL_MAX) == OTA_GH_DL_RESUME_RANGE);
}

static void dl_200_restarts(void)
{
    CHECK(ota_gh_dl_decide(200, 1, DL_MAX) == OTA_GH_DL_RESTART_ZERO);
}

static void dl_attempt_exceeds_max_gives_up(void)
{
    CHECK(ota_gh_dl_decide(206, DL_MAX + 1, DL_MAX) == OTA_GH_DL_GIVE_UP);
    CHECK(ota_gh_dl_decide(200, DL_MAX + 1, DL_MAX) == OTA_GH_DL_GIVE_UP);
}

static void dl_error_or_unexpected_status_gives_up(void)
{
    // -1 теперь означает REOPEN_LATER, а не RESUME_RANGE.
    CHECK(ota_gh_dl_decide(-1, 1, DL_MAX) == OTA_GH_DL_REOPEN_LATER);
    CHECK(ota_gh_dl_decide(-1, DL_MAX, DL_MAX) == OTA_GH_DL_REOPEN_LATER);
    CHECK(ota_gh_dl_decide(-1, DL_MAX + 1, DL_MAX) == OTA_GH_DL_GIVE_UP);
    CHECK(ota_gh_dl_decide(-1, 1, DL_MAX) != OTA_GH_DL_RESUME_RANGE);

    // Ошибки сервера — немедленный отказ.
    CHECK(ota_gh_dl_decide(403, 1, DL_MAX) == OTA_GH_DL_GIVE_UP);
    CHECK(ota_gh_dl_decide(404, 1, DL_MAX) == OTA_GH_DL_GIVE_UP);
    CHECK(ota_gh_dl_decide(416, 1, DL_MAX) == OTA_GH_DL_GIVE_UP);
    CHECK(ota_gh_dl_decide(500, 1, DL_MAX) == OTA_GH_DL_GIVE_UP);
}

// М4 сценарий 2: обрыв ровно на границе конца образа — не отказ, а завершение.
static void dl_already_complete_at_eof(void)
{
    CHECK(ota_gh_dl_is_already_complete(1000, 1000) == true);   // всё принято
    CHECK(ota_gh_dl_is_already_complete(999, 1000) == false);   // не хватает 1 байта
    CHECK(ota_gh_dl_is_already_complete(1000, 0) == false);     // clen неизвестна — не наш случай
    CHECK(ota_gh_dl_is_already_complete(0, -1) == false);
}

static void dl_reopen_status_maps_http_codes(void)
{
    CHECK(ota_gh_dl_reopen_status(true, 206, 0) == 206);
    CHECK(ota_gh_dl_reopen_status(true, 200, 0) == 200);
    CHECK(ota_gh_dl_reopen_status(false, 0, 403) == 403);
    CHECK(ota_gh_dl_reopen_status(false, 0, 500) == 500);
    CHECK(ota_gh_dl_reopen_status(false, 0, 0) == -1);
    CHECK(ota_gh_dl_reopen_status(false, 0, -1) == -1);
}

static void dl_backoff(void)
{
    CHECK(ota_gh_dl_backoff_ms(0) == 2000);
    CHECK(ota_gh_dl_backoff_ms(1) == 2000);
    CHECK(ota_gh_dl_backoff_ms(2) == 4000);
    CHECK(ota_gh_dl_backoff_ms(4) == 16000);
    CHECK(ota_gh_dl_backoff_ms(5) == 30000);
    CHECK(ota_gh_dl_backoff_ms(100) == 30000);
}

typedef struct {
    const int *seq;
    int n;
    int calls;
    int waits;
} dl_fake_t;

static int dl_fake_reopen(void *c)
{
    dl_fake_t *f = (dl_fake_t *)c;
    if (f->calls >= f->n) return -1;
    int res = f->seq[f->calls];
    f->calls++;
    return res;
}

static void dl_fake_wait(void *c, int attempt)
{
    dl_fake_t *f = (dl_fake_t *)c;
    f->waits++;
    (void)attempt;
}

static void dl_reopen_loop(void)
{
    // Случай 1: {-1, -1, 206}, att=0 -> RESUME_RANGE, att==3, calls==3, waits==2.
    {
        int seq[] = {-1, -1, 206};
        dl_fake_t f = {seq, 3, 0, 0};
        int att = 0;
        ota_gh_dl_retry_action_t act = ota_gh_dl_reopen_until_decided(
            dl_fake_reopen, dl_fake_wait, &f, &att, DL_MAX);
        CHECK(act == OTA_GH_DL_RESUME_RANGE);
        CHECK(att == 3);
        CHECK(f.calls == 3);
        CHECK(f.waits == 2);
        CHECK(act != OTA_GH_DL_REOPEN_LATER);
    }

    // Случай 2: {403}, att=0 -> GIVE_UP, calls==1, waits==0.
    {
        int seq[] = {403};
        dl_fake_t f = {seq, 1, 0, 0};
        int att = 0;
        ota_gh_dl_retry_action_t act = ota_gh_dl_reopen_until_decided(
            dl_fake_reopen, dl_fake_wait, &f, &att, DL_MAX);
        CHECK(act == OTA_GH_DL_GIVE_UP);
        CHECK(f.calls == 1);
        CHECK(f.waits == 0);
        CHECK(act != OTA_GH_DL_REOPEN_LATER);
    }

    // Случай 3: {-1, 404}, att=0 -> GIVE_UP, calls==2, waits==1.
    {
        int seq[] = {-1, 404};
        dl_fake_t f = {seq, 2, 0, 0};
        int att = 0;
        ota_gh_dl_retry_action_t act = ota_gh_dl_reopen_until_decided(
            dl_fake_reopen, dl_fake_wait, &f, &att, DL_MAX);
        CHECK(act == OTA_GH_DL_GIVE_UP);
        CHECK(f.calls == 2);
        CHECK(f.waits == 1);
        CHECK(act != OTA_GH_DL_REOPEN_LATER);
    }

    // Случай 4: {200}, att=0 -> RESTART_ZERO, calls==1.
    {
        int seq[] = {200};
        dl_fake_t f = {seq, 1, 0, 0};
        int att = 0;
        ota_gh_dl_retry_action_t act = ota_gh_dl_reopen_until_decided(
            dl_fake_reopen, dl_fake_wait, &f, &att, DL_MAX);
        CHECK(act == OTA_GH_DL_RESTART_ZERO);
        CHECK(f.calls == 1);
        CHECK(act != OTA_GH_DL_REOPEN_LATER);
    }

    // Случай 5: {-1,-1,-1,-1,-1,-1,-1}, att=0, max DL_MAX -> GIVE_UP, calls==DL_MAX, waits==DL_MAX-1.
    {
        int seq[] = {-1, -1, -1, -1, -1, -1, -1};
        dl_fake_t f = {seq, 7, 0, 0};
        int att = 0;
        ota_gh_dl_retry_action_t act = ota_gh_dl_reopen_until_decided(
            dl_fake_reopen, dl_fake_wait, &f, &att, DL_MAX);
        CHECK(act == OTA_GH_DL_GIVE_UP);
        CHECK(f.calls == DL_MAX);
        CHECK(f.waits == DL_MAX - 1);
        CHECK(act != OTA_GH_DL_REOPEN_LATER);
    }

    // Случай 6: {-1}, att=DL_MAX-1 -> GIVE_UP, calls==1, waits==0.
    {
        int seq[] = {-1};
        dl_fake_t f = {seq, 1, 0, 0};
        int att = DL_MAX - 1;
        ota_gh_dl_retry_action_t act = ota_gh_dl_reopen_until_decided(
            dl_fake_reopen, dl_fake_wait, &f, &att, DL_MAX);
        CHECK(act == OTA_GH_DL_GIVE_UP);
        CHECK(f.calls == 1);
        CHECK(f.waits == 0);
        CHECK(act != OTA_GH_DL_REOPEN_LATER);
    }

    // Случай 7: {206}, att=DL_MAX -> GIVE_UP, calls==0.
    {
        int seq[] = {206};
        dl_fake_t f = {seq, 1, 0, 0};
        int att = DL_MAX;
        ota_gh_dl_retry_action_t act = ota_gh_dl_reopen_until_decided(
            dl_fake_reopen, dl_fake_wait, &f, &att, DL_MAX);
        CHECK(act == OTA_GH_DL_GIVE_UP);
        CHECK(f.calls == 0);
        CHECK(act != OTA_GH_DL_REOPEN_LATER);
    }
}

void ota_gh_dl_retry_suite(void)
{
    dl_206_resumes();
    dl_200_restarts();
    dl_attempt_exceeds_max_gives_up();
    dl_error_or_unexpected_status_gives_up();
    dl_already_complete_at_eof();
    dl_reopen_status_maps_http_codes();
    dl_backoff();
    dl_reopen_loop();
}
