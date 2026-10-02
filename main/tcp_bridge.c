#include "atomspectra.h"
#include "shproto.h"
#include "acq_intent.h"      /* P1-a: cmd_is_device_reset() */
#include "spectrogram.h"     /* #RST-TAIL: spectrogram_flush_tail() */
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include <string.h>
#include <errno.h>

static const char *TAG = "tcp_bridge";

static int s_client_fd = -1;
static int s_server_fd = -1;

// P2-4: s_client_fd трогают четыре задачи (usb_to_tcp_cb feed / tcp_tx_task
// send+close / tcp_rx_task recv+close / tcp_server_task accept). Мьютекс
// закрывает use-after-close; блокирующие recv()/send() делаем вне лока.
static SemaphoreHandle_t s_fd_mutex = NULL;
#define FD_LOCK()   do { if (s_fd_mutex) xSemaphoreTake(s_fd_mutex, portMAX_DELAY); } while (0)
#define FD_UNLOCK() do { if (s_fd_mutex) xSemaphoreGive(s_fd_mutex); } while (0)

// #TCP-1: декаплинг USB-RX ↔ TCP-TX через PSRAM-кольцо (StreamBuffer).
// Корень бага: раньше usb_to_tcp_cb звал send(fd,…,MSG_DONTWAIT) прямо из
// CDC-задачи и игнорировал возврат — при переполнении крошечного LWIP-буфера
// (CONFIG_LWIP_TCP_SND_BUF_DEFAULT) байты молча терялись посреди потока →
// рассинхрон sh_proto-фрейминга у клиента («битые пакеты», ~41% на 61 с).
// Наивный блокирующий send() из CDC-задачи нельзя: она же читает USB →
// застрянет USB → потеря уедет в device lostImp. Поэтому producer
// (usb_to_tcp_cb) НИКОГДА не блокирует — только кладёт в кольцо; consumer
// (tcp_tx_task) блокирующе шлёт в сокет с SO_SNDTIMEO + partial-loop.
#define TX_RING_BYTES   (256 * 1024)   // ~4.4 с потока @ 58 КБ/с — буфер на WiFi-джиттер
#define TX_RING_TRIGGER 1              // будить consumer как только есть ≥1 байт
#define TX_CHUNK_BYTES  4096           // сколько тянем из кольца за раз
#define TX_SNDTIMEO_MS  1000           // потолок блокировки send() на застрявшем клиенте

static StreamBufferHandle_t s_tx_ring = NULL;
static StaticStreamBuffer_t s_tx_ring_struct;   // во внутренней RAM (маленькая)
static uint8_t *s_tx_ring_storage = NULL;       // в PSRAM
// Н-5.1: момент последнего обмена с клиентом (send/recv), мс; uint32 — атомарное чтение.
static volatile uint32_t s_client_io_ms = 0;
static inline uint32_t now_ms32(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static volatile uint32_t s_bridge_dropped = 0;  // байт потеряно (overflow кольца + send-timeout)

// producer: вызывается из CDC-задачи на каждый де-FTDI'нутый кусок. Не блокирует.
static void usb_to_tcp_cb(const uint8_t *data, size_t len)
{
    FD_LOCK();
    int fd = s_client_fd;
    FD_UNLOCK();
    if (fd < 0) return;                  // нет клиента — не копим зря

    if (s_tx_ring) {
        size_t put = xStreamBufferSend(s_tx_ring, data, len, 0);   // 0 = без блокировки
        if (put < len) s_bridge_dropped += (uint32_t)(len - put);  // кольцо переполнено
    } else {
        // фоллбэк, если PSRAM-кольцо не выделилось: деградированный прямой режим
        ssize_t sn = send(fd, data, len, MSG_DONTWAIT);   // C-34: хвост не терять молча
        if (sn < (ssize_t)len) s_bridge_dropped += (uint32_t)(len - (sn > 0 ? (size_t)sn : 0));
    }
}

// consumer: единственный, кто шлёт в TCP. Блокирующий send с таймаутом и partial-loop.
static void tcp_tx_task(void *arg)
{
    uint8_t *buf = heap_caps_malloc(TX_CHUNK_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = malloc(TX_CHUNK_BYTES);
    if (!buf) {
        ESP_LOGE(TAG, "tx_task buffer alloc failed");
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        if (!s_tx_ring) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }

        size_t n = xStreamBufferReceive(s_tx_ring, buf, TX_CHUNK_BYTES, pdMS_TO_TICKS(200));
        if (n == 0) continue;            // таймаут, данных нет

        FD_LOCK();
        int fd = s_client_fd;
        FD_UNLOCK();
        if (fd < 0) continue;            // клиент ушёл — слитые байты выбрасываем

        size_t off = 0;
        while (off < n) {
            int w = send(fd, buf + off, n - off, 0);   // блокирующий (с SO_SNDTIMEO)
            if (w > 0) { off += (size_t)w; s_client_io_ms = now_ms32(); continue; }
            if (w < 0 && errno == EINTR) continue;
            if (w < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
                // send-timeout: клиент тормозит дольше TX_SNDTIMEO_MS. Бросаем
                // остаток куска (last-resort), считаем, не вешаем USB.
                s_bridge_dropped += (uint32_t)(n - off);
                break;
            }
            // реальная ошибка сокета → закрываем клиента
            ESP_LOGI(TAG, "tx send error, closing client (errno=%d)", errno);
            FD_LOCK();
            if (s_client_fd == fd) { close(fd); s_client_fd = -1; }
            FD_UNLOCK();
            break;
        }
    }
}

// P1-a: PC-клиент (BecqMoni/AtomSpectra) шлёт байты СЫРЫМ проходом (см. #TCP-1
// выше) прямо usb_host_cdc_send() — мимо usb_host_send_text_command(), поэтому
// его «-rst» не чистит базу, если не декодировать CMD_TEXT здесь тоже. Свой
// декодер, отдельный от s_rx_packet в usb_host_cdc.c (тот — для device→gateway).
static uint8_t s_pc_cmd_buf[512];
static shproto_struct s_pc_cmd_decoder;
static bool s_pc_cmd_decoder_init;
// Скормить сырые байты клиента декодеру; true — в куске был полный CMD_TEXT с «-rst».
// F-05: сам Сброс платы — у вызывающего и только после доставки прибору (rc==0),
// как send_text_command_raw (usb_host_cdc.c) и кнопка UI «Сброс».
static bool tcp_scan_for_reset_cmd(const uint8_t *data, size_t n)
{
    bool saw_rst = false;
    if (!s_pc_cmd_decoder_init) {
        shproto_init(&s_pc_cmd_decoder, s_pc_cmd_buf, sizeof(s_pc_cmd_buf));
        s_pc_cmd_decoder_init = true;
    }
    for (size_t i = 0; i < n; i++) {
        shproto_byte_received(&s_pc_cmd_decoder, data[i]);
        if (s_pc_cmd_decoder.ready) {
            s_pc_cmd_decoder.ready = false;
            if (s_pc_cmd_decoder.cmd == CMD_TEXT && s_pc_cmd_decoder.len > 0 &&
                s_pc_cmd_decoder.data[s_pc_cmd_decoder.len - 1] == '\0' &&
                cmd_is_device_reset((const char *)s_pc_cmd_decoder.data)) {
                saw_rst = true;
            }
        } else if (s_pc_cmd_decoder.dropped) {
            s_pc_cmd_decoder.dropped = false;
        }
    }
    return saw_rst;
}

static void tcp_rx_task(void *arg)
{
    // Гейт 1.2.29 (stack_min_free.tcp_rx = 508 Б после первого клиента): приёмный буфер
    // не на стеке, а в PSRAM — cdc_acm_host_data_tx_blocking копирует данные в свой буфер.
    enum { RX_BUF = 1024 };
    uint8_t *buf = NULL;
    while (!buf) {   // F-5: не удалять задачу при отказе — мост ПК→прибор умер бы молча
        buf = heap_caps_malloc(RX_BUF, MALLOC_CAP_SPIRAM);
        if (!buf) buf = malloc(RX_BUF);
        if (!buf) { ESP_LOGE(TAG, "rx buffer alloc failed, retry in 1 s"); vTaskDelay(pdMS_TO_TICKS(1000)); }
    }
    while (1) {
        FD_LOCK();
        int fd = s_client_fd;
        FD_UNLOCK();
        if (fd < 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        int n = recv(fd, buf, RX_BUF, 0);   // блокирующий recv — вне лока
        if (n <= 0) {
            ESP_LOGI(TAG, "Client disconnected");
            FD_LOCK();
            if (s_client_fd == fd) { close(fd); s_client_fd = -1; }
            FD_UNLOCK();
            continue;
        }
        s_client_io_ms = now_ms32();
        // Прибором управляет внешнее приложение: шлюз не знает, запущен ли набор,
        // и сторож набора не должен перебивать его «Стоп» своим -sta.
        usb_host_cdc_acq_intent_external();
        bool saw_rst = tcp_scan_for_reset_cmd(buf, n);
        if (saw_rst) (void)spectrogram_flush_tail(1200);   // #RST-TAIL: хвост строкой водопада, ДО передачи -rst прибору
        int rc = usb_host_cdc_send(buf, n);
        if (saw_rst) {
            if (rc == 0) {
                ESP_LOGW(TAG, "TCP client sent -rst -- clearing base");
                spectrum_reset();
            } else {
                ESP_LOGW(TAG, "TCP client -rst not delivered to device (rc=%d) -- board base kept", rc);
            }
        }
    }
}

static void tcp_server_task(void *arg)
{
    s_server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s_server_fd < 0) {
        ESP_LOGE(TAG, "Socket create failed");
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    if (setsockopt(s_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
        ESP_LOGW(TAG, "SO_REUSEADDR failed: errno=%d", errno);

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(TCP_BRIDGE_PORT),
        .sin_addr.s_addr = INADDR_ANY
    };
    if (bind(s_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "Bind port %d failed", TCP_BRIDGE_PORT);
        close(s_server_fd);
        vTaskDelete(NULL);
        return;
    }
    if (listen(s_server_fd, 1) < 0) {
        ESP_LOGE(TAG, "Listen on port %d failed: errno=%d", TCP_BRIDGE_PORT, errno);
        close(s_server_fd);
        s_server_fd = -1;
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Listening on port %d", TCP_BRIDGE_PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t clen = sizeof(client_addr);
        int fd = accept(s_server_fd, (struct sockaddr *)&client_addr, &clen);
        if (fd < 0) continue;

        FD_LOCK();
        bool busy = (s_client_fd >= 0);
        FD_UNLOCK();
        if (busy) {
            ESP_LOGW(TAG, "Rejecting second client");
            close(fd);
            continue;
        }

        int nodelay = 1;
        if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0)
            ESP_LOGW(TAG, "TCP_NODELAY failed: errno=%d", errno);

        // #TCP-1: ограничиваем блокировку send() в tcp_tx_task, чтобы застрявший
        // клиент не держал задачу бесконечно (после таймаута бросаем остаток).
        struct timeval snd_to = { .tv_sec = TX_SNDTIMEO_MS / 1000,
                                  .tv_usec = (TX_SNDTIMEO_MS % 1000) * 1000 };
        if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &snd_to, sizeof(snd_to)) < 0)
            ESP_LOGW(TAG, "SO_SNDTIMEO failed: errno=%d", errno);

        // Н-5.1 (release-gate 1.2.29): клиент, пропавший без FIN (ноутбук уснул,
        // ушёл из зоны), при молчащем приборе держал слот моста вечно — данных нет,
        // повторов нет. Keepalive закрывает его за ~KA_IDLE + KA_INTVL*KA_CNT с.
        int ka = 1, ka_idle = 30, ka_intvl = 10, ka_cnt = 3;
        if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &ka, sizeof(ka)) < 0 ||
            setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &ka_idle, sizeof(ka_idle)) < 0 ||
            setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &ka_intvl, sizeof(ka_intvl)) < 0 ||
            setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &ka_cnt, sizeof(ka_cnt)) < 0)
            ESP_LOGW(TAG, "TCP keepalive setup failed: errno=%d", errno);
        s_client_io_ms = now_ms32();

        // Сбрасываем кольцо от хвоста прошлой сессии ДО публикации fd: producer
        // ещё гейтится s_client_fd<0 и не пишет, так что reset без гонки. Если
        // tx_task сейчас заблокирован в receive (reset вернёт fail) — кольцо и
        // так пусто, новому клиенту хвост не уедет.
        if (s_tx_ring) xStreamBufferReset(s_tx_ring);

        FD_LOCK();
        s_client_fd = fd;
        FD_UNLOCK();
        ESP_LOGI(TAG, "Client connected from %s", inet_ntoa(client_addr.sin_addr));
    }
}

void tcp_bridge_init(void)
{
    s_fd_mutex = xSemaphoreCreateMutex();

    // PSRAM-кольцо + статическая управляющая структура (см. #TCP-1 выше).
    s_tx_ring_storage = heap_caps_malloc(TX_RING_BYTES + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_tx_ring_storage) {
        s_tx_ring = xStreamBufferCreateStatic(TX_RING_BYTES, TX_RING_TRIGGER,
                                              s_tx_ring_storage, &s_tx_ring_struct);
    }
    if (!s_tx_ring) {
        ESP_LOGE(TAG, "TX ring alloc failed — fallback to direct non-blocking send");
    } else {
        ESP_LOGI(TAG, "TX ring %d KB in PSRAM", TX_RING_BYTES / 1024);
    }

    usb_host_cdc_set_raw_rx_cb(usb_to_tcp_cb);
    // #TCP-2: пинимся на core 1. USB-задачи запинены на core 0 (usb_host_cdc.c:
    // usb_lib prio 7, usb_conn prio 2, cdc driver prio 8 — после #FW-8 CDC выше
    // tcp_tx(6), прямое вытеснение приёмника невозможно). Привязку сети к core 1
    // держим: USB-приём на core 0 не делит ядро с сетевыми burst'ами вовсе
    // (кэш/критические секции LWIP), независимо от раскладки приоритетов.
    xTaskCreatePinnedToCore(tcp_server_task, "tcp_srv", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(tcp_tx_task,     "tcp_tx",  4096, NULL, 6, NULL, 1);
    // 5120: запас на Сброс от клиента (spectrum_reset → запись метки во флеш + лог); эта ветка
    // не измерена (нужен Сброс спектра через мост); стек без приёмного буфера (см. tcp_rx_task).
    xTaskCreatePinnedToCore(tcp_rx_task,     "tcp_rx",  5120, NULL, 5, NULL, 1);
    ESP_LOGI(TAG, "TCP bridge initialized, port %d (net tasks pinned core 1)", TCP_BRIDGE_PORT);
}

bool tcp_bridge_client_connected(void)
{
    return s_client_fd >= 0;
}

// Н-2/Н-5.1: клиент подключён И обменивался данными (send/recv) не дольше
// max_idle_ms назад — «работа ПК-программы с платой» для возврата из полевой AP.
bool tcp_bridge_client_active(uint32_t max_idle_ms)
{
    if (s_client_fd < 0) return false;
    return (uint32_t)(now_ms32() - s_client_io_ms) <= max_idle_ms;   // uint32: перенос безопасен
}

uint32_t tcp_bridge_dropped_bytes(void)
{
    return s_bridge_dropped;
}
