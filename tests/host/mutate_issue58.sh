#!/usr/bin/env bash
# issue #58 / U-1: targeted mutations of spectrum_reset_gate_step(); each must turn
# exactly the test named in its comment red; baseline must be ALL PASSED.
set -u
cd "$(dirname "$0")"
H=../../main/spectrum_base_plan.h
B=$(mktemp); cp "$H" "$B"
trap 'cp "$B" "$H"; rm -f "$B"' EXIT
check() {   # build failure must not read as "0 FAIL"
    rm -f test_runner; out=$(make test 2>&1)
    if ! grep -qE 'ALL PASSED|CHECK\(S\) FAILED' <<<"$out"; then echo "== $1: BUILD FAILED"; return; fi
    echo "== $1: $(grep -c '^FAIL' <<<"$out") FAIL"; grep '^FAIL' <<<"$out" | sed 's/^/   /'
}
run() {
    cp "$B" "$H"; sed -i "$2" "$H"
    if cmp -s "$H" "$B"; then echo "== $1: SED DID NOT APPLY"; return; fi
    check "$1"
}
run M1_no_armed_bypass  's/if (!g->armed) {/if (0) {/'                                   # boot_without_reset
run M2_no_seq_check     's/if (stat_seq == g->reject_first_seq) {/if (0) {/'             # u1_timeout_needs_new_stat
run M3_no_gen_check     's/if (stat_gen != current_gen) {/if (0) {/'                     # u1_old_gen_never_starts_timer
run M4_timeout_le       's/\/ 1000000 < SPECTRUM_RESET/\/ 1000000 <= SPECTRUM_RESET/'    # u1_timeout_needs_new_stat
run M5_no_plausible     's/if (spectrum_reset_stat_is_plausible_gen(/if (0 \&\& spectrum_reset_stat_is_plausible_gen(/'  # awf4_race
run M6_no_by_timeout    's/\*by_timeout = true;/(void)by_timeout;/'                      # u1_timeout_needs_new_stat
run M7_timer_restarts   's/if (g->reject_since_us == 0) {/if (1) {/'                     # u1_timeout_needs_new_stat
cp "$B" "$H"
check baseline
