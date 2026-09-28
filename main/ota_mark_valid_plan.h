#pragma once
#include <stdbool.h>
#include <stdint.h>
// D3 (2026-09-27, P1, verify-awf4-2026-09-27.md разд.3/5): mark_app_valid
// зависел ТОЛЬКО от wifi_is_connected(), которое в Field/Outdoor AP-режиме
// БЕЗ клиентов НИКОГДА не становится true (wifi_manager.c:602-604). Образ,
// успешно перешедший на новую прошивку и осевший в AP без клиентов, никогда
// не подтверждался; любой следующий краш по НЕ связанной с образом причине
// откатывал бы бутлоадер (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y).
// Правило: образ годен, если (а) реально подключен Wi-Fi (STA/AP-с-клиентом,
// самый сильный сигнал — как раньше), ЛИБО (б) USB-прибор подключён (плата
// жива и работает по назначению независимо от режима Wi-Fi), ЛИБО (в) httpd
// поднят и прошло >= N секунд БЕЗ падения (тик main() выполнился — доказано,
// что цикл жив после ребута/OTA).
//
// N=30с (3 тика по 10с, main.c): достаточно пережить ранние watchdog-
// чувствительные фазы (Wi-Fi connect timeout, USB enumerate — единицы
// секунд), но мало, чтобы не держать образ в PENDING_VERIFY дольше нужного —
// откат бутлоадера срабатывает на СЛЕДУЮЩЕЙ загрузке, не по таймеру. Число
// НЕ измерено на живой плате (нет разрешения на реальный OTA+power-cycle на
// единственном стенде в этой сессии) — по коду/рассуждению, честно (#AH-1).
static inline bool ota_mark_valid_should_fire(bool wifi_connected,
                                               bool usb_connected,
                                               bool httpd_up,
                                               uint32_t seconds_since_boot,
                                               uint32_t min_seconds_httpd)
{
    if (wifi_connected) return true;
    if (usb_connected) return true;
    if (httpd_up && seconds_since_boot >= min_seconds_httpd) return true;
    return false;
}
