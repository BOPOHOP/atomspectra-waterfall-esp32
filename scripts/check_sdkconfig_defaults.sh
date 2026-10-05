#!/bin/sh
# Проверка: действующий sdkconfig совпадает с sdkconfig.defaults.
# defaults применяется ТОЛЬКО при создании sdkconfig; существующий (он в .gitignore) игнорирует его молча.
# Запуск из корня проекта: sh scripts/check_sdkconfig_defaults.sh [sdkconfig]. rc 0 = совпало, 1 = расхождение.
F=${1:-sdkconfig}; rc=0; n=0
# ОГРАНИЧЕНИЕ: ключ, которого нет в sdkconfig, считается =n (так IDF пишет "# CONFIG_X is not set" либо ключа нет вовсе).
# Поэтому опечатка в имени ключа defaults со значением =n НЕ ловится (отличить её от легитимного "нет такой опции" без списка
# всех Kconfig-ключей нельзя); для =y/число опечатка ловится (в sdkconfig ключа нет -> got=n != v).
# "|| [ -n "$l" ]": последняя строка без завершающего \n тоже проверяется.
while IFS= read -r l || [ -n "$l" ]; do
  l=$(printf %s "$l" | tr -d '\r')
  case "$l" in CONFIG_*=*) ;; *) continue ;; esac
  k=${l%%=*}; v=${l#*=}; n=$((n+1))
  got=$(grep -m1 "^$k=" "$F" | cut -d= -f2- | tr -d '\r'); [ -n "$got" ] || got=n
  [ "$got" = "$v" ] || { echo "MISMATCH $k: defaults=$v $F=$got"; rc=1; }
done < sdkconfig.defaults
echo "checked=$n rc=$rc"; exit $rc
