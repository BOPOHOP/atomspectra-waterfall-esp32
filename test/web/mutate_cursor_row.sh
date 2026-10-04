#!/bin/sh
# 1.2.32: мутационная приёмка test/web/cursor_row_test.mjs на КОПИИ web/waterfall.html (репо не меняется).
# Запуск из корня репо: sh test/web/mutate_cursor_row.sh. Ждём: годный fails=0; мутант fails>=1.
T=${TMPDIR:-/tmp}/cur_mut.html
echo "good: fails=$(node test/web/cursor_row_test.mjs web/waterfall.html | grep -c '^FAIL')"
sed '/^function drawCursor/,/^}/ s/var li=Math.floor(bottom-Math.floor(y\*visRows()\/WFHd))-baseIndex;/var li=(bottom-y)-baseIndex;/' web/waterfall.html > "$T"
if cmp -s "$T" web/waterfall.html; then echo "M1_old_formula: SED NOOP"; rm -f "$T"; exit 1; fi
echo "M1_old_formula: fails=$(node test/web/cursor_row_test.mjs "$T" | grep -c '^FAIL')"
sed '/^function visRows/ s/Math.min(WFHd,n)/WFHd/' web/waterfall.html > "$T"
echo "M2_visRows_const: fails=$(node test/web/cursor_row_test.mjs "$T" | grep -c '^FAIL')"
rm -f "$T"
