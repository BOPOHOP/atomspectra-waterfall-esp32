#!/usr/bin/env bash
#AUD-DUP1 / F-13: мутанты main/wf_ref_plan.h — на КОПИИ (-I$T впереди ../../main), исходник не трогаем, можно параллельно со сборкой; каждый мутант обязан покраснеть ровно в названных тест-функциях, baseline — без FAIL
#Без -Werror: мутант, выключивший проверку, оставляет параметр неиспользованным — это не должно ронять сборку.
#Не мутируются проверки h==NULL и bins==NULL: без них тест разыменует NULL (падение, а не FAIL)

set -u; cd "$(dirname "$0")"; T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; RC=0; H=../../main/wf_ref_plan.h
CF="-std=c11 -Wall -Wextra -O2 -I$T -I../../components/shproto/include -I../../main"

mut() {
    local name="$1" sed_expr="$2" expected="$3"
    cp "$H" "$T/wf_ref_plan.h"
    if [ -n "$sed_expr" ]; then
        sed -i "$sed_expr" "$T/wf_ref_plan.h"
        if cmp -s "$H" "$T/wf_ref_plan.h"; then
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
mut W2_size 's/if (file_bytes != sizeof/if (0 \&\& file_bytes != sizeof/' 'test_pick_rejects'
mut W3_magic 's/if (h->magic != WF_REF_MAGIC) {/if (0) {/' 'test_pick_rejects'
mut W4_channels 's/if (h->channels != channels) {/if (0) {/' 'test_pick_rejects'
mut W5_session0 's/if (h->boot_session == 0) {/if (0) {/' 'test_pick_rejects'
mut W6_next_boot 's/if (h->boot_session + 1u != cur_session) {/if (0) {/' 'test_pick_rejects'
mut W8_checksum 's/if (h->sum != wf_ref_checksum(h, bins, channels)) {/if (0) {/' 'test_pick_rejects'
mut W9_autosave 's/        return ref_bins;/        return live_bins;/' 'test_first_row_no_double_count'
mut W10_row_on_resync 's/return c == WF_REF_USE_FILE;/return true;/' 'test_first_row_forced_resync'
mut W11_clamp_hi 's/if (d > 65535) {/if (0) {/' 'test_row_delta'
mut W12_clamp_lo 's/if (d < 0) {/if (0) {/' 'test_row_delta'
mut W13_reset 's/(reset ? 0 : (int64_t)prev)/((int64_t)prev)/' 'test_first_row_device_reset_during_reboot test_row_delta'
mut W14_sum_bins 's/hash = wf_ref_fnv_u32(hash, bins\[i\]);/(void)bins;/' 'test_pick_rejects'
# #AUD-RST: R1 = старое поведение (всегда перенос опоры без строки) — обязан краснеть на потере
mut R1_old_resync 's/if (pre == NULL || cur == NULL) {/if (1) {/' 'test_rst_rejects test_rst_unconfirmed_no_loss'
mut R2_total 's/if (cur_total < pre_total || cur_time < pre_time) {/if (cur_time < pre_time) {/' 'test_rst_rejects'
mut R3_time 's/if (cur_total < pre_total || cur_time < pre_time) {/if (cur_total < pre_total) {/' 'test_rst_rejects'
mut R4_channels 's/if (cur\[i\] < pre\[i\]) {/if (0) {/' 'test_rst_after_device_restart_no_jump test_rst_device_did_reset test_rst_rejects'
mut R5_last_channel 's/for (size_t i = 0; i < channels; i++) {/for (size_t i = 0; i + 1 < channels; i++) {/' 'test_rst_rejects'

rm -f test_runner; exit $RC
