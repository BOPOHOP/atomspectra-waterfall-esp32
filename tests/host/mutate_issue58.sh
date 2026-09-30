#!/usr/bin/env bash
# issue #58 / U-1: targeted mutations of spectrum_reset_gate_step(); each must turn
# exactly the test named in its comment red; baseline must be ALL PASSED.
set -u
cd "$(dirname "$0")"
H=../../main/spectrum_base_plan.h
B=$(mktemp); cp "$H" "$B"
C=../../main/calib_autoread.h; CB=$(mktemp); cp "$C" "$CB"
trap 'cp "$B" "$H"; cp "$CB" "$C"; rm -f "$B" "$CB"' EXIT
RC=0
check() {   # build failure must not read as "0 FAIL"; rc=1: survivor / build fail / red baseline (CI)
    rm -f test_runner; out=$(make test 2>&1)
    if ! grep -qE 'ALL PASSED|CHECK\(S\) FAILED' <<<"$out"; then echo "== $1: BUILD FAILED"; RC=1; return; fi
    local n; n=$(grep -c '^FAIL' <<<"$out")
    echo "== $1: $n FAIL"; grep '^FAIL' <<<"$out" | sed 's/^/   /'
    if [ "$1" = baseline ]; then [ "$n" -eq 0 ] || RC=1; else [ "$n" -gt 0 ] || RC=1; fi
}
run() {
    cp "$B" "$H"; sed -i "$2" "$H"
    if cmp -s "$H" "$B"; then echo "== $1: SED DID NOT APPLY"; RC=1; return; fi
    check "$1"
}
run M1_no_armed_bypass  's/if (!g->armed) {/if (0) {/'                                   # boot_without_reset
run M2_no_seq_check     's/if (stat_seq == g->reject_first_seq) {/if (0) {/'             # u1_timeout_needs_new_stat
run M3_no_gen_check     's/if (stat_gen != current_gen) {/if (0) {/'                     # u1_old_gen_never_starts_timer
run M4_timeout_le       's/\/ 1000000 < SPECTRUM_RESET/\/ 1000000 <= SPECTRUM_RESET/'    # u1_timeout_needs_new_stat
run M5_no_plausible     's/if (spectrum_reset_stat_is_plausible_gen(/if (0 \&\& spectrum_reset_stat_is_plausible_gen(/'  # awf4_race
run M6_no_by_timeout    's/\*by_timeout = true;/(void)by_timeout;/'                      # u1_timeout_needs_new_stat
run M7_timer_restarts   's/if (g->reject_since_us == 0) {/if (1) {/'                     # u1_timeout_needs_new_stat
# release-gate 1.2.29, Н-1.1..Н-1.3 / Н-1
run M8_plausible_no_gen 's/if (spectrum_reset_stat_is_plausible_gen(stat_time_sec, elapsed_since_reset_sec, stat_gen, current_gen)) {/if (spectrum_reset_stat_is_plausible(stat_time_sec, elapsed_since_reset_sec)) {/'  # n13
run M9_no_seq_inc       's/    t->seq++;/    (void)t;/'                                     # n12_stat_tag_stamp
run M10_no_session      's/return t->fresh \&\& t->session == session;/return ((void)session, t->fresh);/'    # n11_stat_tag_session
run M11_publish_armed   's/    g->armed = false;/    (void)g;/'                               # n12_gate_on_publish
run M14_pending_no_armed 's/return g->armed \&\& pending_gen == current_gen;/return ((void)g, pending_gen == current_gen);/'   # nd1
run M15_pending_no_gen   's/return g->armed \&\& pending_gen == current_gen;/return ((void)pending_gen, (void)current_gen, g->armed);/'   # nd1
run M12_resync_always   's/return first_valid \&\& !reset_confirmed;/return ((void)reset_confirmed, first_valid);/'   # n12_gate_on_publish
cp "$B" "$H"
sed -i 's/return read_success \&\& !serial_only_request;/return ((void)serial_only_request, read_success);/' "$C"
if cmp -s "$C" "$CB"; then echo "== M13_calib_serial_only: SED DID NOT APPLY"; RC=1; else check M13_calib_serial_only; fi  # calib_autoread
cp "$CB" "$C"
check baseline
exit $RC
