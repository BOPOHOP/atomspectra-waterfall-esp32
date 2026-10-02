#!/usr/bin/env bash
#59: мутанты calib_autoread_needed_pref()/calib_request_serial_only() в main/calib_autoread.h — на КОПИИ (-I$T впереди ../../main), исходник не трогаем; каждый мутант обязан
#покраснеть ровно в X, baseline — без FAIL. Образец — mutate_wf_tail.sh.

set -u; cd "$(dirname "$0")"; T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; RC=0; H=../../main/calib_autoread.h
CF="-std=c11 -Wall -Wextra -O2 -I$T -I../../components/shproto/include -I../../main"

mut() {
    local name="$1" sed_expr="$2" expected="$3"
    cp "$H" "$T/calib_autoread.h"
    if [ -n "$sed_expr" ]; then
        sed -i "$sed_expr" "$T/calib_autoread.h"
        if cmp -s "$H" "$T/calib_autoread.h"; then
            echo "== $name: SED DID NOT APPLY"
            RC=1
            return
        fi
    fi
    local out got
    out=$(make -s -B test CFLAGS="$CF" 2>&1)
    if ! grep -qE 'ALL PASSED|CHECK\(S\) FAILED' <<<"$out"; then
        echo "== $name: BUILD/RUN FAILED"
        RC=1
        return
    fi
    got=$(bash fail_funcs.sh <<<"$out")
    echo "== $name: [$got] need [$expected]"
    if [ "$got" != "$expected" ]; then
        RC=1
    fi
}

mut baseline '' ''

mut P1_always_ignored 's/return always || calib_autoread_needed(calib_missing, serial_missing);/return calib_autoread_needed(calib_missing, serial_missing);/' 'test_calib_pref'
mut P2_always_only 's/return always || calib_autoread_needed(calib_missing, serial_missing);/return always;/' 'test_calib_pref'
mut P3_serial_only_old 's/return !calib_missing \&\& !always;/return !calib_missing;/' 'test_calib_pref'
mut P4_apply_never 's/return !calib_missing \&\& !always;/return true;/' 'test_calib_pref'


rm -f test_runner; exit $RC
