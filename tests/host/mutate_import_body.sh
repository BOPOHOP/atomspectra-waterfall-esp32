#!/bin/sh
# F9 (проход 3A): мутационная проверка import_body_plan.h. Работает на КОПИИ (оригинал не трогается),
# trap убирает копию. Исправный: 0 FAIL; каждый мутант M1-M4 должен покраснеть ровно на ОДНОМ CHECK.
# E1 (удаление `if (buf_cap == 0) return 0;`) - ЭКВИВАЛЕНТНЫЙ мутант: при buf_cap==0 тернарник
# `(remaining < 0) ? remaining : 0` тоже даёт 0 (size_t), поведение не меняется; ожидается fails=0.
cd "$(dirname "$0")" || exit 2
W=$(mktemp -d /tmp/imb.XXXXXX)
trap 'rm -rf "$W"' EXIT INT TERM
mkdir -p "$W/main" "$W/tests/host"
cp ../../main/import_body_plan.h "$W/main/"
cp test_import_body_plan.c "$W/tests/host/"
H="$W/main/import_body_plan.h"; cp "$H" "$W/orig.h"
build() { (cd "$W/tests/host" && gcc -std=c11 -Wall -Wextra -Werror -O2 -o "$W/t" test_import_body_plan.c); }
run() { # $1=имя $2=sed-выражение
  cp "$W/orig.h" "$H"; sed -i "$2" "$H"
  if cmp -s "$H" "$W/orig.h"; then echo "$1: SED NOOP"; return; fi
  build 2>/dev/null || { echo "$1: COMPILE-ERR"; return; }
  out=$("$W/t"); echo "$1: fails=$(echo "$out" | grep -c '^FAIL')"
}
build && "$W/t"; echo "good rc=$?"
run M1_drain_lt 's/content_len <= IMPORT_DRAIN_MAX/content_len < IMPORT_DRAIN_MAX/'
run M2_deadline_gt 's/>= (int64_t)IMPORT_RECV_DEADLINE_MS/> (int64_t)IMPORT_RECV_DEADLINE_MS/'
run M3_deadline_units 's/IMPORT_RECV_DEADLINE_MS \* 1000/IMPORT_RECV_DEADLINE_MS * 100/'
run M4_clock_back_true '/now_us < start_us/{n;s/false/true/}'
run E1_bufcap0_EQUIV '/if (buf_cap == 0) {/,/^    }/d'
echo "original untouched: $(cmp ../../main/import_body_plan.h "$W/orig.h" && echo yes)"
