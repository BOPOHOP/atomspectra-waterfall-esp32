#!/usr/bin/env bash
#RST-TAIL: мутанты wf_tail_should_row() в main/wf_tail_plan.h — на КОПИИ (-I$T впереди ../../main), исходник не трогаем; каждый мутант обязан
#покраснеть ровно в X, baseline — без FAIL. Образец — mutate_wf_ref.sh.

set -u; cd "$(dirname "$0")"; T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; RC=0; H=../../main/wf_tail_plan.h
CF="-std=c11 -Wall -Wextra -O2 -I$T -I../../components/shproto/include -I../../main"

mut() {
    local name="$1" sed_expr="$2" expected="$3"
    cp "$H" "$T/wf_tail_plan.h"
    if [ -n "$sed_expr" ]; then
        sed -i "$sed_expr" "$T/wf_tail_plan.h"
        if cmp -s "$H" "$T/wf_tail_plan.h"; then
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

mut T1_force_zero 's/if (force) return now_time > prev_time;/if (force) return now_time >= prev_time;/' 'test_tail_forced'
mut T2_force_always 's/if (force) return now_time > prev_time;/if (force) return true;/' 'test_tail_forced'
mut T3_force_ignored 's/    if (force) return now_time > prev_time;//' 'test_tail_forced'
mut T4_normal_early 's/now_time - prev_time < iv)/now_time - prev_time <= iv)/' 'test_tail_normal_tick'
mut T5_normal_nowrap 's/return !(now_time >= prev_time \&\& now_time - prev_time < iv);/return now_time >= prev_time \&\& now_time - prev_time >= iv;/' 'test_tail_normal_tick'

rm -f test_runner; exit $RC
