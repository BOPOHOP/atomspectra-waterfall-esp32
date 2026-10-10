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
mut P1_no_hidden_skip common-time.js poll_visible_test.mjs 's/return setInterval(function () { if (!document.hidden) run(); }, ms);/return setInterval(function () { run(); }, ms);/' 3
mut P2_no_resume_call common-time.js poll_visible_test.mjs 's/function () { if (!document.hidden) run(); });$/function () {});/' 2
mut P3_resume_if_hidden common-time.js poll_visible_test.mjs 's/function () { if (!document.hidden) run(); });$/function () { run(); });/' 1
# S1-3 (1.2.36): защита от повторного входа в pollWhileVisible
mut P5_no_reentry_guard common-time.js poll_visible_test.mjs 's/    if (since \&\& Date.now() - since < Math.max(30000, ms \* 10)) return;//' 5
mut P6_reject_latches common-time.js poll_visible_test.mjs 's/r.then(done, done);/r.then(done, function () {});/' 3
mut P7_no_stale_release common-time.js poll_visible_test.mjs 's/Date.now() - since < Math.max(30000, ms \* 10)/Date.now() - since < 1e15/' 1
mut P8_never_busy common-time.js poll_visible_test.mjs 's/var mine = since = Date.now();/var mine = Date.now();/' 5
mut P9_sync_latch common-time.js poll_visible_test.mjs 's/    var r = fn();/    since = Date.now(); var r = fn();/' 4
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
# OPT-13 (каркас i18n в common-time.js) и №6 wf_passA_test (setLang -> onLang страницы -> updGrip)
mut baseline_i18n    common-time.js i18n_core_test.mjs -                                                   0
mut I1_no_ru_fallback common-time.js i18n_core_test.mjs 's/(D.ru\[k\] || k)/k/'                            1
mut I2_no_lang_check common-time.js i18n_core_test.mjs 's/if (l !== "ru" && l !== "en") l = "ru";//'      1
mut I3_no_storage    common-time.js i18n_core_test.mjs 's/try { localStorage.setItem("aswf-lang", l); } catch (e) {}//' 2
mut I4_no_onLang     common-time.js i18n_core_test.mjs 's/if (I18N.onLang) I18N.onLang(l);//'             1
mut I5_no_title_attr common-time.js i18n_core_test.mjs 's/el.title = tr(key);//'                           1
mut I6_html_as_text  common-time.js i18n_core_test.mjs 's/el.innerHTML = tr(key);/el.textContent = tr(key);/' 1
mut I7_no_onApply    common-time.js i18n_core_test.mjs 's/if (I18N.onApply) I18N.onApply();//'            1
mut baseline_passA   waterfall.html wf_passA_test.mjs  -                                                   0
mut A1_no_updGrip    waterfall.html wf_passA_test.mjs  's/try{if(typeof updGrip==="function")updGrip();}catch(e){}//' 1
# OPT-11 (время/длительность/локаль/пояс в common-time.js; time_fmt_test) - число FAIL = число независимых проверок, затронутых порчей
mut baseline_time    common-time.js time_fmt_test.mjs  -                                                   0
mut T1_dur_no_hours  common-time.js time_fmt_test.mjs  's/if (d > 0 || h > 0) p.push(h + u.h);/if (h > 0) p.push(h + u.h);/' 1
mut T2_dur_no_min    common-time.js time_fmt_test.mjs  's/if (d > 0 || h > 0 || m > 0) p.push(m + u.m);/if (m > 0) p.push(m + u.m);/' 2
mut T3_dur_floor     common-time.js time_fmt_test.mjs  's/s = Math.max(0, Math.round(s));/s = Math.max(0, Math.floor(s));/' 1
mut T4_clock_round   common-time.js time_fmt_test.mjs  's/s = Math.max(0, Math.floor(s));/s = Math.max(0, Math.round(s));/' 1
mut T5_pad2          common-time.js time_fmt_test.mjs  's/return n < 10 ? "0" + n : "" + n;/return "" + n;/' 9
mut T6_lcl_ru        common-time.js time_fmt_test.mjs  's/return LANG === "ru" ? "ru-RU" : "en-US";/return "ru-RU";/' 3
mut T7_tz_no_offset  common-time.js time_fmt_test.mjs  's/(off + d.getTimezoneOffset())/(off)/' 1
mut T8_tz_label_sign common-time.js time_fmt_test.mjs  's/var s = min < 0 ? "-" : "+";/var s = "+";/' 1
mut T9_clock_no_days common-time.js time_fmt_test.mjs  's/var prefix = d > 0 ? d + u.d + " " : "";/var prefix = "";/' 2
mut T10_tz_no_default common-time.js time_fmt_test.mjs 's/return localStorage.getItem(TZ_KEY) || "auto";/return localStorage.getItem(TZ_KEY);/' 1
mut T11_dt_no_tz     common-time.js time_fmt_test.mjs  's/return tzShift(new Date(ms)).toLocaleString(lcl());/return new Date(ms).toLocaleString(lcl());/' 1
mut T12_tod_no_lcl   common-time.js time_fmt_test.mjs  's/return tzShift(d).toLocaleTimeString(lcl());/return tzShift(d).toLocaleTimeString("en-US");/' 1
# OPT-9 (энергетический полином, обратная функция, монотонность: блок CAL в common-time.js; cal_poly_test + боевые rebin_test/import_parse_test)
mutc() {   # mutc <имя> <тест.mjs> <страница> <sed по common-time.js> <ожидаемое число FAIL>: тест берёт страницу (argv2) и мутанта common-time.js (argv3)
    cp web/common-time.js "$T/c.js"; sed -i "$4" "$T/c.js"; cmp -s web/common-time.js "$T/c.js" && { echo "== $1: SED DID NOT APPLY"; RC=1; return; }
    local n; n=$(node "test/web/$2" "web/$3" "$T/c.js" 2>&1 | grep -c '^FAIL ')
    echo "== $1: $n FAIL (need $5)"; [ "$n" -eq "$5" ] || RC=1
}
mut baseline_cal     common-time.js cal_poly_test.mjs  -                                                   0
mut K1_horner_order  common-time.js cal_poly_test.mjs  's/for (var i = c.length - 1; i >= 0; i--) e = e \* x + c\[i\];/for (var i = 0; i < c.length; i++) e = e * x + c[i];/' 13
mut K2_mono_nonstrict common-time.js cal_poly_test.mjs 's/if (!(e > p)) return false;/if (!(e >= p)) return false;/' 2
mut K3_mono_few_pts  common-time.js cal_poly_test.mjs  's/for (var j = 0; j <= 64; j++) {/for (var j = 0; j <= 2; j++) {/' 1
mut K4_same_tol      common-time.js cal_poly_test.mjs  's/< 0.5)) return false;/< 5)) return false;/'    1
mut K5_inv_20_iter   common-time.js cal_poly_test.mjs  's/i < 45; i++/i < 20; i++/'                     6
mut K6_inv_lo        common-time.js cal_poly_test.mjs  's/lo = -0.5;/lo = 0;/'                           1
mut K7_inv_hi        common-time.js cal_poly_test.mjs  's/hi = n - 0.5;/hi = n;/'                        1
mut K8_inv_direction common-time.js cal_poly_test.mjs  's/if (calE(c, m) < E) lo = m;/if (calE(c, m) > E) lo = m;/' 6
mut K9_kev_no_calib  common-time.js cal_poly_test.mjs  's/^    return ch;$/    return 0;/'                1
mut K10_chan_range   common-time.js cal_poly_test.mjs  's/return calInv(c || \[0, 1\], kev, n, 0, n);/return calInv(c || [0, 1], kev, n);/' 1
mut K11_chan_unit    common-time.js cal_poly_test.mjs  's/c || \[0, 1\]/c || [0, 2]/'                    1
mutc K12_horner_rebin  rebin_test.mjs index.html 's/for (var i = c.length - 1; i >= 0; i--) e = e \* x + c\[i\];/for (var i = 0; i < c.length; i++) e = e * x + c[i];/' 4
mutc K13_inv_3_rebin   rebin_test.mjs index.html 's/i < 45; i++/i < 3; i++/' 2
mutc K14_mono_off_rebin rebin_test.mjs index.html 's/if (!(e > p)) return false;/if (false) return false;/' 2
mutc K15_mono_off_import import_parse_test.mjs saved.html 's/if (!(e > p)) return false;/if (false) return false;/' 2
mut  K16_import_no_mono saved.html import_parse_test.mjs 's/ return calMonoCh(c,IMP_CH);/ return true;/'          2
# 1.2.36 (S1-2): приём калибровок при импорте - прежняя сетка 64 целых канала (calMonoCh), а не calMono
mut  K17_import_calmono saved.html import_parse_test.mjs 's/ return calMonoCh(c,IMP_CH);/ return calMono(c,IMP_CH);/' 1
mut  K18_monoch_grid common-time.js cal_poly_test.mjs 's/Math.round(j \* (n - 1) \/ 63)/Math.round(j * n \/ 64)/' 2
# OPT-12 / LOAD-2 водопад: опросы статуса через pollWhileVisible
mut baseline_wfpoll  waterfall.html poll_waterfall_test.mjs -                                                0
mut P4_wf_status_bare waterfall.html poll_waterfall_test.mjs 's/(window.pollWhileVisible||setInterval)(refreshStatus,5000)/setInterval(refreshStatus,5000)/' 2
exit $RC
