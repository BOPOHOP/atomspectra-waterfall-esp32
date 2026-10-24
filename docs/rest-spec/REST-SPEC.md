# REST-интерфейс платы AtomSpectra-водопад — черновик спецификации для клиентов спектра

Версия документа: 0.2 (на рассмотрении), дата: 2026-10-22.
Объект: прошивка `1.2.38` (`version.txt:1`), коммит `29ad4d7`, тег `firmware-v1.2.38`.
Метод: только чтение исходников (`main/*.c|h`, `tests/host/`, `README.md`, `web/`). Плата и сеть не трогались, поведение на живом приборе НЕ проверялось.
Ссылки `file:line` — относительно корня дерева исходников прошивки в указанном теге. «Не нашёл» = в указанных файлах факта нет. Пометка **[вывод]** — умозаключение из кода, не прямая строка.

## 1. Общие сведения

| Пункт | Факт | Источник |
|---|---|---|
| Версия прошивки в API | `fw_version` в `/api/status` и `/api/system` = `version` из app-descriptor | `main/web_server.c:208`, `:2027` |
| Базовый URL | `http://<IP-платы>/`, порт не задаётся в коде проекта (берётся умолчание `HTTPD_DEFAULT_CONFIG()`); в документации адрес без порта | `main/web_server.c:2966`, `README.md:72` |
| Порт 80 | умолчание IDF, в репозитории числом не записано | не нашёл |
| TCP-мост | порт 8234 | `main/atomspectra.h:19` |
| Аутентификация | пользовательской нет; README: «не имеет аутентификации» | `README.md:~467-470` (раздел «Безопасность и модель доверия»); проверка Authorization/Basic/Bearer в `main/web_server.c` не найдена (поиск без учёта регистра — 0 совпадений); пароль относится к точке доступа Wi-Fi (`handle_ap_pass`) |
| CSRF | заголовок `X-CSRF-Token` обязателен на мутирующих POST (и на нескольких «тяжёлых» GET: `/api/debug/log`, `/api/ota/github/check`, выборочно другие) | `main/web_server.c:111-122`, `:400`, `:594` |
| Выдача токена | `GET /api/csrf-token` → `{"token":"<32 hex>"}`, без CSRF; токен генерируется заново при каждом старте платы | `main/web_server.c:93-100`, `:131-137`, `:2961` |
| Неверный/нет токена | `403`, текст `Bad or missing CSRF token` | `main/web_server.c:121` |
| Токен на GET спектра | не требуется для `spectrum.json`, `spectrum`, `meta.json`, `status`, `system`, `device`, `devlog` | отсутствие `csrf_check` в `handle_status:190`, `handle_spectrum:250`, `handle_spectrum_meta:256`, `handle_spectrum_json:331`, `handle_system:2016`, `handle_device:1927`, `handle_devlog:362` |
| Формат тел | JSON (`application/json`) у ручек спектра, `status`, `system`; `/api/spectrum` — `application/octet-stream`; у `POST /api/command` и `POST /api/reset` тип не задан явно (`httpd_resp_sendstr`, умолчание IDF) — клиенту не следует опираться на `Content-Type` этих ответов | `main/spectrum_http_cache.c:381,527` |
| Целые | беззнаковые десятичные без экспоненты (`PRIu32`) | `main/json_uint_fmt.h:11-19`, `main/spectrum_meta_fmt.h:106-111` |
| Дробные | `live` — `%.1f`; `t1..t3` — `%.1f` либо `null`; `calib[]` — `%.15g` | `main/spectrum_meta_fmt.h:107,126-128`, `main/spectrum_t1.c:41-44` |
| Время | в живом `spectrum.json` метки времени (epoch) НЕТ; `time` — целые секунды набора с учётом базы. `saved_at` (epoch, `%ld`) есть только в JSON сохранённых спектров | `main/spectrum_meta_fmt.h:107`; `main/web_server.c:313-314` |
| Единицы | `bins` — отсчёты; `time`, `live` — с; `cps` — имп/с (значение прибора); `cpu` — % | `README.md:~411-430` (таблица полей) |
| Порядок полей JSON | значим только для конструкций «ключ ищется подстрокой»; фиксирован кодом (см. п. 2.2) | `main/spectrum_fold.h:61-88` |

## 2. Эндпоинты для клиента спектра

Таблица маршрутов: `main/web_server.c:3009-3076` (+ водопад `main/web_waterfall.c:1277,1399`).

### 2.1. `GET /api/spectrum.json` без параметров (версия 1)

- Тело: `{"bins":[8192 целых],"total":…,"cpu":…,"cps":…,"lost":…,"time":…,"live":…,"bridge_drop":…,"usb_rx_err":…,"rx_ring_drops":…,"hist_ok":…,"hist_drop":…,"autosave_age_sec":…,"autosave_fail_streak":…,"t1":…,"t2":…,"t3":…,"serial":"…","dead":0|1,"calib":[…],"calib_set":true|false,"reset_unconfirmed":…}` — `main/spectrum_meta_fmt.h:105-135`, склейка `:144-170`.
- Поля `channels` в версии 1 НЕТ: склейка берёт `meta[1..a_end)` и `meta[b_start..]`, пропуская `,"channels":8192` (`main/spectrum_meta_fmt.h:121,161-163`). README (`README.md:~413`) пишет «длину повторяет поле `channels`» — для v1 по коду неверно **[вывод]**; поле есть в `meta.json` и в v2.
- `calib` присутствует только при `calib_valid` (`main/spectrum_meta_fmt.h:125-131`); `calib_set:false` — калибровка не задана (`main/calib_autoread.h`, использование `main/spectrum_meta_fmt.h:66`).
- Эпоха (`boot_id`, `dev_resets`, `spec_resets`, `calib_channels`) в v1 отсутствует; тест это проверяет побайтно (`tests/host/test_glue_epoch.c:262-271`).
- Заголовки ответа: `Content-Type: application/json`, `X-Spectrum-Render-Count` (номер поколения кэша), `Cache-Control: no-store`, `X-Hist-Age-Ms` (мс с последнего принятого фрагмента гистограммы на момент ответа, `-1` — ни одного с загрузки) — `main/spectrum_http_cache.c:248-263,379-383,452-456`.
- Кэш: тело живёт 2 с (`main/spectrum_http_cache.h:11`, проверка `main/spectrum_http_cache.c:203`); полный текст v1 строится лениво и общий для запросов поколения (`:371-388`); при смене калибровки кэш сбрасывается раньше (`main/spectrum.c:688,741,1467,1507`).
- Сжатие v1: нет (п. 4).

### 2.2. `GET /api/spectrum.json?channels=N` (версия 2)

- Допустимые N: `8192`, `4096`, `2048`, `1024` (`main/spectrum_fold.h:7-9`).
- Разбор параметра (`main/spectrum_fold.h:11-44`; обёртка `main/spectrum_http_cache.c:314-328`): ключ ищется в начале строки запроса или после `&`; значение — 1–5 десятичных цифр, без ведущего `0`; повтор ключа `channels` → ошибка; `channels` без `=` → ошибка; чужой ключ `channelsx=` не считается. Тест неверных строк: `tests/host/test_glue_fold.c:156` (`channels=3`, `channels=`, `channels=abc`, `channels=16384`, `channels=0`, `channels`, `channels=04096`).
- Строка запроса длиннее буфера (96 Б, т.е. ≥96 символов): `400`, если `channels` виден в уместившейся части или на её границе; иначе ответ как v1 (`main/spectrum_http_cache.c:316-325`).
- Свёртка: `m = 8192 / N`; k-й элемент `bins` = сумма `m` соседних исходных каналов `[k·m .. k·m+m-1]`; сумма считается в 64 бит и **насыщается** до `4294967295` (`UINT32_MAX`) (`main/spectrum_fold.h:46-57,69-76`). Для N=8192 свёртки нет (`m=1`).
- Состав v2, порядок ключей (`main/spectrum_fold.h:67-82`): `bins` (N целых) → поля meta от `total` до `dead` → `"channels":N` → хвост meta: `calib` (если есть), `calib_set`, `reset_unconfirmed` → `boot_id`, `dev_resets`, `spec_resets`, `calib_channels`. Хвост `meta[b_start .. len-1)` без закрывающей `}` (`:80`).
- `calib` в v2 копируется из meta без пересчёта (`main/spectrum_fold.h:80`): коэффициенты НЕ масштабируются под N. `calib_channels` всегда `8192` (`SPECTRUM_CHANNELS`, `main/spectrum_fold.h:81-82`, `main/atomspectra.h:8`; тест `tests/host/test_spectrum_epoch.c:204`). **[вывод]** Коэффициенты относятся к шкале 8192 каналов; для свёрнутого спектра номер канала k в формуле `E(ch)` надо пересчитать клиенту (в коде формулы пересчёта нет). Центр группы k в исходной шкале — вывод клиента, не прошивки.
- Тело строится при каждом запросе заново в приватном буфере из того же поколения кэша (`s_sp`, `s_meta`, `s_ep`), кэш v1 не трогается (`main/spectrum_http_cache.c:470-511`). Буфер `N·6+2048` байт, при нехватке — повторное построение (`:488-495`).
- Эпоха берётся парой со снимком (`main/spectrum_http_cache.c:56-58,485`), снимок и эпоха читаются в одном окне seqlock (`main/spectrum.c:964-977`).
- Заголовки v2: `Content-Type`, `X-Spectrum-Render-Count`, `Cache-Control: no-store`, `X-Hist-Age-Ms` — ровно три прикладных, проверено тестом (`tests/host/test_glue_fold.c:112`).
- Порядок проверок в обработчике: сначала параметр (400), затем наличие данных (404), затем сборка (`main/spectrum_http_cache.c:336-354`).

### 2.3. `GET /api/spectrum` (бинарный)

- Тело — массив `8192 × uint32` без обрамления, 32768 байт, `application/octet-stream`, `Content-Disposition: inline` (`main/spectrum_http_cache.c:513-533`, `main/spectrum_http_cache.h:39`). Параметры не принимаются.
- Порядок байт кодом не задан: шлётся память массива (`:296-297`); `main/spectrum_http_cache.h:39` называет формат «uint32 LE». Метаданных (время, калибровка) в теле нет — они в `meta.json`.
- Заголовки: `X-Spectrum-Render-Count`, `Cache-Control: no-store` (`snapshot_begin`, `:248-254,288-303`). `X-Hist-Age-Ms` у бинарного ответа НЕТ (ставится только у `.json`, `:256-263`).
- 404 с `X-Spectrometer-Dead` — как у `.json` (`:515-521`).
- Не сжимается (`main/spectrum_gz.h:2`).

### 2.4. `GET /api/spectrum/meta.json[?w=N]`

- Без `w` — meta без `bins` (`main/spectrum_http_cache.c:535-551`): те же поля от `total` до `dead`, затем `channels` (=8192), `calib` (если есть), `calib_set`, `reset_unconfirmed` (`main/spectrum_meta_fmt.h:105-135`). Эпохи (`boot_id`, `dev_resets`, `spec_resets`) в meta НЕТ.
- С `w` (целое, цифры): добавляются `sma`, `sma_w`, `sma_n` (`sma:null` при пустом кольце) и хвост живой мощности дозы `dose_rate`, `dose_mode`, `dose_w` (`main/spectrum_meta_fmt.h:174-180`, `main/spectrum_http_cache.c:557-602`, `main/meta_w_handler.h:29-36`). Ответ чанками, без `Content-Length`.
- `w` не целое (`1e+21`, `-5`, `12.5`, пусто) → ответ без SMA, `200` (`main/meta_w_parse.h:22-40`); значение > `UINT32_MAX` насыщается (`meta_w_parse.h:33,38`).
- `414` с текстом `w: query too long` — строка запроса длиннее буфера (128 Б) и значение `w` упирается в обрез (`main/meta_w_handler.h:28`, `main/meta_w_parse.h:22-40`).
- Заголовки: `X-Spectrum-Render-Count`, `Cache-Control: no-store` (`main/spectrum_http_cache.c:248-254`).

### 2.5. `GET /api/status`

Без CSRF. Поля (`main/web_server.c:168-246`):

| Поле | Тип | Смысл по коду | Строка |
|---|---|---|---|
| `analyzer_connected` | bool | USB-связь с прибором | `:197` |
| `wifi_connected` | bool | | `:198` |
| `total_counts` | число | суммарные отсчёты текущего спектра | `:199` |
| `cpu_load` | число | загрузка CPU прибора | `:200` |
| `tcp_client` | bool | подключён ли клиент TCP-моста | `:201` |
| `fs_formatted` | bool | ФС отформатирована при этой загрузке | `:203` |
| `fw_version`, `running_partition` | строки | | `:208-210` |
| `time` | число, с | `total_time_sec` текущего спектра | `:216` |
| `reset_reason`, `reset_reason_code` | строка, число | причина последнего перезапуска ESP32: `POWERON/EXT/SW/PANIC/INT_WDT/TASK_WDT/WDT/DEEPSLEEP/BROWNOUT/SDIO/UNKNOWN/OTHER` | `:171-174`, `:134-148` |
| `boot_count` | число | номер сессии платы, зафиксированный на загрузке (значение счётчика `sess` после первого успешного приращения) | `:175`, `main/boot_config.c:156,186-195` |
| `session` | число | текущее значение счётчика `sess` из NVS, читается при каждом запросе; растёт на загрузке и на Сбросе | `:176`, `main/boot_config.c:197-203` |
| `uptime_s` | число, с | с момента старта ESP32 | `:177` |
| `elf_sha` | строка | начало SHA256 ELF (буфер 17 Б) | `:178-180` |
| `base_time`, `base_counts` | числа | база, сложенная при сбросах прибора | `:181-184` |
| `dev_resets` | число | см. п. 5 | `:185` |
| `reset_unconfirmed` | число | Сбросы, которые прибор не выполнил | `:186-187` |
| `sweep_missed`, `load` | число, объект | диагностика | `:223-225` |
| `dose_mode`, `dose_rate`, `dose_w` | строка, число/`null`, число | живая МД за окно 10 с | `:226-235` |
| `dev`, `version`, `mode`, `freq`, `t1..t3`, `noise`, `max` | числа | только при `di->valid` (после ответа прибора на `-inf`) | `:238-249` |

При нехватке памяти — код `503` и тело `{"ok":false,"err":"oom"}`; при невозможности создать объект — `500` `oom` (`:193-195,252-255`).

### 2.6. `GET /api/system`

Без CSRF. Поля (`main/web_server.c:2016-2147`): `fw_version` (`:2027`), `free_heap` (`:2028`), `min_free_heap` (`:2029`, минимум свободной кучи за время работы — `esp_get_minimum_free_heap_size`), `heap_walk_us`, `heap_walk_age_ms`, `psram_total`, `psram_free`, `psram_largest` (`:2063`; обновляется не чаще раза в 10 с — `:2040-2047,2050-2052`), `int_free`, `int_largest`, `int_min`, `int_dflt_free`, `int_dflt_largest`, `cjson_spill`, `alloc_fail*`, `stack_min_free{}`, `uptime_sec` (`:2085`), `usb_connected`, `wifi_connected`, `tcp_client`, `flash_total`, `flash_used` (кэш 30 с), `rssi` и `ssid` (только если STA подключена к AP, `:2108-2112`), `net_mode`, `ap_*`, `return_block_reason`, `http_idle_s`, `time_source`, `sntp_synced`, `spectrum_render_count`, `spectrum_build_us`, `spectrum_build_us_max`, `spectrum_json_builds` (`:2133-2136`), поля `gz_*` (`:2137`, формирует `spectrum_gz_add_metrics`):

| Поле `gz_*` | Источник |
|---|---|
| `gz_level`, `gz_comp_bytes`, `gz_ok`, `gz_miss`, `gz_nomem`, `gz_fail`, `gz_last_us`, `gz_max_us`, `gz_last_in`, `gz_last_out`, `gz_stack_hwm` | `main/spectrum_gz.c:395-405` |
| `gz_async_ok`, `gz_fallback`, `gz_async_timeout`, `gz_queue_full`, `gz_send_err` | `main/spectrum_gz.c:408-412` |
| `gz_ring_us` (массив ≤16 последних времён сжатия, мкс) | `main/spectrum_gz.c:415-424`, `main/spectrum_gz_plan.h:163,210-223` |

Далее: `heap_walk_n`, `autosave_writes`, `autosave_skips`, `spec_seq_retry`, `spec_seq_fallback`, `http_heavy_busy`, `http_heavy_rejects` (`:2138-2144`). Единицы `gz_*` в коде не оговорены, кроме `*_us` (мкс) и `gz_stack_hwm` (байты по `uxTaskGetStackHighWaterMark`, IDF — вне репо).

### 2.7. `POST /api/reset`

- Требует `X-CSRF-Token`; тело не читается (`main/web_server.c:564-587`).
- Действие: сбрасывает хвост водопада (`spectrogram_flush_tail`), шлёт прибору `-rst`; при успешной отправке — `spectrum_reset()`, иначе `spectrum_reset_undelivered()` и отложенная досылка `-rst` (`:566-583`).
- Ответ: `200`, `{"ok":true,"sent":true}` либо `{"ok":true,"sent":false}` (`:586`); `sent:false` — прибору `-rst` не доставлен.
- Последствия (оба пути идут через `reset_state_zero`, `main/spectrum.c:845-865,872-885`): `spec_resets` +1 (`:849`), `s_spectrum.valid=false`, бины/`total`/`time` обнуляются (`:853-863`), удаляются `current.bin`, `current.bin.tmp`, `base.bin`, `base.bin.tmp` (`:897-921`), создаётся метка `reset.mark` (`:884`). Новая сессия платы открывается, только если сбрасывалось непустое (`main/session_plan.h:6-9`, `main/spectrum.c:848`).
- **Первый ответ после сброса — не обязательно 404.** `reset_impl` кэш не инвалидирует (вызовы `spectrum_http_cache_invalidate` в `main/spectrum.c` только для калибровки: `:688,741,1467,1507`; в `reset_impl:872-926` нет). В пределах TTL 2 с `spectrum.json` может вернуть `200` со спектром ДО сброса и со СТАРОЙ эпохой (кэш хранит снимок и эпоху парой, `main/spectrum_http_cache.c:56-58,203`). `404 No spectrum data yet` появляется после истечения кэша и пока не опубликована новая гистограмма (`valid=false` → снимок отвергнут, `main/spectrum.c:971,977`; `main/spectrum_http_cache.c:144-148`). README подтверждает (`README.md:~437-440`). Если кэш к моменту сброса уже истёк, первый же запрос даёт `404`; случай «200 со старым спектром в пределах 2 с после сброса» на живой плате не наблюдался **[не проверено вживую]**.
- Эпоха: новое значение `spec_resets` клиент увидит только в `200`, собранном после сброса. Ответ из кэша в пределах TTL несёт старую пару «снимок+эпоха»; пока `valid=false`, новая пара не собирается (`main/spectrum.c:967-977`) **[вывод]**.
- Другие пути, вызывающие `spectrum_reset()` и, следовательно, `spec_resets`+1: `POST /api/command` с `-rst` или `-sta … -r` (`main/usb_host_cdc.c:996`, `main/acq_intent.h:45-59`), `-rst`/`-sta … -r` от клиента TCP-моста (`main/tcp_bridge.c:178-186`), загрузка с `clear_spectrum` или меткой `reset.mark` (`main/main.c:120-126`, вызывается `spectrum_reset_undelivered`).

### 2.8. `GET /api/csrf-token`, `POST /api/reboot-esp`, `GET/POST /api/boot-config`

| Эндпоинт | Что делает | Строки |
|---|---|---|
| `GET /api/csrf-token` | `{"token":"…"}` (32 hex), без CSRF | `main/web_server.c:131-138` |
| `POST /api/reboot-esp` | CSRF; отвечает `{"ok":true}`, затем `spectrogram_prepare_reboot()`, пауза 500 мс, `esp_restart()` — перезагрузка ESP32, меняет `boot_id`, обнуляет RAM-счётчики; прибор не перезагружается | `:2669-2678` |
| `POST /api/reboot-device` | CSRF; шлёт прибору `CMD_REBOOT`; ответ `{"ok":true\|false}` | `:2001-2013` |
| `GET /api/boot-config` | без CSRF: `autostart_spectrum`, `autostart_waterfall`, `clear_spectrum`, `clear_waterfall`, `name_prefix`, `backup_keep`, `backup_hours`, `backup_test_minutes`, `field_ap_fallback`, `calib_always`, `dose_live`, `session`, `gzip_level` (последним) | `:833-870`, `:863` |
| `POST /api/boot-config` | CSRF; JSON до 255 Б; отсутствующие ключи сохраняют значение; ошибки: `400 Empty body`, `400 Bad JSON`, `400 backup_keep out of range`, `400 backup_hours out of range`, `400 gzip_level must be 0, 1 or 6`; ответ `{"ok":true\|false}`; уровень сжатия применяется сразу | `:872-950`, `:931` |

### 2.9. Прочие эндпоинты, полезные клиенту спектра

| Эндпоинт | Назначение | Строки |
|---|---|---|
| `POST /api/command` | CSRF; тело — сырой текст команды прибору (напр. `-sta`, `-sto`, `-sho`); `400 Empty body`, `400 Command too long` (>480 Б); ответ `{"ok":true\|false}` (запись в USB, подтверждения от прибора нет) | `main/web_server.c:338-358`; `README.md:~443-450` |
| `GET /api/devlog?since=N` | текстовые ответы прибора, без CSRF; журнал хранит последние 8 строк (по README) | `main/web_server.c:362-377`; `README.md:~445` |
| `GET /api/device` | параметры прибора, `calibration[]`, `calib_order`, `calib_set`, `calib_reject_seq`, `serial` | `main/web_server.c:1927-1999` |
| `POST /api/calibration` | CSRF; JSON `{"coeffs":[…]}` (до 5), ошибки `400`: `Empty body`, `Invalid JSON`, `Missing coeffs`, `Empty coeffs`, `Bad coeff` (не число / не конечное); коэффициенты сверх 5 отбрасываются молча (`main/web_server.c:2268`) | `main/web_server.c:2244-2290` |
| `GET /api/export.xml|csv|n42|spe` | экспорт живого спектра (BecqMoni XML, InterSpec CSV, N42, ЛСРМ SPE) | `main/web_server.c:3034-3037`; `README.md:~380` |
| `GET /healthcheck` | `200`, если USB и Wi-Fi в порядке, иначе `503`; поля `status`, `analyzer_connected`, `wifi_connected`, `uptime_sec`, `spectrum_valid` | `main/web_server.c:2680-2706` |
| `POST /api/save`, `GET /api/list`, `/api/saved/*`, `/api/backup/*` | сохранённые спектры/автоснимки | `main/web_server.c:3032-3042` |

Остальные маршруты (OTA, Wi-Fi, время, водопад) для клиента спектра не разбирались.

## 3. Коды ответов и тексты ошибок

| Ситуация | Код | Текст/тело | Источник |
|---|---|---|---|
| `channels` недопустим/дубль/без значения | `400` | `channels must be 8192, 4096, 2048 or 1024` | `main/spectrum_http_cache.c:340-343` |
| Данных ещё нет (после загрузки или сброса, до новой гистограммы); `spectrum.json`, `spectrum`, `meta.json` | `404` | `No spectrum data yet`, заголовок `X-Spectrometer-Dead: 1\|0` | `main/spectrum_http_cache.c:344-349,515-521,537-542,561-565,605-609` |
| Нет памяти под полный JSON v1 | `500` | `oom` | `main/spectrum_http_cache.c:416,429` |
| Нет памяти / сбой построения v2 | `500` | `oom` | `main/spectrum_http_cache.c:499-503` |
| Сбой сборки SMA/дозового хвоста | `500` | `meta sma` / `meta dose` | `main/spectrum_http_cache.c:582-585,620-623` |
| `meta.json?w=` слишком длинная строка запроса | `414` | `w: query too long` | `main/meta_w_handler.h:28` |
| Нет/неверный CSRF-токен | `403` | `Bad or missing CSRF token` | `main/web_server.c:121` |
| Неизвестный URI | `404` | `Not Found` | `main/web_server.c:2924-2928,3101` |
| Нет памяти для JSON `status`/`system`/`device` | `503` с `{"ok":false,"err":"oom"}` либо `500 oom` | | `main/web_server.c196,242-244,2020,2143-2145` |
| «Тяжёлый» слот занят (сохранённые спектры, экспорт и пр.) | `503`, заголовки `Retry-After: 1`, `Connection: close`; тело `{"ok":false,"class":"heavy",…}` | | `main/http_io_gate.c:89` |
| Таймаут сокета httpd | recv/send 3 с | | `main/web_server.c:2990-2991` |

Не нашёл: клиентских таймаутов и политики повторов в коде прошивки (она — сервер). Точное оформление тела `400/404/500` (`httpd_resp_send_err`) определяется ESP-IDF; в репозитории не проверено, хост-тесты тело ошибок не сравнивают (`tests/host/test_glue_fold.c:163` — `body_len == 0`).

## 4. Сжатие gzip (только v2)

| Условие | Факт | Источник |
|---|---|---|
| Что сжимается | только `GET /api/spectrum.json?channels=N` (v2); v1 и `/api/spectrum` — нет | `main/spectrum_http_cache.c:350-353`, `main/spectrum_gz.h:2`, `main/spectrum_http_cache.h:42` |
| Запрос клиента | `Accept-Encoding` содержит элемент `gzip` (регистр не важен) без `q=0`; `*`, `x-gzip`, `deflate` не считаются; читается первые 127 символов заголовка | `main/spectrum_gz_plan.h:73-106`, `main/spectrum_gz.c:345-348` |
| Уровень | `gz_level` ∈ {0, 1, 6}; по умолчанию **0 (выкл.)**; нет ключа NVS `gz_lvl` или иное значение → 0 | `main/spectrum_gz_plan.h:41-50`, `main/boot_config.c:64,97-101` |
| Где настраивается | `POST /api/boot-config` `{"gzip_level":0\|1\|6}` (CSRF), чтение — `GET /api/boot-config`, страница «Система» → «Старт платы» | `main/web_server.c:928-935`, `:863`; `web/system.html:303` |
| Включение | сразу, без перезагрузки; задача `spec_gz` создаётся при первом включении (ядро 0, приоритет 3, стек 16 КБ в PSRAM) | `main/spectrum_gz.c:36-38,306-336` |
| При записи спектрограммы | сжатие НЕ делается, ответ несжатый | `main/spectrum_gz_plan.h:166-168`, `main/spectrum_gz.c:357,366` |
| Очередь | глубина 2, срок задания 1500 мс; просроченное — несжатый ответ | `main/spectrum_gz_plan.h:160-163,181-183`, `main/spectrum_gz_job.h:44-49` |
| Нет PSRAM под буферы / компрессор | несжатый ответ (в пробе нужно ≥64 КБ и остаток ≥192 КБ) | `main/spectrum_gz_plan.h:28-31,137-141,164`, `main/spectrum_gz.c:358-360` |
| Тело короче 1024 Б, сжатое не короче несжатого, ошибка tdefl | несжатый ответ | `main/spectrum_gz_plan.h:28,120-129`, `main/spectrum_gz_job.h:69-73,89-102` |
| Каждый gzip-запрос идёт в очередь | `slot_valid=false` в `spec_gz_pick` → всегда «промах», готовых сжатых тел в кэше нет | `main/spectrum_gz.c:350` **[вывод]** |
| Заголовки сжатого ответа | `Content-Encoding: gzip`, `Vary: Accept-Encoding`, `X-Spectrum-Gz: 1`, а также `X-Spectrum-Render-Count`, `Cache-Control: no-store`, `X-Hist-Age-Ms`, `Content-Type: application/json` | `main/spectrum_gz.c:188-204` |
| Заголовки несжатого ответа v2 при включённом gzip | как в п. 2.2: без `Vary` и без `X-Spectrum-Gz` | `main/spectrum_http_cache.c:504-508`, `main/spectrum_gz.c:208-213` |
| Формат сжатого тела | gzip-кадр: заголовок 10 Б (`1f 8b 08 00 …`, OS=255), raw deflate (ROM tdefl), хвост CRC32+ISIZE; уровень 1 — жадный разбор, 6 — 128 проб | `main/spectrum_gz_plan.h:52-59,143-157` |

Тест включён как «тест, по умолчанию выключен» (`README.md:371`; `main/spectrum_gz.h:2-4`). Реальный выигрыш по размеру/времени на приборе не измерялся в рамках этого разбора.

## 5. Эпоха и сбросы

| Величина | Определение по коду | Когда меняется | Живёт в | Источник |
|---|---|---|---|---|
| `boot_id` | случайное ненулевое `uint32` (`fmix32(rnd ^ MAC_lo32 ^ timer_lo32)`; 0 заменяется на 1) | один раз за загрузку платы (после старта Wi-Fi) | RAM, не NVS | `main/spectrum.c:980-992`, `main/spectrum_epoch.h:9-23`, `main/main.c:86` |
| `dev_resets` | число «сворачиваний»: плата обнаружила, что накопление ПРИБОРА началось заново (по счёту отсчётов — всегда; по времени STAT — если STAT свежий), и добавила прежний спектр в базу. Показанный спектр при этом **не обнуляется**, `total` продолжает расти | +1 на каждое обнаруженное сворачивание; других инкрементов нет | RAM с загрузки платы | `main/spectrum.c:152,301`, `main/spectrum_epoch.h:25-29`, `main/spectrum_base_plan.h:112-130` |
| `spec_resets` | счётчик Сбросов СПЕКТРА (`s_reset_gen`): показанный спектр и база обнуляются | +1 в `reset_state_zero` при каждом Сбросе (кнопка/`/api/reset`/`-rst`/`-sta -r` из любого канала/автоочистка на старте); других инкрементов нет | RAM, после загрузки может быть ≥1 (автоочистка на старте) | `main/spectrum.c:154,849`, `main/spectrum_epoch.h:31-35`, `main/main.c:120-126` |
| `boot_count` | значение счётчика сессий NVS на первом успешном приращении после старта (обычно — загрузочное; при отказе записи NVS на загрузке — позднее) | +1 на загрузке (`boot_config_bump_session`); не меняется до перезагрузки | NVS (ключ `sess`) + RAM-копия `s_boot_sess` | `main/boot_config.c:156-190,195`; `main/main.c:109` |
| `session` | то же NVS-значение, читается живьём | +1 на загрузке и +1 на Сброс непустого спектра (задача main, не чаще одной записи за такт; при отказе записи NVS не меняется) | NVS | `main/boot_config.c:197-203`, `main/main.c:359-366`, `main/session_plan.h:6-18` |
| `calib_channels` | число каналов, в шкале которых заданы `calib` — всегда `8192` | не меняется | константа | `main/spectrum_fold.h:81-82` |

Как клиенту отличить событие (**[вывод]** из определений выше; на живой плате не проверялось):

| Событие | Признак |
|---|---|
| Перезагрузка платы | сменился `boot_id` (и `boot_count`, и `/api/status.uptime_s`/`reset_reason`); `dev_resets` и `spec_resets` начинаются с нуля (возможно, `spec_resets`=1 из-за автоочистки). Токен CSRF тоже новый (`main/web_server.c:2961`) |
| Сброс/перезапуск ПРИБОРА (плата работает) | `dev_resets` вырос; `boot_id`, `spec_resets` те же; спектр не обнуляется, `total` не падает (прежний набор сложен в базу) |
| Сброс СПЕКТРА (кнопка, `/api/reset`, `-rst`) | `spec_resets` вырос, `boot_id` тот же; спектр обнулён — `total`/`time` падают; `session` +1 (если сбрасывалось непустое), `boot_count` не меняется |
| «Пустой» промежуток | `404 No spectrum data yet` пока нет новой гистограммы |

Предупреждение: из-за кэша 2 с после Сброса клиент может получить до одного-двух ответов со старым спектром и старой эпохой (п. 2.7). Совпадает ли `dev_resets`+1 с одновременным Сбросом спектра, из кода однозначно не следует (база после Сброса обнуляется, `main/spectrum.c:862-863`; прямого теста не нашёл).

## 6. TCP-мост (порт 8234)

| Пункт | Факт | Источник |
|---|---|---|
| Порт, адрес | `INADDR_ANY:8234` | `main/atomspectra.h:19`, `main/tcp_bridge.c:204-208` |
| Клиентов | один; второй принимается и сразу закрывается (`listen(fd,1)`, лог «Rejecting second client») | `main/tcp_bridge.c:215,230-237`, `main/socket_budget.h:12` |
| Вход (от клиента) | байты идут СЫРЫМ проходом в `usb_host_cdc_send`, без преобразования; буфер приёма 1024 Б | `main/tcp_bridge.c:149,165,179` |
| Любой входящий байт | намерение набора → UNKNOWN (сторож набора перестаёт повторять `-sta`) | `main/tcp_bridge.c:176`, `main/usb_host_cdc.c:822-826` |
| Распознаётся платой в потоке клиента | только `CMD_TEXT` с командой сброса: `-rst` (точно) и `-sta` с токеном `-r` | `main/tcp_bridge.c:112-143`, `main/acq_intent.h:45-59` |
| Реакция на `-rst`/`-sta … -r` | сброс хвоста водопада; после успешной передачи прибору (`rc==0`) — `spectrum_reset()` (база платы очищается, `spec_resets`+1); при ошибке отправки — база сохраняется | `main/tcp_bridge.c:178-187` |
| Команды `-cal`, `-sto`, `-inf`, `-sta` без `-r` от клиента | плата их НЕ интерпретирует, пересылает прибору как есть; запрос калибровки перед `-sta` (авточтение `-cal`) для TCP-моста не делается (намеренно) | `main/usb_host_cdc.c:1001-1020`; обработчика `-cal`/`-sto` в `tcp_bridge.c` нет |
| Список команд протокола прибора | `-sta [xx] [-r] [-s]`, `-sto`, `-sho`, `-stt`, `-rst` и др. — описаны в `PROTOCOL.md`, а не в коде моста | `PROTOCOL.md:~26-45` |
| Выход (прибор→клиент) | сырой поток прибора через PSRAM-кольцо 256 КБ; блок `send` 4096 Б; `SO_SNDTIMEO` 1000 мс — при таймауте остаток куска отбрасывается и считается в `bridge_drop`; также `bridge_drop` растёт при переполнении кольца (`main/tcp_bridge.c:61`) | `main/tcp_bridge.c:38-41,69-110`, `:245-248` |
| Нет клиента | поток не копится | `main/tcp_bridge.c:57` |
| Параметры сокета | `TCP_NODELAY`, keepalive idle 30 с / interval 10 с / count 3 | `main/tcp_bridge.c:239-258` |
| Аутентификация | нет | `README.md:~458`, код `tcp_bridge.c` |
| Потоки задач | `tcp_srv`, `tcp_tx`, `tcp_rx` на core 1 | `main/tcp_bridge.c:296-300` |

Не нашёл в коде моста: фрейминга/версионирования, ответа на неизвестные команды, ограничения скорости команд.

## 7. Ограничения и поведение под нагрузкой

| Пункт | Факт | Источник |
|---|---|---|
| Поток httpd | одна задача, обработчики исполняются строго по очереди (ядро 1, стек 8192) | `main/http_gate_budget.h:6-8`, `main/web_server.c:2970,2981` |
| Открытые сокеты httpd | 19 (+3 служебных IDF +2 TCP-моста = 24 при сборке) | `main/socket_budget.h:9-13`, `main/web_server.c:2982` |
| Вытеснение | `lru_purge_enable=true`: при исчерпании пула закрывается самое давнее соединение | `main/web_server.c:2983` |
| Таймауты | recv/send 3 с; keep-alive TCP idle 5 с, interval 5 с, count 3 | `main/web_server.c:2990-2991,2998-3001` |
| Обработчиков URI | до 90 (комментарий: запас на момент пересчёта) | `main/web_server.c:2980,2934-2936` |
| Кэш спектра | TTL 2 с, одиночная пересборка (single-flight), общая для вкладок | `main/spectrum_http_cache.h:8-11`, `main/spectrum_http_cache.c:196-217` |
| Частота опроса | в коде явной нормы нет; README: чаще раза в 2 с новых данных кэш не даёт; Web UI опрашивает каждые 2000 мс | `README.md:~430`, `web/index.html:914` |
| «Тяжёлый» слот | concurrency=1 для флеш-операций (сохранённые спектры, экспорт, импорт); занят → `503 Retry-After: 1`; ожидание слота до 2000 мс | `main/http_io_gate.h:6-8`, `main/http_gate_budget.h:20-30` |
| Очередь сжатия | глубина 2; переполнение → несжатый ответ, `gz_queue_full`+1 | `main/spectrum_gz_plan.h:161`, `main/spectrum_gz.c:362-378` |
| Срок задания | 1500 мс | `main/spectrum_gz_plan.h:162,181-183` |
| Память | при включённом gzip постоянно ≈180 КиБ PSRAM (компрессор + стек), на задание — ещё буферы | `web/system.html:303` (текст UI), `main/spectrum_gz.c:36,76` |
| Размер ответа | v1 ≈ до `8192·11` Б + meta (буфер ≥64 КБ, растёт); v2 — `N·6+2048` Б минимум; `/api/spectrum` — 32768 Б | `main/spectrum_http_cache.c:411,488`, `README.md:370` |
| TCP_NODELAY | включается на сокете для ответов спектра | `main/spectrum_http_cache.c:265-280` |
| Параллельная работа каналов | согласования команд между TCP-мостом, Web UI и REST в коде не обнаружено (README) | `README.md:~459-461` |

## 8. Что не установлено / не проверено

| № | Вопрос | Статус |
|---|---|---|
| 1 | Порт HTTP-сервера числом | умолчание IDF; в репозитории не записано (README — адрес без порта) |
| 2 | Точный текст и заголовки тел `400/404/500` на проводе (`httpd_resp_send_err`) | зависит от IDF, не проверено |
| 3 | Порядок байт `/api/spectrum` | кодом не задан; «LE» — комментарий в `spectrum_http_cache.h:39` |
| 4 | Применение `calib` к свёрнутому спектру (центры групп) | формулы в коде нет |
| 5 | Совпадение `dev_resets`+1 с Сбросом спектра | прямого теста не нашёл |
| 6 | Поведение на живой плате (все пункты выше) | в этом разборе не измерялось; только код и host-тесты |
| 7 | Выигрыш gzip по размеру/времени | не измерялся |
| 8 | Рекомендуемая частота опроса сверх «≥2 с» | кроме README/UI нет |
| 9 | Поведение после перезагрузки: гарантия уникальности `boot_id` | вероятностная (32 бит), порог коллизии в доках не указан |
| 10 | Что делает прибор с `-sto` в части гистограмм | README: «на приборе не проверялось» (`README.md:~441`) |
| 11 | Неконечные значения `calib` (NaN/Inf) в JSON meta | формат `%.15g` не защищает; источник значений из прибора не разбирался |
| 12 | Расхождения документации: README описывает REST по 1.2.37 (`README.md:~407`) и называет `channels` полем v1 | v1 без `channels` по коду (п. 2.1); README про v2 (`?channels=N`) в таблице эндпоинтов, кроме строки gzip, не говорит |
| 13 | Объём ответов `/api/status` без `load` и т.д. | не измерялся |
