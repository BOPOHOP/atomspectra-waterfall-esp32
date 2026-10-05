#!/bin/sh
# #AWF-F4: мутационная приёмка import_tmp_plan.h. Исходник восстанавливается. Ждём: годный = 0 FAIL; каждый мутант красит тест.
H=../../main/import_tmp_plan.h
cp "$H" /tmp/itp.orig
trap 'cp /tmp/itp.orig "$H"' EXIT INT TERM   # восстановить заголовок при обрыве
run() { # $1=имя $2=sed-выражение
  cp /tmp/itp.orig "$H"; sed -i "$2" "$H"
  if cmp -s "$H" /tmp/itp.orig; then echo "$1: SED NOOP"; return; fi
  gcc -std=c11 -Wall -Wextra -Werror -O2 -o /tmp/itp_m test_import_tmp_plan.c 2>/dev/null || { echo "$1: COMPILE-ERR"; return; }
  out=$(/tmp/itp_m); echo "$1: rc=$? fails=$(echo "$out" | grep -c '^FAIL')"
}
cp /tmp/itp.orig "$H"; gcc -std=c11 -Wall -Wextra -Werror -O2 -o /tmp/itp_m test_import_tmp_plan.c && /tmp/itp_m; echo "good rc=$?"
run M1_prefix 's/strcmp(name, IMPORT_TMP_NAME) == 0/strncmp(name, IMPORT_TMP_NAME, 10) == 0/'
run M2_null_true 's/name != NULL \&\&/name == NULL ||/'
run M3_nocase 's/strcmp(name, IMPORT_TMP_NAME) == 0/strcasecmp(name, IMPORT_TMP_NAME) == 0/;s/<string.h>/<string.h>\n#include <strings.h>/'
run M4_any_tmp 's/strcmp(name, IMPORT_TMP_NAME) == 0/strlen(name) > 4 \&\& strcmp(name + strlen(name) - 4, ".tmp") == 0/'
run M5_always_false 's/strcmp(name, IMPORT_TMP_NAME) == 0/0/'
cp /tmp/itp.orig "$H"; cmp "$H" /tmp/itp.orig && echo restored
