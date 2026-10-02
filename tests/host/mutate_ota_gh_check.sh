#!/usr/bin/env bash
#LK-07: мутанты ota_chk_decide() в main/ota_gh_check_state.h — на КОПИИ (-I$T впереди ../../main), исходник не трогаем; каждый мутант обязан
#покраснеть ровно в X, baseline — без FAIL. Образец — mutate_wf_ref.sh.

set -u; cd "$(dirname "$0")"; T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; RC=0; H=../../main/ota_gh_check_state.h
CF="-std=c11 -Wall -Wextra -O2 -I$T -I../../components/shproto/include -I../../main"

mut() {
    local name="$1" sed_expr="$2" expected="$3"
    cp "$H" "$T/ota_gh_check_state.h"
    if [ -n "$sed_expr" ]; then
        sed -i "$sed_expr" "$T/ota_gh_check_state.h"
        if cmp -s "$H" "$T/ota_gh_check_state.h"; then
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


mut C1_running_start 's/if (st == OTA_CHK_RUNNING) return OTA_CHK_ACT_WAIT;//' 'test_ota_gh_check_state'
mut C2_age_le 's/done_age_ms < keep_ms/done_age_ms <= keep_ms/' 'test_ota_gh_check_state'
mut C3_done_always 's/ \&\& done_age_ms < keep_ms//' 'test_ota_gh_check_state'
mut C4_idle_wait 's/return OTA_CHK_ACT_START;$/return OTA_CHK_ACT_WAIT;/' 'test_ota_gh_check_state'

rm -f test_runner; exit $RC
