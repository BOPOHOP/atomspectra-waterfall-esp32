#!/bin/sh
# 1.2.36 #MX-14 (G): мутационная приёмка (#SA-3) sma_ratio_test.mjs на КОПИИ web/common-time.js (репо не меняется).
# Запуск из корня репо: sh test/web/mutate_sma_ratio.sh. Ждём: годный - 0 FAIL; MG4 краснит РОВНО sma_ratio_def; MG0 - три теста.
T=${TMPDIR:-/tmp}/sma_mut.js
names() { node test/web/sma_ratio_test.mjs "$T" 2>&1 | grep '^FAIL' | awk '{print $2}' | sort -u | tr '\n' ' '; }
m() { # m <имя> <ожидаемые тесты> <sed -e ...>
  n=$1; want=$2; shift 2
  sed "$@" web/common-time.js > "$T"
  if cmp -s "$T" web/common-time.js; then echo "$n: SED NOOP"; return 1; fi
  got=$(names); [ "$got" = "$want " ] && v=OK || v=BAD
  echo "== $n: tests=[$got] want=[$want] $v"
}
cp web/common-time.js "$T"; echo "== baseline: fails=$(node test/web/sma_ratio_test.mjs "$T" | grep -c '^FAIL')"
# MG4: вернуть среднее отношений вёдер (прежнее определение «Мониторинга»): sc копит counts/dur, sd считает вёдра
m MG4_mean_of_ratios sma_ratio_def -e 's/sc += counts\[i\]; sd += durs\[i\];/sc += counts[i] \/ durs[i]; sd += 1;/' -e 's/sc -= counts\[i - win\]; sd -= durs\[i - win\];/sc -= counts[i - win] \/ durs[i - win]; sd -= 1;/'
# MG0 (грубый): значение не считается
m MG0_const_zero "sma_ratio_bucket_close sma_ratio_def sma_ratio_edges" -e 's/out.push(sd > 0 ? sc \/ sd : 0);/out.push(0);/'
