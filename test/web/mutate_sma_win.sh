#!/bin/sh
# 1.2.36: мутационная приёмка (#SA-3) sma_win_test.mjs на КОПИИ web/index.html (репо не меняется). Запуск из корня репо.
# Ждём: baseline 0 FAIL; каждый мутант краснит ровно перечисленные проверки.
T=${TMPDIR:-/tmp}/sma_win_mut.html; RC=0
names() { node test/web/sma_win_test.mjs "$T" 2>&1 | grep '^FAIL ' | awk '{print $2}' | sort -u | tr '\n' ' '; }
m() { n=$1; want=$2; shift 2
  sed "$@" web/index.html > "$T"
  if cmp -s "$T" web/index.html; then echo "$n: SED NOOP"; RC=1; return; fi
  got=$(names); [ "$got" = "$want " ] && v=OK || { v=BAD; RC=1; }
  echo "== $n: tests=[$got] want=[$want] $v"; }
cp web/index.html "$T"; echo "== baseline: fails=$(node test/web/sma_win_test.mjs "$T" | grep -c '^FAIL')"
m MW1_multiplier "sma_win_default sma_win_int sma_win_iv sma_win_no_multiplier" -e 's/catch(e){}return iv}/catch(e){}return iv*10}/'
m MW2_label_back "sma_no_label sma_text_only_number" -e 's/" \/ "+sma.toFixed(2)/" \/ SMA "+W+"s "+sma.toFixed(2)/'
m MW3_default60 sma_win_default -e 's/function smaWinSec(){var iv=10;/function smaWinSec(){var iv=60;/'
m MW4_no_clamp sma_win_int -e 's/Math.min(Math.floor(st.iv),86400)/st.iv/'
m MW5_old_key sma_no_label -e 's/var iv=10;try{/var iv=10;try{localStorage.getItem("aswf-sma-win");/'
exit $RC
