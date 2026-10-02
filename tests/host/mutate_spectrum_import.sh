#!/usr/bin/env bash
# 1.2.31 (импорт фона + запрет удаления «любым POST»): мутанты main/spectrum_import_plan.h (#SA-3, по образцу mutate_wf_ref.sh).
# Каждый мутант (копия заголовка с одной правкой) обязан покраснеть РОВНО в одном, названном тесте; baseline — ни в одном.
# Вывод: "== имя: N FAIL (need 1) red=[тесты]"; N — число покрасневших тестов (строки IMPFAIL из test_spectrum_import_plan.c).
set -u; cd "$(dirname "$0")"; T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; RC=0
printf '#include <stdio.h>\nint g_failures;\nvoid spectrum_import_plan_suite(void);\nint main(void){spectrum_import_plan_suite();return 0;}\n' > "$T/m.c"
mut() {   # mut <name> <sed-expr | -> <ожидаемый тест | ->
    rm -rf "$T/inc"; mkdir "$T/inc"; cp ../../main/atomspectra.h ../../main/spectrum_import_plan.h "$T/inc/"
    if [ "$1" != baseline ]; then cp "$T/inc/spectrum_import_plan.h" "$T/o"; sed -i "$2" "$T/inc/spectrum_import_plan.h"
        cmp -s "$T/o" "$T/inc/spectrum_import_plan.h" && { echo "== $1: SED DID NOT APPLY"; RC=1; return; }; fi
    gcc -std=c11 -O1 -w -I"$T/inc" -o "$T/t" "$T/m.c" test_spectrum_import_plan.c -lm || { echo "== $1: BUILD FAIL"; RC=1; return; }
    local out red n want=1 wt="$3"; out=$("$T/t" 2>&1); red=$(grep '^IMPFAIL' <<<"$out" | awk '{print $2}' | tr '\n' ' ')
    n=$(grep -c '^IMPFAIL' <<<"$out"); [ "$1" = baseline ] && { want=0; wt=""; }
    local tag=""; [ "$red" = "${wt:+$wt }" ] || tag=" WRONG TEST (want: ${wt:-none})"
    echo "== $1: $n FAIL (need $want) red=[$red]$tag"; { [ "$n" -eq "$want" ] && [ -z "$tag" ]; } || RC=1
}
mut baseline         -                                                                                  -
mut I1_size          's#if (len != SPEC_IMPORT_SIZE)#if (len < 1)#'                                       test_imp_size
mut I2_magic         's#if (imp_rd32(b) != SPEC_IMPORT_MAGIC)#if (0)#'                                    test_imp_magic
mut I3_version       's#if ((b\[4\] | b\[5\] << 8) != 1 || #if (0 || #'                                    test_imp_version
mut I3b_hdrsize      's#(b\[6\] | b\[7\] << 8) != (int)SPEC_IMPORT_HDR#0#'                                 test_imp_version
mut I4_channels      's#if (imp_rd32(b + 8) != SPECTRUM_CHANNELS)#if (0)#'                                test_imp_channels
mut I5_crc           's#if (c != imp_rd32(b + 124))#if (0)#'                                              test_imp_crc
mut I6_time0         's#if (t == 0 ||#if (0 ||#'                                                          test_imp_time
mut I7_tmax          's#t > SPEC_IMPORT_MAX_TIME#t > SPEC_IMPORT_MAX_TIME + 1#'                           test_imp_time
mut I8_sum32         's#uint64_t s = 0;#uint32_t s = 0;#'                                                 test_imp_counts
mut I9_lost          's#s + lost > UINT32_MAX#s > UINT32_MAX#'                                            test_imp_counts
mut I10_finite       's#if (!isfinite(d))#if (0)#'                                                        test_imp_calib
mut I11_prefix       's#"IMP:", 4)#"IMP;", 4)#'                                                           test_imp_ok
mut I12_zero_calib   's#(ord >= 0 && !any) ? IMP_BAD_CALIB#0 ? IMP_BAD_CALIB#'                            test_imp_calib
mut I13_serial_nul   's#if (n == 48) return IMP_BAD_SERIAL;##'                                            test_imp_serial
mut I14_saved_at     's#(sa <= 0xFFFFFFFFull)#(1)#'                                                       test_imp_30days
# дефект «любой POST /api/saved/* удаляет запись»: разбор пути удаления (handle_saved_delete зовёт saved_delete_index)
mut D1_no_suffix     's#if (strncmp(p, suf, sizeof(suf) - 1) != 0) return -1;#if (strncmp(p, suf, sizeof(suf) - 1) != 0) return v;#' test_saved_delete_uri
mut D2_tail          's#if (\*p != .\\0. && \*p != .?.) return -1;##'                                      test_saved_delete_uri
mut D3_no_digits     's#if (n == 0 || n > 4) return -1;#if (n > 4) return -1;#'                           test_saved_delete_uri
mut D4_range         's#return (v <= 9998) ? v : -1;#return v;#'                                          test_saved_delete_uri
exit $RC
