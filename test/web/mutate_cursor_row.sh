#!/bin/sh
# 1.2.32: мутационная приёмка test/web/cursor_row_test.mjs на КОПИИ web/waterfall.html (репо не меняется).
# Запуск из корня репо: sh test/web/mutate_cursor_row.sh. Ждём: годный fails=0; мутант fails>=1.
T=${TMPDIR:-/tmp}/cur_mut.html
echo "good: fails=$(node test/web/cursor_row_test.mjs web/waterfall.html | grep -c '^FAIL')"
sed '/^function drawCursor/,/^}/ s/var li=wfhRowAt(y,bottom,baseIndex);/var li=(bottom-y*2)-baseIndex;/' web/waterfall.html > "$T"
if cmp -s "$T" web/waterfall.html; then echo "M1_old_formula: SED NOOP"; rm -f "$T"; exit 1; fi
echo "M1_old_formula: fails=$(node test/web/cursor_row_test.mjs "$T" | grep -c '^FAIL')"
sed "/^function draw()/,/^}/ s/var gIdx=bottom-y;/var gIdx=bottom-Math.floor(y*0.5);/" web/waterfall.html > "$T"
echo "M2_draw_stretch: fails=$(node test/web/cursor_row_test.mjs "$T" | grep -c '^FAIL')"
rm -f "$T"
