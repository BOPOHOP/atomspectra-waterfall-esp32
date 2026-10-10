#!/usr/bin/env bash
# 1.2.36 B (R-3): эталон «до» — дословный фрагмент f97ad6b; подмена файла сделала бы тест бессмысленным.
# rc 0 — sha256 (после удаления CR) совпал с записанным в шапке test_spectrum_meta_fmt.c.
set -u
cd "$(dirname "$0")"
WANT=6df4e31cad6bc2a8eb7fe0d9d991f01b702051b1cd0791015f9709ef60cdd2b5
GOT=$(tr -d '\r' < old_spectrum_json_f97ad6b.inc | sha256sum | cut -d' ' -f1)
if [ "$GOT" != "$WANT" ]; then echo "== SHA256 MISMATCH old_spectrum_json_f97ad6b.inc: $GOT (want $WANT)"; exit 1; fi
echo "== old_spectrum_json_f97ad6b.inc sha256 OK ($WANT)"
