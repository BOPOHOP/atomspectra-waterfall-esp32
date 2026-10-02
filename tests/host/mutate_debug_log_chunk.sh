#!/usr/bin/env bash
#WP5: мутанты dbglog_chunk_plan() в main/debug_log_chunk_plan.h — на КОПИИ (-I$T впереди ../../main), исходник не трогаем; каждый мутант обязан
#покраснеть ровно в X, baseline — без FAIL. Образец — mutate_wf_ref.sh.

set -u; cd "$(dirname "$0")"; T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; RC=0; H=../../main/debug_log_chunk_plan.h
CF="-std=c11 -Wall -Wextra -O2 -I$T -I../../components/shproto/include -I../../main"

mut() {
    local name="$1" sed_expr="$2" expected="$3"
    cp "$H" "$T/debug_log_chunk_plan.h"
    if [ -n "$sed_expr" ]; then
        sed -i "$sed_expr" "$T/debug_log_chunk_plan.h"
        if cmp -s "$H" "$T/debug_log_chunk_plan.h"; then
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



mut D1_overwrite_le 's/if (want_abs < oldest)/if (want_abs <= oldest)/' 'test_debug_log_chunk'
mut D2_no_clamp 's/    if (n > chunk_max) n = chunk_max;//' 'test_debug_log_chunk'
mut D3_done_gt 's/if (want_abs >= end_abs)/if (want_abs > end_abs)/' 'test_debug_log_chunk'
mut D4_off_abs 's/\*off = (size_t)(want_abs - oldest);/*off = (size_t)want_abs;/' 'test_debug_log_chunk'

rm -f test_runner; exit $RC
