// AWF-2a (#1): расписание пауз реконнекта (main/wifi_reconnect_plan.h).
#include "wifi_reconnect_plan.h"
#include "test_util.h"

void test_wifi_reconnect_plan(void)
{
    CHECK(wifi_reconnect_delay_s(0) == 1);
    CHECK(wifi_reconnect_delay_s(1) == 2);
    CHECK(wifi_reconnect_delay_s(2) == 5);
    CHECK(wifi_reconnect_delay_s(3) == 10);
    CHECK(wifi_reconnect_delay_s(4) == 30);
    CHECK(wifi_reconnect_delay_s(5) == 60);
    CHECK(wifi_reconnect_delay_s(6) == 60);   // за пределами расписания — повтор последнего шага
    CHECK(wifi_reconnect_delay_s(100) == 60);
    CHECK(wifi_reconnect_delay_s(-1) == 1);   // защита от мусора

    // Сумма 1+2+5+10+30+60 = 108с < 300с порога — расписание одну фазу fallback не покрывает.
    // ВКЛ (ap_fallback_enabled=true) — прежнее поведение, got_ip_this_boot не влияет.
    CHECK(!wifi_reconnect_should_fallback(true, true,  108));
    CHECK(!wifi_reconnect_should_fallback(true, false, 299));
    CHECK( wifi_reconnect_should_fallback(true, true,  300));
    CHECK( wifi_reconnect_should_fallback(true, false, 301));
    CHECK(!wifi_reconnect_should_fallback(true, true,  0));

    // AWF-2a настройка ВЫКЛ + IP УЖЕ БЫЛА в этой загрузке -> НИКОГДА, даже
    // сильно за порогом (живой тест 25.09, шаг 2).
    CHECK(!wifi_reconnect_should_fallback(false, true, 300));
    CHECK(!wifi_reconnect_should_fallback(false, true, 100000));

    // ВЫКЛ, но IP в этой загрузке ЕЩЁ НЕ БЫЛО (страховка) -> прежний порог.
    CHECK(!wifi_reconnect_should_fallback(false, false, 299));
    CHECK( wifi_reconnect_should_fallback(false, false, 300));

    // #AWF-F1: непроверенная сеть из портала — назад в портал после 5 неудач и >=15 с.
    CHECK( wifi_setup_should_return(true,  false, 5, 15));
    CHECK( wifi_setup_should_return(true,  false, 9, 108));
    CHECK(!wifi_setup_should_return(true,  false, 4, 100));   // мало неудач
    CHECK(!wifi_setup_should_return(true,  false, 5, 14));    // слишком рано
    CHECK(!wifi_setup_should_return(false, false, 50, 1000)); // сеть проверена/настроена раньше
    CHECK(!wifi_setup_should_return(true,  true,  50, 1000)); // IP в этой загрузке была
}
