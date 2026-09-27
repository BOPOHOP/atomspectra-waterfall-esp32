#include "ota_mark_valid_plan.h"
#include "test_util.h"

void ota_mark_valid_plan_suite(void)
{
    // D3 живой сценарий: Field AP без клиентов, никогда wifi_connected,
    // usb_connected пришёл ПОЗЖЕ (или никогда): до 30с httpd-only -- НЕ годен.
    CHECK(!ota_mark_valid_should_fire(false, false, true, 0, 30));
    CHECK(!ota_mark_valid_should_fire(false, false, true, 29, 30));
    // ИСХОДНЫЙ БАГ: старое правило (wifi-only) никогда не сработало бы тут.
    CHECK(ota_mark_valid_should_fire(false, false, true, 30, 30));
    CHECK(ota_mark_valid_should_fire(false, false, true, 1000, 30));

    // USB-прибор подключён -- годен независимо от Wi-Fi и времени.
    CHECK(ota_mark_valid_should_fire(false, true, true, 0, 30));

    // Wi-Fi реально подключен -- годен сразу (прежнее поведение сохранено).
    CHECK(ota_mark_valid_should_fire(true, false, true, 0, 30));

    // httpd не поднят и ничего другого -- НЕ годен даже после N секунд
    // (защита от вырожденного вызова с httpd_up=false).
    CHECK(!ota_mark_valid_should_fire(false, false, false, 1000, 30));
}
