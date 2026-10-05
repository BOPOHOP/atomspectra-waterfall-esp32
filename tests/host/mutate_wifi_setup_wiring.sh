#!/bin/sh
# #AWF-WIFI-1: мутанты проводки wifi_fallback_timeout_s в КОПИИ wifi_manager.c; ловит wiring_check.sh.
# Ждём: годная копия -> wiring: OK; каждый мутант -> хотя бы одна строка WIRING FAIL.
R=/tmp/wst_root; W=$R/main; rm -rf $R; mkdir -p $W; cp -r ../../main/. $W/; cp -r ../../web $R/web; cp -r ../../scripts $R/scripts
wire() { # $1=имя $2=sed-выражение
  cp ../../main/wifi_manager.c $W/wifi_manager.c; sed -i "$2" $W/wifi_manager.c
  if cmp -s ../../main/wifi_manager.c $W/wifi_manager.c; then echo "$1: SED NOOP"; return; fi
  r=$(bash ./wiring_check.sh $W 2>&1 | grep -c 'WIRING FAIL'); echo "$1: wiring FAIL lines=$r"
}
cp ../../main/wifi_manager.c $W/wifi_manager.c; bash ./wiring_check.sh $W | tail -1
wire W1_timer_old_const 's/(uint64_t)wifi_fallback_timeout_s(s_unverified) \* 1000000/(uint64_t)WIFI_RECONNECT_FALLBACK_S * 1000000/'
wire W2_timer_flag_false 's/(uint64_t)wifi_fallback_timeout_s(s_unverified) \* 1000000/(uint64_t)wifi_fallback_timeout_s(false) * 1000000/'
wire W3_log_old_const 's/(unsigned)wifi_fallback_timeout_s(s_unverified), (int)s_unverified/(unsigned)WIFI_RECONNECT_FALLBACK_S, (int)s_unverified/'
rm -rf $R
