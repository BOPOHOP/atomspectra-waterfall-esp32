// AWF-2a (#1): паузы между попытками STA-реконнекта с нарастанием, планируются
// через esp_timer в wifi_manager.c (не блокируя обработчик событий). Модуль
// свободен от ESP-IDF — host-тестируется на обычном gcc (образец acq_watch.h).
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define WIFI_RECONNECT_STEPS 6
static const uint32_t WIFI_RECONNECT_SCHEDULE_S[WIFI_RECONNECT_STEPS] =
    { 1, 2, 5, 10, 30, 60 };

// До ухода в fallback AP должно пройти не меньше ~5 минут — короткая
// перезагрузка роутера (1-2 мин) не должна уводить плату в поле раньше, чем
// роутер успевает подняться.
#define WIFI_RECONNECT_FALLBACK_S 300u

// #AWF-WIFI-1 (1.2.33): сеть из портала настройки, ещё НЕ подтверждённая (ни разу
// не было IP, s_unverified), возвращается в портал по короткому сроку — при опечатке
// в пароле/SSID ждать 300 с незачем. Единица — секунды. Подтверждённая сеть: 300 с.
#define WIFI_SETUP_FALLBACK_S 90u

// Срок fallback-таймера в секундах: единственное место выбора (wifi_manager.c).
static inline uint32_t wifi_fallback_timeout_s(bool unverified)
{
    return unverified ? WIFI_SETUP_FALLBACK_S : WIFI_RECONNECT_FALLBACK_S;
}

// Пауза перед (attempt+1)-й попыткой (attempt считается с 0). После
// исчерпания расписания повторяет последний шаг, пока не сработает fallback.
static inline uint32_t wifi_reconnect_delay_s(int attempt)
{
    if (attempt < 0) attempt = 0;
    if (attempt >= WIFI_RECONNECT_STEPS) attempt = WIFI_RECONNECT_STEPS - 1;
    return WIFI_RECONNECT_SCHEDULE_S[attempt];
}

// AWF-2a настройка (живой тест 25.09, шаг 2 «Wi-Fi выкл 8 мин»): решение об
// уходе в Field AP теперь зависит ещё от настройки «Переходить в полевую точку
// доступа при потере Wi-Fi» (ap_fallback_enabled) и от того, получала ли STA
// IP хоть раз в ЭТОЙ загрузке (got_ip_this_boot). ВЫКЛ + IP уже была в этой
// загрузке -> НИКОГДА не уходить (бесконечный реконнект по расписанию,
// потолок паузы 60с — WIFI_RECONNECT_SCHEDULE_S). Иначе (ВКЛ, либо страховка:
// IP в этой загрузке ещё не было ни разу) — прежний порог по времени.
static inline bool wifi_reconnect_should_fallback(bool ap_fallback_enabled,
                                                   bool got_ip_this_boot,
                                                   uint32_t elapsed_disconnected_s)
{
    if (!ap_fallback_enabled && got_ip_this_boot) return false;
    return elapsed_disconnected_s >= WIFI_RECONNECT_FALLBACK_S;
}

// #AWF-F1 (1.2.32, Макс): сеть, введённую в портале настройки, плата ни разу не
// проверяла — неверный пароль оставался в NVS. Пока признак «не проверена» стоит
// (ставит портал, снимает первая выданная IP) и подключиться не удалось за
// WIFI_SETUP_VERIFY_FAILS неудач подряд (не раньше WIFI_SETUP_VERIFY_MIN_S с),
// плата стирает сеть и снова открывает портал. Платы без признака (все настроенные
// раньше) не затронуты.
#define WIFI_SETUP_VERIFY_FAILS 5
#define WIFI_SETUP_VERIFY_MIN_S 15u
static inline bool wifi_setup_should_return(bool unverified, bool got_ip_this_boot,
                                            int fails, uint32_t elapsed_s)
{
    return unverified && !got_ip_this_boot &&
           fails >= WIFI_SETUP_VERIFY_FAILS && elapsed_s >= WIFI_SETUP_VERIFY_MIN_S;
}

// Причина отказа STA, означающая «неверный пароль» (ESP-IDF 5.4, esp_wifi_types_generic.h):
// 15 = 4WAY_HANDSHAKE_TIMEOUT, 202 = AUTH_FAIL, 204 = HANDSHAKE_TIMEOUT. Остальные
// (201 NO_AP_FOUND, 200 BEACON_TIMEOUT, 205 CONNECTION_FAIL, 2 AUTH_EXPIRE, 8 ASSOC_LEAVE...)
// — медленный/далёкий/перезагружающийся роутер: верную сеть из-за них не стираем.
static inline bool wifi_reason_is_bad_password(uint8_t reason)
{
    return reason == 15 || reason == 202 || reason == 204;
}

// Счётчик неудач для wifi_setup_should_return: ПОДРЯД идущие отказы «неверный пароль».
// Любая другая причина (201/200/205...) обрывает серию и обнуляет счётчик; на GOT_IP
// его обнуляет вызывающий (wifi_manager.c). Иначе 5 редких 15/204 при перезагрузке
// роутера, разбавленные 201, стёрли бы верную сеть.
static inline int wifi_setup_fail_bump(int fails, uint8_t reason)
{
    return wifi_reason_is_bad_password(reason) ? fails + 1 : 0;
}

// Длины, которые принимает esp_wifi_set_config для STA: SSID 1..32 байта; пароль пустой
// (открытая сеть), 8..63 символа (WPA2-PSK) либо ровно 64 (hex PSK). 1..7 — отказ
// (раньше ESP_ERROR_CHECK -> abort -> бутлуп). Проверка по байтам (strlen), не по символам.
static inline bool wifi_ssid_len_ok(size_t len)
{
    return len >= 1 && len <= 32;
}
static inline bool wifi_pass_len_ok(size_t len)
{
    return len == 0 || (len >= 8 && len <= 64);
}
// Полная проверка пароля: пусто (открытая сеть); 8..63 печатных ASCII (0x20..0x7E);
// ровно 64 — только если ВСЕ символы hex (иначе esp_wifi_set_config отвергает -> сеть стёрта).
static inline bool wifi_pass_ok(const char *p)
{
    size_t n = strlen(p);
    bool hex = true;
    if (!wifi_pass_len_ok(n)) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)p[i];
        if (c < 0x20 || c > 0x7e) return false;
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) hex = false;
    }
    return n != 64 || hex;
}
