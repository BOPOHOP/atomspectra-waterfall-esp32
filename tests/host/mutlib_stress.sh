#!/usr/bin/env bash
# 1.2.36 H1: мутант против многопоточного стресса (make stress) — дополнение к mutlib.sh (mt_init/mt_reset/MT_BAKS/MT_RC).
# stress_mut <label> <file> <sed>: стресс под мутантом обязан дать "FAIL seq_stress ... torn snapshot accepted" и rc != 0.
stress_mut() {
    mt_reset; sed -i "$3" "$2"
    local i=-1 j; for j in "${!MT_FILES[@]}"; do [ "${MT_FILES[$j]}" == "$2" ] && i=$j; done
    if [ "$i" -lt 0 ] || cmp -s "${MT_BAKS[$i]}" "$2"; then echo "== $1: SED DID NOT APPLY"; MT_RC=1; mt_reset; return; fi
    local out rc; out=$(make stress 2>&1); rc=$?
    echo "$out" | grep -E '^(INFO|FAIL) seq_stress' | sed "s/^/   [$1] /"
    if [ "$rc" -ne 0 ] && echo "$out" | grep -q '^FAIL seq_stress .*torn snapshot accepted'; then
        echo "== $1 (stress): RED, VERDICT OK"
    else
        echo "== $1 (stress): GREEN, VERDICT BAD (stress must catch this mutant)"; MT_RC=1
    fi
    mt_reset
}
