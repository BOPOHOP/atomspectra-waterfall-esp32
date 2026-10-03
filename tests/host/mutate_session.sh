#!/usr/bin/env bash
# 1.2.31: мутанты main/session_plan.h — на КОПИИ (-I$T впереди ../../main), исходник не трогаем, можно параллельно со сборкой; каждый мутант обязан покраснеть ровно в названных тест-функциях, baseline — без FAIL
#Без -Werror: мутант, выключивший проверку, оставляет параметр неиспользованным — это не должно ронять сборку.

set -u; cd "$(dirname "$0")"; T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; RC=0; H=../../main/session_plan.h
CF="-std=c11 -Wall -Wextra -O2 -I$T -I../../components/shproto/include -I../../main"

mut() {
    local name="$1" sed_expr="$2" expected="$3"
    cp "$H" "$T/session_plan.h"
    if [ -n "$sed_expr" ]; then
        sed -i "$sed_expr" "$T/session_plan.h"
        if cmp -s "$H" "$T/session_plan.h"; then
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
mut S1_empty_opens 's/return valid || total_time_sec != 0;/return true;/' 'test_reset_empty_no_session test_scenario_operator'
mut S2_data_closed 's/return valid || total_time_sec != 0;/return valid;/' 'test_reset_opens_on_data'
mut S3_sess0 's/return s->sess != 0 \&\& req_now/return req_now/' 'test_need_bump_disabled'
mut S4_no_consume 's/    s->seen_req = req_now; /    (void)req_now; /' 'test_apply_failure_keeps test_apply_success test_scenario_operator'
mut S5_overwrite 's/if (new_sess <= s->sess) return;/if (0) return;/' 'test_apply_failure_keeps test_apply_not_monotonic'
mut S6_seq_kept 's/s->seq = 0; //' 'test_apply_success test_scenario_operator'
mut S7_snap_always 's/{ return req_at_snap == req_expected; }/{ (void)req_at_snap; (void)req_expected; return true; }/' 'test_snap_stale'

rm -f test_runner; exit $RC
