// 1.2.36 (release-gate pass2, P1): бюджет сокетов lwIP, проверяемый при СБОРКЕ.
// sdkconfig.defaults не действует на уже существующий sdkconfig (16/16): прошивка собралась бы с
// max_open_sockets=19 на пуле 16 (httpd_start отказывает либо пул исчерпан, errno 23).
#ifndef SOCKET_BUDGET_H
#define SOCKET_BUDGET_H
#if __has_include("sdkconfig.h")
#  include "sdkconfig.h"
#endif
#define HTTPD_MAX_OPEN_SOCKETS   19   // config.max_open_sockets (web_server.c)
#define HTTPD_INTERNAL_SOCKETS    3   // IDF httpd: max_open_sockets <= LWIP_MAX_SOCKETS - 3
#define TCP_BRIDGE_SOCKETS        2   // tcp_bridge.c: слушающий + один клиент (listen(fd, 1))
#define SOCKETS_REQUIRED (HTTPD_MAX_OPEN_SOCKETS + HTTPD_INTERNAL_SOCKETS + TCP_BRIDGE_SOCKETS)   // 24
#if !defined(CONFIG_LWIP_MAX_SOCKETS) || !defined(CONFIG_LWIP_MAX_ACTIVE_TCP)
#  error "CONFIG_LWIP_MAX_SOCKETS/CONFIG_LWIP_MAX_ACTIVE_TCP undefined: sdkconfig.h is required"
#endif
#if CONFIG_LWIP_MAX_SOCKETS < SOCKETS_REQUIRED
#  error "CONFIG_LWIP_MAX_SOCKETS < 24: stale sdkconfig. Delete sdkconfig and build/sdkconfig, then run idf.py reconfigure"
#endif
#if CONFIG_LWIP_MAX_ACTIVE_TCP < SOCKETS_REQUIRED
#  error "CONFIG_LWIP_MAX_ACTIVE_TCP < 24: stale sdkconfig. Delete sdkconfig and build/sdkconfig, then run idf.py reconfigure"
#endif
#endif
