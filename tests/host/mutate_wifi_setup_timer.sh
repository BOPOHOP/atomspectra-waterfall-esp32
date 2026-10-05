#!/bin/sh
# #AWF-WIFI-1: мутационная приёмка wifi_fallback_timeout_s (90 с непроверенная / 300 с подтверждённая).
# Ждём: годный = 0 FAIL; каждый мутант красит тест (lines = номера CHECK в test_wifi_reconnect_plan.c).
# Проводка в wifi_manager.c - mutate_wifi_setup_wiring.sh.
H=../../main/wifi_reconnect_plan.h
O=/tmp/wst.orig
if [ -f /tmp/wst.running ] && [ -f $O ]; then cp $O "$H"; echo "prev run killed: header restored"; fi
cp "$H" $O; : > /tmp/wst.running
trap 'cp $O "$H"; rm -f /tmp/wst.running' EXIT INT TERM
printf 'int g_failures;\nvoid test_wifi_fallback_timeout(void);\nint main(void){test_wifi_fallback_timeout();return g_failures!=0;}\n' > /tmp/wst_main.c
build() { gcc -std=c11 -Wall -Wextra -Werror -O2 -I../../main -o /tmp/wst_m /tmp/wst_main.c test_wifi_reconnect_plan.c 2>/dev/null; }
run() { # $1=имя $2=sed-выражение
  cp $O "$H"; sed -i "$2" "$H"
  if cmp -s "$H" $O; then echo "$1: SED NOOP"; return; fi
  build || { echo "$1: COMPILE-ERR"; return; }
  out=$(/tmp/wst_m); rc=$?
  echo "$1: rc=$rc fails=$(echo "$out" | grep -c '^FAIL') lines=$(echo "$out" | sed -n 's/^FAIL [^:]*:\([0-9]*\):.*/\1/p' | tr '\n' ',')"
}
cp $O "$H"; build && /tmp/wst_m; echo "good rc=$?"
run G_gross_body_zero 's/return unverified ? WIFI_SETUP_FALLBACK_S : WIFI_RECONNECT_FALLBACK_S;/return (uint32_t)unverified;/'
run P1_setup_91 's/WIFI_SETUP_FALLBACK_S 90u/WIFI_SETUP_FALLBACK_S 91u/'
run P2_setup_89 's/WIFI_SETUP_FALLBACK_S 90u/WIFI_SETUP_FALLBACK_S 89u/'
run P3_setup_300 's/WIFI_SETUP_FALLBACK_S 90u/WIFI_SETUP_FALLBACK_S 300u/'
run P4_invert_flag 's/return unverified ?/return !unverified ?/'
run P5_single_term_300 's/return unverified ? WIFI_SETUP_FALLBACK_S :/return unverified ? WIFI_RECONNECT_FALLBACK_S :/'
run P6_single_term_90 's/: WIFI_RECONNECT_FALLBACK_S;/: WIFI_SETUP_FALLBACK_S;/'
run P7_known_301 's/FALLBACK_S 300u/FALLBACK_S 301u/'
run P8_known_299 's/FALLBACK_S 300u/FALLBACK_S 299u/'
cp $O "$H"; cmp "$H" $O && echo header_restored
