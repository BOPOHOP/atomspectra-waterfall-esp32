#!/bin/bash
# WP9 (1.2.30): сборка диагностического образа (TWDT с паникой, run-time stats, heap poisoning, coredump).
# Запуск внутри контейнера espressif/idf:v5.4 из корня проекта: bash scripts/build_diag.sh
# Свой sdkconfig и каталог сборки — основной sdkconfig/build не затрагиваются. В релиз образ НЕ идёт.
set -e
. /opt/esp/idf/export.sh >/dev/null 2>&1
rm -f sdkconfig.diag
idf.py -B build-diag-130 -D SDKCONFIG=sdkconfig.diag \
  -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.diag.defaults" build
echo "DIAG_BUILD_RC=$?"
