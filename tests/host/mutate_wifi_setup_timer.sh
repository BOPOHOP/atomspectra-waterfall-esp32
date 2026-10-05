#!/bin/sh
# #AWF-WIFI-1: мутационная приёмка wifi_fallback_timeout_s (120 с непроверенная / 300 с подтверждённая).
# Мутации - на КОПИИ заголовка в /tmp/wst_inc (реальный main/wifi_reconnect_plan.h не трогается, проход B W-5).
# Ждём: годный = 0 FAIL; каждый мутант красит тест (lines = номера CHECK в test_wifi_reconnect_plan.c)
# либо COMPILE-ERR (сработал _Static_assert). Проводка в wifi_manager.c - mutate_wifi_setup_wiring.sh.
D=/tmp/wst_inc; O=../../main/wifi_reconnect_plan.h
rm -rf $D; mkdir -p $D; H=$D/wifi_reconnect_plan.h
printf 'int g_failures;\nvoid test_wifi_fallback_timeout(void);\nint main(void){test_wifi_fallback_timeout();return g_failures!=0;}\n' > $D/main.c
build() { gcc -std=c11 -Wall -Wextra -Werror -O2 -I$D -I../../main -o $D/m $D/main.c test_wifi_reconnect_plan.c 2>/dev/null; }
run() { # $1=имя $2=sed-выражение
  cp $O "$H"; sed -i "$2" "$H"
  if cmp -s "$H" $O; then echo "$1: SED NOOP"; return; fi
  build || { echo "$1: COMPILE-ERR"; return; }
  out=$($D/m); rc=$?
  echo "$1: rc=$rc fails=$(echo "$out" | grep -c '^FAIL') lines=$(echo "$out" | sed -n 's/^FAIL [^:]*:\([0-9]*\):.*/\1/p' | tr '\n' ',')"
}
cp $O "$H"; build && $D/m; echo "good rc=$?"
run G_gross_body 's/return unverified ? WIFI_SETUP_FALLBACK_S : WIFI_RECONNECT_FALLBACK_S;/return (uint32_t)unverified;/'
run P1_margin_11 's/WIFI_SETUP_MARGIN_S 12u/WIFI_SETUP_MARGIN_S 11u/'
run P2_margin_13 's/WIFI_SETUP_MARGIN_S 12u/WIFI_SETUP_MARGIN_S 13u/'
run S1_margin_0_static 's/WIFI_SETUP_MARGIN_S 12u/WIFI_SETUP_MARGIN_S 0u/'
run S2_old_90_static 's/^#define WIFI_SETUP_FALLBACK_S .*/#define WIFI_SETUP_FALLBACK_S 90u/'
run S3_old_90_noassert 's/^#define WIFI_SETUP_FALLBACK_S .*/#define WIFI_SETUP_FALLBACK_S 90u/;/^_Static_assert/d'
run P4_invert_flag 's/return unverified ?/return !unverified ?/'
run P5_single_term_300 's/return unverified ? WIFI_SETUP_FALLBACK_S :/return unverified ? WIFI_RECONNECT_FALLBACK_S :/'
run P6_single_term_setup 's/: WIFI_RECONNECT_FALLBACK_S;/: WIFI_SETUP_FALLBACK_S;/'
run P7_known_301 's/FALLBACK_S 300u/FALLBACK_S 301u/'
run P8_known_299 's/FALLBACK_S 300u/FALLBACK_S 299u/'
run P9_array_60_to_61 's/{ 1, 2, 5, 10, 30, 60 }/{ 1, 2, 5, 10, 30, 61 }/'
run P10_sum_107 's/SCHEDULE_SUM_S 108u/SCHEDULE_SUM_S 107u/'
echo "real header modified by script: no (copy in $D)"; rm -rf $D
