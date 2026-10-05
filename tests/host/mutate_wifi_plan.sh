#!/bin/sh
# #AWF-F1: мутационная приёмка wifi_reconnect_plan.h (wifi_setup_should_return + порог 300 с). Исходник восстанавливается.
# Ждём: годный = 0 FAIL; каждый мутант красит тест (lines = номера CHECK в test_wifi_reconnect_plan.c).
H=../../main/wifi_reconnect_plan.h
# Защита от kill -9: маркер /tmp/wrp.running живёт только во время прогона; если он остался от убитого
# прогона, рабочий заголовок мог остаться мутантом -> сначала восстановить из /tmp/wrp.orig (чистая копия).
if [ -f /tmp/wrp.running ] && [ -f /tmp/wrp.orig ]; then
  if cmp -s /tmp/wrp.orig "$H"; then echo "prev run was killed, header already clean"
  else cp /tmp/wrp.orig "$H"; echo "prev run was killed: header restored from /tmp/wrp.orig"; fi
fi
cp "$H" /tmp/wrp.orig; : > /tmp/wrp.running
trap 'cp /tmp/wrp.orig "$H"; rm -f /tmp/wrp.running' EXIT INT TERM   # восстановить заголовок при обрыве
printf 'int g_failures;\nvoid test_wifi_reconnect_plan(void);\nint main(void){test_wifi_reconnect_plan();return g_failures!=0;}\n' > /tmp/wrp_main.c
build() { gcc -std=c11 -Wall -Wextra -Werror -O2 -I../../main -o /tmp/wrp_m /tmp/wrp_main.c test_wifi_reconnect_plan.c 2>/dev/null; }
run() { # $1=имя $2=sed-выражение
  cp /tmp/wrp.orig "$H"; sed -i "$2" "$H"
  if cmp -s "$H" /tmp/wrp.orig; then echo "$1: SED NOOP"; return; fi
  build || { echo "$1: COMPILE-ERR"; return; }
  out=$(/tmp/wrp_m); rc=$?
  echo "$1: rc=$rc fails=$(echo "$out" | grep -c '^FAIL') lines=$(echo "$out" | sed -n 's/^FAIL [^:]*:\([0-9]*\):.*/\1/p' | tr '\n' ',')"
}
cp /tmp/wrp.orig "$H"; build && /tmp/wrp_m; echo "good rc=$?"
run M1_fails_gt5 's/fails >= WIFI_SETUP_VERIFY_FAILS/fails > WIFI_SETUP_VERIFY_FAILS/'
run M2_fails_ge4 's/fails >= WIFI_SETUP_VERIFY_FAILS/fails >= WIFI_SETUP_VERIFY_FAILS - 1/'
run M3_min_gt15 's/elapsed_s >= WIFI_SETUP_VERIFY_MIN_S/elapsed_s > WIFI_SETUP_VERIFY_MIN_S/'
run M4_min_ge14 's/elapsed_s >= WIFI_SETUP_VERIFY_MIN_S/elapsed_s >= WIFI_SETUP_VERIFY_MIN_S - 1u/'
run M5_no_unver_flag 's/return unverified \&\&/return (unverified || 1) \&\&/'
run M6_no_gotip 's/!got_ip_this_boot \&\&/(got_ip_this_boot || 1) \&\&/'
run M7_invert_unver 's/return unverified \&\&/return !unverified \&\&/'
run M8_and_to_or 's/fails >= WIFI_SETUP_VERIFY_FAILS \&\&/(fails >= WIFI_SETUP_VERIFY_FAILS ||/;s/elapsed_s >= WIFI_SETUP_VERIFY_MIN_S;/elapsed_s >= WIFI_SETUP_VERIFY_MIN_S);/'
run M12_reason_201_bad 's/reason == 15 || reason == 202/reason == 15 || reason == 201 || reason == 202/'
run M13_reason_no15 's/reason == 15 || reason == 202/reason == 202/'
run M14_bump_always 's/? fails + 1 : 0/? fails + 1 : fails + 1/'
run M15_bump_no_reset 's/? fails + 1 : 0/? fails + 1 : fails/'
run M16_bump_no_inc 's/? fails + 1 : 0/? fails : 0/'
run M17_pass_max_63 's/len >= 8 \&\& len <= 64/len >= 8 \&\& len <= 63/'
run M18_pass_min_7 's/len >= 8 \&\& len <= 64/len >= 7 \&\& len <= 64/'
run M19_pass_no_empty 's/len == 0 || (len >= 8/(len >= 8/'
run M20_ssid_max_31 's/len >= 1 \&\& len <= 32/len >= 1 \&\& len <= 31/'
run M21_ssid_allow_empty 's/len >= 1 \&\& len <= 32/len <= 32/'
run M22_reason_203_bad 's/reason == 15 || reason == 202/reason == 15 || reason == 203 || reason == 202/'
run M23_reason_14_bad 's/reason == 15 || reason == 202/reason == 15 || reason == 14 || reason == 202/'
run M24_pass64_hex_not_required 's/return n != 64 || hex;/return n != 64 || hex || 1;/'
run M25_pass64_always_bad 's/return n != 64 || hex;/return n != 64 || (hex \&\& 0);/'
run M26_pass_no_printable 's/c < 0x20 || c > 0x7e/0/'
run M27_hex_no_lower_f 's/c <= .f.)/c <= 0x65)/'
run M28_hex_no_upper_F 's/c <= .F.)/c <= 0x45)/'
run M29_hex_no_digit9 's/c <= .9.)/c <= 0x38)/'
run M30_pass_len_dropped 's/if (!wifi_pass_len_ok(n)) return false;//'
run M9_fb_gt300 's/elapsed_disconnected_s >= WIFI_RECONNECT_FALLBACK_S/elapsed_disconnected_s > WIFI_RECONNECT_FALLBACK_S/'
run M10_fb_const_301 's/FALLBACK_S 300u/FALLBACK_S 301u/'
run M11_fb_const_299 's/FALLBACK_S 300u/FALLBACK_S 299u/'
cp /tmp/wrp.orig "$H"; cmp "$H" /tmp/wrp.orig && echo restored
