#!/bin/bash
# #AWF-WIFI-1: мутанты проводки wifi_fallback_timeout_s в КОПИИ wifi_manager.c; ловит wiring_check.sh.
# Классы: источник значения (W1-W3, M1), порядок (M2, M4), охрана гонки (M5, M6), комментарий вместо кода (W4).
# Ждём: годная копия -> wiring: OK; каждый мутант -> хотя бы одна строка WIRING FAIL (число печатается).
R=/tmp/wst_root; W=$R/main; rm -rf $R; mkdir -p $W; cp -r ../../main/. $W/; cp -r ../../web $R/web; cp -r ../../scripts $R/scripts
SRC=../../main/wifi_manager.c
RD='s_unverified = (unver != 0);'; ST='esp_timer_start_once(s_fallback_timer, (uint64_t)wifi_fallback_timeout_s(s_unverified) * 1000000);'
SP='if (s_fallback_timer) esp_timer_stop(s_fallback_timer);'; UF='s_unverified = false;'
chk() { # $1=имя; $2=0 - файл не менялся (годный)
  if [ "${2:-1}" = 1 ] && cmp -s $SRC $W/wifi_manager.c; then echo "$1: NOOP"; return; fi
  r=$(bash ./wiring_check.sh $W 2>&1 | grep -c 'WIRING FAIL'); echo "$1: wiring FAIL lines=$r"
}
sedm() { cp $SRC $W/wifi_manager.c; sed -i "$2" $W/wifi_manager.c; chk "$1"; }
awkm() { cp $SRC $W/wifi_manager.c; awk -v mv="$2" -v after="$3" 'function t(x){c=x;sub(/\r$/,"",c);sub(/^[ \t]+/,"",c);return c}
  !dm && t($0)==mv {held=$0; dm=1; next} {print} held!="" && t($0)==after && !pr {print held; pr=1}' $SRC > $W/wifi_manager.c; chk "$1"; }
cp $SRC $W/wifi_manager.c; chk GOOD 0
sedm W1_timer_old_const 's/(uint64_t)wifi_fallback_timeout_s(s_unverified) \* 1000000/(uint64_t)WIFI_RECONNECT_FALLBACK_S * 1000000/'
sedm W2_timer_flag_false 's/(uint64_t)wifi_fallback_timeout_s(s_unverified) \* 1000000/(uint64_t)wifi_fallback_timeout_s(false) * 1000000/'
sedm W3_log_old_const 's/(unsigned)wifi_fallback_timeout_s(s_unverified), (int)s_unverified/(unsigned)WIFI_RECONNECT_FALLBACK_S, (int)s_unverified/'
sedm W4_comment_instead_of_call 's/esp_timer_start_once(s_fallback_timer, (uint64_t)wifi_fallback_timeout_s(s_unverified) \* 1000000);/esp_timer_start_once(s_fallback_timer, (uint64_t)WIFI_SETUP_FALLBACK_S * 1000000); \/* wifi_fallback_timeout_s(s_unverified) *\//'
sedm M1_no_nvs_read 's/s_unverified = (unver != 0);/s_unverified = false;/'
awkm M2_read_after_timer "$RD" "$ST"
awkm M4_clear_before_stop "$SP" "$UF"
sedm M5_no_timer_stop 's/if (s_fallback_timer) esp_timer_stop(s_fallback_timer);/(void)0;/'
sedm M6_no_cb_guard 's/if (s_got_ip_this_boot) return;   \/\/ #AWF-WIFI-1/(void)0;   \/\/ #AWF-WIFI-1/'
rm -rf $R
