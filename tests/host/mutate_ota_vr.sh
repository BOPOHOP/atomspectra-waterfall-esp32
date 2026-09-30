#!/usr/bin/env bash
#OTA-VR: мутанты ota_boot_retry_pure() в main/ota_busy.h — на КОПИИ (-I$T впереди ../../main), исходник не трогаем; каждый мутант обязан
#покраснеть ровно в test_boot_retry_policy, baseline — без FAIL. Образец — mutate_wf_ref.sh.

set -u; cd "$(dirname "$0")"; T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; RC=0; H=../../main/ota_busy.h
CF="-std=c11 -Wall -Wextra -O2 -I$T -I../../components/shproto/include -I../../main"

mut() {
    local name="$1" sed_expr="$2" expected="$3"
    cp "$H" "$T/ota_busy.h"
    if [ -n "$sed_expr" ]; then
        sed -i "$sed_expr" "$T/ota_busy.h"
        if cmp -s "$H" "$T/ota_busy.h"; then
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
mut V1_no_break '/if (err != validate_failed) break;/d' 'test_boot_retry_policy'
mut V2_no_retry 's/\*calls < tries; )/*calls < 1; )/' 'test_boot_retry_policy'
mut V3_retry_any 's/if (err != validate_failed) break;/if (err == 0) break;/' 'test_boot_retry_policy'
mut V4_calls_lost 's/        (\*calls)++;/        ;/' 'test_boot_retry_policy'

rm -f test_runner; exit $RC
