#!/usr/bin/env bash
# 1.2.36 (P1, #SA-3): мутанты main/socket_budget.h; baseline 0 FAIL, каждый мутант краснит ровно N строк FAIL.
set -u; cd "$(dirname "$0")"; T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; RC=0
mut() {   # mut <имя> <sed|-> <ожидаемо FAIL>
    cp ../../main/socket_budget.h "$T/h.h"
    if [ "$2" != - ]; then sed -i "$2" "$T/h.h"; cmp -s ../../main/socket_budget.h "$T/h.h" && { echo "== $1: SED DID NOT APPLY"; RC=1; return; }; fi
    local n; n=$(SB_HEADER="$T/h.h" bash ./socket_budget_check.sh 2>&1 | grep -c '^FAIL ')
    echo "== $1: $n FAIL (need $3)"; [ "$n" -eq "$3" ] || RC=1
}
mut baseline -  0
mut B1_no_sockets_guard 's/^#if CONFIG_LWIP_MAX_SOCKETS < SOCKETS_REQUIRED/#if 0/' 2
mut B2_no_tcp_guard     's/^#if CONFIG_LWIP_MAX_ACTIVE_TCP < SOCKETS_REQUIRED/#if 0/' 1
mut B3_weak_threshold   's/(HTTPD_MAX_OPEN_SOCKETS + HTTPD_INTERNAL_SOCKETS + TCP_BRIDGE_SOCKETS)/(HTTPD_MAX_OPEN_SOCKETS)/' 1
mut B4_internal_zero    's/HTTPD_INTERNAL_SOCKETS    3/HTTPD_INTERNAL_SOCKETS    0/' 1
exit $RC
