// LK-07 (1.2.30): решение обработчика /api/ota/github/check (main/ota_gh_check_state.h).
#include "ota_gh_check_state.h"
#include "test_util.h"

void test_ota_gh_check_state(void)
{
    CHECK(ota_chk_decide(OTA_CHK_IDLE, 0, 10000) == OTA_CHK_ACT_START);       // ничего не шло — запустить
    CHECK(ota_chk_decide(OTA_CHK_RUNNING, 0, 10000) == OTA_CHK_ACT_WAIT);     // идёт — «checking», не запускать вторую
    CHECK(ota_chk_decide(OTA_CHK_RUNNING, 99999, 10000) == OTA_CHK_ACT_WAIT); // возраст для RUNNING не важен
    CHECK(ota_chk_decide(OTA_CHK_DONE, 0, 10000) == OTA_CHK_ACT_SERVE);       // готово только что — отдать
    CHECK(ota_chk_decide(OTA_CHK_DONE, 9999, 10000) == OTA_CHK_ACT_SERVE);    // ещё свежее
    CHECK(ota_chk_decide(OTA_CHK_DONE, 10000, 10000) == OTA_CHK_ACT_START);   // протухло — новая проверка
    CHECK(ota_chk_decide(OTA_CHK_DONE, 50000, 10000) == OTA_CHK_ACT_START);
}
