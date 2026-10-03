#!/usr/bin/env bash
# 1.2.31 (#SA-3): мутанты web/saved.html (разбор/кодирование импорта) и web/index.html (перенос оверлея по энергии).
# Каждый мутант = копия страницы с одной правкой; node-тест обязан дать ровно N строк FAIL (N — последний столбец), baseline — 0.
# N > 1 там, где одну величину проверяют несколько независимых проверок (CRC: zlib + общий вектор с C; сумма счёта: линия/боевой/переполнение).
# Запуск из корня репозитория: bash test/web/mutate_import_web.sh
set -u; cd "$(dirname "$0")/../.."; T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; RC=0
mut() {   # mut <имя> <страница> <тест.mjs> <sed | -> <ожидаемое число FAIL>
    cp "web/$2" "$T/p.html"
    if [ "$4" != - ]; then sed -i "$4" "$T/p.html"; cmp -s "web/$2" "$T/p.html" && { echo "== $1: SED DID NOT APPLY"; RC=1; return; }; fi
    local n; n=$(node "test/web/$3" "$T/p.html" 2>&1 | grep -c '^FAIL ')
    echo "== $1: $n FAIL (need $5)"; [ "$n" -eq "$5" ] || RC=1
}
mut baseline_import  saved.html import_parse_test.mjs -                                                      0
mut baseline_rebin   index.html rebin_test.mjs         -                                                      0
mut W1_crc_poly      saved.html import_parse_test.mjs 's/0xEDB88320/0xEDB88321/'                             2
mut W2_hdr_size      saved.html import_parse_test.mjs 's/dv.setUint16(6,128,true)/dv.setUint16(6,127,true)/'  2
mut W3_endianness    saved.html import_parse_test.mjs 's/dv.setUint32(128+4\*i,p.bins\[i\],true)/dv.setUint32(128+4*i,p.bins[i],false)/' 1
mut W4_channels      saved.html import_parse_test.mjs 's/if(!b||b.length!==IMP_CH)impFail/if(!b)impFail/'     1
mut W5_negative      saved.html import_parse_test.mjs 's/if(!(v>=0&&v<=IMP_U32/if(!(v<=IMP_U32/'           1
mut W6_time0         saved.html import_parse_test.mjs 's/if(!(p.time>=1&&p.time<=IMP_MAXT))/if(!(p.time<=IMP_MAXT))/' 1
mut W7_xml_nch       saved.html import_parse_test.mjs 's/Number(nc)!==b.length/false/'                       1
mut W8_xml_date      saved.html import_parse_test.mjs 's/savedAt:sa||Math.floor(mt\/1000),serial:nm/savedAt:Math.floor(mt\/1000),serial:nm/' 1
mut W9_imp_prefix    saved.html import_parse_test.mjs 's/.replace(\/^IMP:\/,"")//'                           1
mut R1_no_overlap    index.html rebin_test.mjs 's/out\[j\]+=v\*fin\*o/out[j]+=v*o*2/'                        3
mut R2_no_mono       index.html rebin_test.mjs 's/||!calMono(ca,n)//'                                        1
mut R3_overflow      index.html rebin_test.mjs 's/ out\[n-1\]+=src\[n-1\];//'                                2
mut R4_no_same       index.html rebin_test.mjs 's/||calSame(ca,cb))return src/)return src/'                  1
mut R5_clip_above    index.html rebin_test.mjs 's/out\[ehi<=bmin?0:n-1\]+=v/out[0]+=v/'                      1
mut baseline_undef   index.html undef_calls_test.mjs   -                                                      0
mut U1_calibsame     index.html undef_calls_test.mjs   's/!calSame(overlayCal,calib)/!calibSame(overlayCal,calib)/' 1
exit $RC
