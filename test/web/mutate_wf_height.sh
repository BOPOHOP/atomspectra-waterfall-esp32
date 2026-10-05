#!/bin/sh
# 1.2.33: мутационная приёмка (#SA-3) wf_height/wf_layout/wf_axis на КОПИИ web/waterfall.html (репо не меняется).
# Запуск из корня репо: sh test/web/mutate_wf_height.sh. Ждём: годный fails=0; каждый мутант fails>=1 (fails = FAIL-строки + итоговая FAILED, т.е. красных тестов на 1 меньше).
T=${TMPDIR:-/tmp}/wfh_mut.html
f() { echo "$1 fails=$(node test/web/$2 "$T" | grep -c '^FAIL')"; }
m() { sed "$2" web/waterfall.html > "$T"; if cmp -s "$T" web/waterfall.html; then echo "$1: SED NOOP"; return 1; fi; f "$1" "$3"; }
echo "good height: $(node test/web/wf_height_test.mjs web/waterfall.html | grep -c '^FAIL') layout: $(node test/web/wf_layout_test.mjs web/waterfall.html | grep -c '^FAIL') axis: $(node test/web/wf_axis_test.mjs web/waterfall.html | grep -c '^FAIL')"
m "H1c_stretch_back" 's/Math.floor(bottom-Math.floor(y))-baseIndex/Math.floor(bottom-Math.floor(y*0.5))-baseIndex/' wf_height_test.mjs
m "H2p_rowAt_nofloor" 's/Math.floor(bottom-Math.floor(y))-baseIndex/Math.floor(bottom-y)-baseIndex/' wf_height_test.mjs
m "H3p_parse_5digits" 's/\[0-9\]{1,4}/[0-9]{1,5}/' wf_height_test.mjs
m "H4p_clamp_nan" 's/if(!isFinite(v))return WFH_DEF;//' wf_height_test.mjs
m "H5p_win_const520" 's/Math.min(h,n);}/Math.min(520,n);}/' wf_height_test.mjs
m "L1c_start_chHi_CH" 's/var chLo=0, chHi=wfChMax(CH);/var chLo=0, chHi=CH;/' wf_layout_test.mjs
m "L2p_chmax_n-2" 's/return n>1?n-1:n;/return n>1?n-2:n;/' wf_layout_test.mjs
m "L3p_no_cHi_clamp" 's/if(cHi>chHi)cHi=chHi;//' wf_layout_test.mjs
m "P1p_no_slider_pad" 's/calc(16px + var(--wf-slider) + var(--wf-gap))/16px/' wf_layout_test.mjs
m "P2p_gap_differs" 's/gap:var(--wf-gap); align-items/gap:6px; align-items/' wf_layout_test.mjs
m "O1c_overlay_stretch" 's/Wd=iw\/k,totH=ih\/k,/Wd=iw\/k,totH=ih\/k*1.7,/' wf_overlay_test.mjs
m "O2p_h_floor_to_ceil" 's/h:Math.max(1,Math.floor(0.98\*regionH))/h:Math.max(1,Math.ceil(0.98*regionH))/' wf_overlay_test.mjs
m "O3p_dataH_off1" 's/var n=bottom-baseIndex+1;/var n=bottom-baseIndex;/' wf_overlay_test.mjs
m "O4p_marker_full_h" 's/g.lineTo(x,dH)/g.lineTo(x,h-16)/' wf_overlay_test.mjs
m "O5p_ovH_back_to_WFHd" 's/scrollAcc+=frac\*ovH;/scrollAcc+=frac*WFHd;/' cursor_row_test.mjs
m "A1c_k_suffix" 's/wfAxisNum(ch2kev(ch))/ch2kev(ch).toFixed(0)+"k"/' wf_axis_test.mjs
m "A2p_no_unit" 's/lbl+" "+t("ax.kev")/lbl/' wf_axis_test.mjs
m "A3p_floor" 's/var r=Math.round(v)/var r=Math.floor(v)/' wf_axis_test.mjs
rm -f "$T"
