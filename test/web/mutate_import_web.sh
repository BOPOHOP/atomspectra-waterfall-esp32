#!/usr/bin/env bash
# 1.2.31 (#SA-3): мутанты web/saved.html (разбор/кодирование импорта) и web/index.html (перенос оверлея по энергии).
# Каждый мутант = копия страницы с одной правкой; node-тест обязан дать ровно N строк FAIL (N — последний столбец), baseline — 0.
# N > 1 там, где одну величину проверяют несколько независимых проверок (CRC: zlib + общий вектор с C; сумма счёта: линия/боевой/переполнение).
# Запуск из корня репозитория: bash test/web/mutate_import_web.sh
set -u; cd "$(dirname "$0")/../.."; T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; RC=0
mut() {   # mut <имя> <страница> <тест.mjs> <sed | -> <ожидаемое число FAIL>
    cp "web/$2" "$T/p.html"
    if [ "$4" != - ]; then sed -i "$4" "$T/p.html"; cmp -s "web/$2" "$T/p.html" && { echo "== $1: SED DID NOT APPLY"; RC=1; return; }; fi
    local n; n=$(TZ=EST5EDT node "test/web/$3" "$T/p.html" 2>&1 | grep -c '^FAIL ')
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
mut W8_xml_date      saved.html import_parse_test.mjs 's/savedAt:sa||Math.floor(mt\/1000),serial:nm/savedAt:Math.floor(mt\/1000),serial:nm/' 2
mut W9_imp_prefix    saved.html import_parse_test.mjs 's/.replace(\/^IMP:\/,"")//'                           1
mut R1_no_overlap    index.html rebin_test.mjs 's/out\[j\]+=v\*fin\*o/out[j]+=v*o*2/'                        3
mut R2_no_mono       index.html rebin_test.mjs 's/||!calMono(ca,n)//'                                        1
mut R3_overflow      index.html rebin_test.mjs 's/ out\[n-1\]+=src\[n-1\];//'                                2
mut R4_no_same       index.html rebin_test.mjs 's/||calSame(ca,cb))return src/)return src/'                  1
mut R5_clip_above    index.html rebin_test.mjs 's/out\[ehi<=bmin?0:n-1\]+=v/out[0]+=v/'                      1
mut U2_xml_board_local saved.html import_parse_test.mjs 's/impBoardXml(txt)):0;/false):0;/' 1
mut U3_xml_always_utc  saved.html import_parse_test.mjs 's/impBoardXml(txt)):0;/true):0;/'  1
mut U4_guid_not_zero   saved.html import_parse_test.mjs 's/0{8}-0{4}-0{4}/1{8}-0{4}-0{4}/'    1
mut baseline_undef   index.html undef_calls_test.mjs   -                                                      0
mut U1_calibsame     index.html undef_calls_test.mjs   's/!calSame(overlayCal,calib)/!calibSame(overlayCal,calib)/' 1
mut W10_n42_z_flag   saved.html import_parse_test.mjs 's/m\[7\]==="Z"/m[7]==="z"/'  1
mut baseline_poll    common-time.js poll_visible_test.mjs -                                                0
mut P1_no_hidden_skip common-time.js poll_visible_test.mjs 's/return setInterval(function () { if (!document.hidden) fn(); }, ms);/return setInterval(function () { fn(); }, ms);/' 3
mut P2_no_resume_call common-time.js poll_visible_test.mjs 's/function () { if (!document.hidden) fn(); });$/function () {});/' 2
mut P3_resume_if_hidden common-time.js poll_visible_test.mjs 's/function () { if (!document.hidden) fn(); });$/function () { fn(); });/' 1
# OPT-12 (общие post/gget в common-time.js; ретрай CSRF) - мутанты common-time.js и обёртки postJson в waterfall.html
mutw() {   # mutw <имя> <sed> <ожидаемое число FAIL>: мутант waterfall.html (csrf_retry_test берёт вторым аргументом страницу)
    cp web/waterfall.html "$T/w.html"; sed -i "$2" "$T/w.html"; cmp -s web/waterfall.html "$T/w.html" && { echo "== $1: SED DID NOT APPLY"; RC=1; return; }
    local n; n=$(node test/web/csrf_retry_test.mjs web/common-time.js "$T/w.html" 2>&1 | grep -c '^FAIL ')
    echo "== $1: $n FAIL (need $3)"; [ "$n" -eq "$3" ] || RC=1
}
mut baseline_csrf    common-time.js csrf_retry_test.mjs -                                                  0
mut C1_post_no_retry common-time.js csrf_retry_test.mjs 's/if(r.status===403){await loadCsrf();opts.headers=Object.assign({},h,{"X-CSRF-Token":csrfToken});r=await fetch(url,opts)}return r}/return r}/' 3
mut C2_post_no_refresh common-time.js csrf_retry_test.mjs 's/if(r.status===403){await loadCsrf();opts.headers/if(r.status===403){opts.headers/' 3
mut C3_gget_no_retry common-time.js csrf_retry_test.mjs 's/if(r.status===403){await loadCsrf();r=await f();}return r;}/return r;}/' 1
mut C4_gget_no_load  common-time.js csrf_retry_test.mjs 's/if(!csrfToken)await loadCsrf();//'              1
mut C5_post_method   common-time.js csrf_retry_test.mjs 's/opts.method="POST";/opts.method="GET";/'         1
mut C6_post_twice    common-time.js csrf_retry_test.mjs 's/r=await fetch(url,opts)}return r}/r=await fetch(url,opts);if(r.status===403){await loadCsrf();r=await fetch(url,opts)}}return r}/' 1
mutw C7_json_no_ctype 's/headers:{"Content-Type":"application\/json"},body:body/headers:{},body:body/' 2
mutw C8_json_no_stringify 's/body:body?JSON.stringify(body):undefined/body:body/' 1
exit $RC
