#!/usr/bin/env bash
# 1.2.36 B (R-3): эталон «до» — ДОСЛОВНО append_fmt / append_uint32_csv_grow / build_json_full / build_json_meta из f97ad6b
# (main/spectrum_http_cache.c:59-205). Файл .inc получен этой командой; sha256 фрагмента сверяет mutate_spectrum_meta_fmt.sh.
set -eu
cd "$(dirname "$0")"
git show f97ad6b:main/spectrum_http_cache.c | sed -n '59,205p' > old_spectrum_json_f97ad6b.inc
sha256sum old_spectrum_json_f97ad6b.inc
