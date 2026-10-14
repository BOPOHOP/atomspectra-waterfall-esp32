#!/usr/bin/env bash
# 1.2.36 (P1): защита сборки от устаревшего sdkconfig (main/socket_budget.h): 16/16 -> #error, 24/24 -> успех.
# Мутации (#SA-3): mutate_socket_budget.sh.
set -u; cd "$(dirname "$0")"
H=${SB_HEADER:-../../main/socket_budget.h}; RC=0
chk() {   # chk <имя> <ok|err> <SOCKETS> <TCP>
    if out=$(gcc -fsyntax-only -x c -DCONFIG_LWIP_MAX_SOCKETS="$3" -DCONFIG_LWIP_MAX_ACTIVE_TCP="$4" "$H" 2>&1); then got=ok
    elif echo "$out" | grep -q 'error: #error'; then got=err; else got="сбой gcc: $out"; fi   # err — только от #error, не от отсутствия gcc
    [ "$got" = "$2" ] && echo "ok   $1" || { echo "FAIL $1 (ждали $2, получили $got)"; RC=1; }
}
chk sockets16_tcp16_rejected err 16 16
chk sockets16_tcp24_rejected err 16 24
chk sockets24_tcp16_rejected err 24 16
chk sockets23_tcp24_rejected err 23 24
chk sockets24_tcp24_ok       ok  24 24
chk sockets32_tcp32_ok       ok  32 32
if gcc -fsyntax-only -x c "$H" 2>&1 | grep -q 'error: #error'; then echo "ok   undefined_rejected"; else echo "FAIL undefined_rejected"; RC=1; fi
[ $RC -eq 0 ] && echo "ALL PASSED"; exit $RC
