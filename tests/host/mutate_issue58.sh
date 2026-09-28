#!/usr/bin/env bash
# issue #58: targeted mutations of spectrum_reset_stat_accept(); each must turn tests red.
set -u
cd "$(dirname "$0")"
H=../../main/spectrum_base_plan.h
cp "$H" /tmp/sbp_backup.h
trap 'cp /tmp/sbp_backup.h "$H"' EXIT
check() {   # header edits are not make deps; build failure must not read as "0 FAIL"
    rm -f test_runner; out=$(make test 2>&1)
    if ! grep -qE 'ALL PASSED|CHECK\(S\) FAILED' <<<"$out"; then echo "== $1: BUILD FAILED"; return; fi
    echo "== $1: $(grep -c '^FAIL' <<<"$out") FAIL"; grep '^FAIL' <<<"$out" | sed 's/^/   /'
}
run() {
    cp /tmp/sbp_backup.h "$H"; sed -i "$2" "$H"
    if cmp -s "$H" /tmp/sbp_backup.h; then echo "== $1: SED DID NOT APPLY"; return; fi
    check "$1"
}
run M1_no_armed_bypass 's/    if (!reset_armed) return true;/    (void)reset_armed;/'
run M2_timeout_gt      's/rejected_for_sec >= SPECTRUM_RESET_CONFIRM_TIMEOUT_S/rejected_for_sec > SPECTRUM_RESET_CONFIRM_TIMEOUT_S/'
run M3_no_gen_check    '/stat_stage_gen != current_reset_gen) return false;/{x;s/^/x/;/^xx$/{x;s/if (.*) return false;/(void)stat_stage_gen; (void)current_reset_gen;/;b};x}'
run M4_never_timeout   's/return rejected_for_sec >= SPECTRUM_RESET_CONFIRM_TIMEOUT_S;/(void)rejected_for_sec; return false;/'
cp /tmp/sbp_backup.h "$H"
check baseline
