#!/bin/sh
# 1.2.32 (F12/F13): мутационная приёмка test/web/view_restore_test.mjs на КОПИИ web/index.html (репо не меняется).
# Запуск из корня репо: sh test/web/mutate_view_restore.sh. Ждём: годный fails=0; каждый мутант fails>=1.
T=${TMPDIR:-/tmp}/vr_mut.html
good() { node test/web/view_restore_test.mjs web/index.html | grep -c '^FAIL'; }
run() { # $1=имя $2=sed-выражение
  sed "$2" web/index.html > "$T"
  if cmp -s "$T" web/index.html; then echo "$1: SED NOOP"; return; fi
  echo "$1: fails=$(node test/web/view_restore_test.mjs "$T" | grep -c '^FAIL')"
}
echo "good: fails=$(good)"
run M1_restore_no_ns '/^function viewRestore/ s/,1)/)/g'
run M2_setCps_no_save '/^function setCps/ s/if(!ns)viewSave();//'
run M3_save_no_pow 's/pow:isPow,//'
run M4_no_startup_restore '/^ viewRestore();$/d'
run M5_setLog_keeps_pow '/^function setLog/ s/isPow=false;//'
run M6_cps_default_false '/^function viewRestore/ s/d(o\.cps,isCps)/!!o.cps/'
run M7_kev_default_false '/^function viewRestore/ s/d(o\.kev,isKev)/!!o.kev/'
run M8_setKev_no_save '/^function setKev/ s/if(!ns)viewSave();//'
run M9_setLog_no_save '/^function setLog/ s/if(!ns)viewSave();//'
run M10_setPow_no_save '/^function setPow/ s/if(!ns)viewSave();//'
rm -f "$T"
