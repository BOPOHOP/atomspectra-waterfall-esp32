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

    // причина отказа: стираем только по «неверный пароль» (15/202/204), не по NO_AP_FOUND и т.п.
    CHECK( wifi_reason_is_bad_password(15));
    CHECK( wifi_reason_is_bad_password(202));
    CHECK( wifi_reason_is_bad_password(204));
    CHECK(!wifi_reason_is_bad_password(201));   // NO_AP_FOUND
    CHECK(!wifi_reason_is_bad_password(200));   // BEACON_TIMEOUT
    CHECK(!wifi_reason_is_bad_password(205));   // CONNECTION_FAIL
    CHECK(!wifi_reason_is_bad_password(2));     // AUTH_EXPIRE
    CHECK(!wifi_reason_is_bad_password(0));
    CHECK(wifi_setup_fail_bump(3, 202) == 4);
    CHECK(wifi_setup_fail_bump(wifi_setup_fail_bump(4, 201), 15) == 1);   // F2: «подряд» — 201 обрывает серию
    CHECK(wifi_setup_fail_bump(0, 0) == 0);
    int f = 0;                                  // медленный роутер: 5 NO_AP_FOUND — не возврат
    for (int i = 0; i < 5; i++) f = wifi_setup_fail_bump(f, 201);
    CHECK(!wifi_setup_should_return(true, false, f, 100));
    for (int i = 0; i < 5; i++) f = wifi_setup_fail_bump(f, 15);   // неверный пароль — возврат
    CHECK( wifi_setup_should_return(true, false, f, 100));
    // F3/F4: длины SSID (1..32) и пароля (0, 8..64) — граница обоих краёв.
    CHECK(!wifi_ssid_len_ok(0)); CHECK(wifi_ssid_len_ok(1));
    CHECK(wifi_ssid_len_ok(32)); CHECK(!wifi_ssid_len_ok(33));
    CHECK(wifi_pass_len_ok(0));  CHECK(!wifi_pass_len_ok(1));
    CHECK(!wifi_pass_len_ok(7)); CHECK(wifi_pass_len_ok(8));
    CHECK(wifi_pass_len_ok(63)); CHECK(wifi_pass_len_ok(64));
    CHECK(!wifi_pass_len_ok(65));
    // pass3B #4: полная проверка пароля — 64 допустимо только hex, иначе 8..63 печатных ASCII.
    char h64[65], n64[65], p63[64], p8[9], p7[8];
    memset(h64, 'a', 64); h64[64] = 0; h64[10] = 'F'; h64[63] = '0'; h64[20] = 'f'; h64[5] = '9'; h64[6] = 'A';
    memset(n64, 'a', 64); n64[64] = 0; n64[30] = 'g';
    memset(p63, 'x', 63); p63[63] = 0;
    memset(p8, 'x', 8);   p8[8] = 0;
    memset(p7, 'x', 7);   p7[7] = 0;
    CHECK( wifi_pass_ok(h64));                  // 64 hex
    CHECK(!wifi_pass_ok(n64));                  // 64, но не hex
    CHECK( wifi_pass_ok(p63)); CHECK( wifi_pass_ok(p8));
    CHECK(!wifi_pass_ok(p7));  CHECK( wifi_pass_ok(""));   // 7 — отказ; пусто = открытая сеть
    CHECK(!wifi_pass_ok("pass\x01word")); CHECK(!wifi_pass_ok("\xd0\xbf\xd0\xb0\xd1\x80\xd0\xbe\xd0\xbb\xd1\x8c"));
    CHECK( wifi_pass_ok("pass word!"));         // пробел и пунктуация — печатные
    // pass3B #8: причины, которые НЕ «неверный пароль» (мутанты 203/14 обязаны краснеть).
    CHECK(!wifi_reason_is_bad_password(203));   // ASSOC_FAIL
    CHECK(!wifi_reason_is_bad_password(14));    // MIC_FAILURE
    CHECK(!wifi_reason_is_bad_password(16));    CHECK(!wifi_reason_is_bad_password(206));
    CHECK(!wifi_reason_is_bad_password(255));   CHECK(!wifi_reason_is_bad_password(1));
}

// #AWF-WIFI-1: срок fallback-таймера (unverified -> 90 с, подтверждённая -> 300 с).
// Один CHECK на свойство: точечный мутант красит ровно то, что портит.
void test_wifi_fallback_timeout(void)
{
    CHECK(wifi_fallback_timeout_s(true) == 90u);    // непроверенная сеть из портала, секунды
    CHECK(wifi_fallback_timeout_s(false) == 300u);  // подтверждённая: срок прежний
    CHECK(WIFI_SETUP_FALLBACK_S < WIFI_RECONNECT_FALLBACK_S);   // инвариант 90 < 300
}
