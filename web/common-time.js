// #FIELD-5: авто-синхронизация времени платы от браузера телефона.
// В полевом AP (Outdoor) интернета нет → SNTP не работает → плата стартует с
// near-epoch (1970). Любая открытая страница шлёт текущее время браузера на
// POST /api/time; прошивка сама решает, принимать ли (net_time_should_accept):
// при активном SNTP — откажет (война источников), иначе примет при расхождении
// > 5 с. manual=false → авто-режим (для явного ручного ввода — форма в /system).
(function () {
  async function syncTime() {
    try {
      const t = await fetch('/api/csrf-token').then(r => r.json());
      const res = await fetch('/api/time', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', 'X-CSRF-Token': t.token },
        body: JSON.stringify({ epoch_ms: Date.now(), manual: false })
      }).then(r => r.json());
      if (res && res.accepted)
        console.log('[time] синхронизировано от браузера, источник=' + res.source);
    } catch (e) {
      // Оффлайн или рано при загрузке — молча, авто-синк не должен мешать UI.
    }
  }
  if (document.readyState === 'loading')
    document.addEventListener('DOMContentLoaded', syncTime);
  else
    syncTime();
  // Повтор раз в час с открытой вкладки: корректирует дрейф часов платы (нет RTC,
  // нет SNTP в поле) без перезагрузки страницы. Приём — по той же guard-логике.
  setInterval(syncTime, 3600000);
})();

/* #PERF-2: shared HEAVY fetch — retries on 503 + Retry-After (segment/export/window).
   Retry-After берётся с потолка сервера/прокси, поэтому доверяем ему ограниченно:
   clamp [1,5] с плюс общий дедлайн на всю серию попыток. Без дедлайна 30 попыток
   по 5 с растянулись бы на 150 с, и вкладка выглядела бы зависшей. */
window.HEAVY_RETRY_MIN_S = 1;
window.HEAVY_RETRY_MAX_S = 5;
window.HEAVY_DEADLINE_MS = 30000;

window.heavyFetch = async function heavyFetch(url, opts) {
  opts = opts || {};
  var maxAttempts = opts.maxAttempts || 30;
  var deadlineMs = opts.deadlineMs || window.HEAVY_DEADLINE_MS;
  var started = Date.now();
  for (var i = 0; i < maxAttempts; i++) {
    var r = await fetch(url, opts);
    if (r.status !== 503) return r;
    var ra = parseInt(r.headers.get("Retry-After") || "1", 10);
    if (!(ra > 0)) ra = window.HEAVY_RETRY_MIN_S;
    if (ra > window.HEAVY_RETRY_MAX_S) ra = window.HEAVY_RETRY_MAX_S;
    var left = deadlineMs - (Date.now() - started);
    if (left <= 0) break;
    await new Promise(function (res) { setTimeout(res, Math.min(ra * 1000, left)); });
  }
  throw new Error("heavyFetch: still busy after "
    + Math.round((Date.now() - started) / 1000) + " s / " + maxAttempts
    + " attempts: " + url);
};

/* #PERF-2: скачивание HEAVY-эндпоинта. Через location.href нельзя: при занятых
   воротах браузер уйдёт на страницу с 503-JSON вместо файла, и пользователь
   потеряет и выгрузку, и текущее состояние вкладки. Имя файла берём из
   Content-Disposition — там уже учтён пользовательский префикс выгрузок. */
window.heavyDownload = async function heavyDownload(url, fallbackName) {
  var r = await window.heavyFetch(url);
  if (!r.ok) throw new Error("HTTP " + r.status + ": " + url);
  var name = fallbackName || url.split("/").pop();
  var cd = r.headers.get("Content-Disposition") || "";
  /* RFC 6266: если присланы оба параметра, filename* побеждает голый filename.
     Прошивка сейчас отдаёт только ASCII (префикс выгрузок режется до
     [A-Za-z0-9_-] в boot_config.c), так что второй вариант и есть рабочий,
     но порядок проверки важен для любого другого источника заголовка. */
  var m = /filename\*=\s*(?:UTF-8'')?"?([^";]+)"?/i.exec(cd)
       || /filename=\s*"?([^";]+)"?/i.exec(cd);
  if (m) { try { name = decodeURIComponent(m[1]); } catch (e) { name = m[1]; } }
  var href = URL.createObjectURL(await r.blob());
  var a = document.createElement("a");
  a.href = href;
  a.download = name;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(function () { URL.revokeObjectURL(href); }, 10000);
  return name;
};

/* #MX-13 (1.2.34, #62): средний CPS за набор = импульсы / время (секунды); null, если время не конечное число или <= 0, либо импульсы не число. Единственное место формулы: главная делит на реальное время (как SMA), «Мониторинг» — на живое. Имя у функции — для test/web/undef_calls_test.mjs */
window.avgCps = function avgCps(n, l) {
  return (typeof n === "number" && typeof l === "number" && isFinite(n) && isFinite(l) && l > 0) ? n / l : null;
};

/* #LOAD-2 (OPT, этап 1): периодический опрос, который молчит в скрытой вкладке. Пока document.hidden - вызовы fn пропускаются
   (не ошибка, счётчики сбоев страниц не трогаются); при возврате вкладки fn вызывается сразу, затем снова по интервалу.
   Без Page Visibility API (document.hidden === undefined) ведёт себя как обычный setInterval. Возвращает id таймера (clearInterval);
   слушатель visibilitychange остаётся - у вызывающих страниц опросы не отменяются. Имя у функции - для test/web/undef_calls_test.mjs */
window.pollWhileVisible = function pollWhileVisible(fn, ms) {
  document.addEventListener("visibilitychange", function () { if (!document.hidden) fn(); });
  return setInterval(function () { if (!document.hidden) fn(); }, ms);
};

/* #OPT-12 (OPT, этап 1): единственная реализация CSRF-обёрток (раньше loadCsrf/post - 4 копии, gget - 2). Скрипт подключается ДО inline-скрипта
   страницы: страницы зовут loadCsrf() в первой же строке старта. csrfToken - общая глобальная переменная (system: XHR-загрузка прошивки,
   waterfall: backfill читают её напрямую). Имена функций - для test/web/undef_calls_test.mjs; тест ретрая - test/web/csrf_retry_test.mjs. */
var csrfToken = "";
async function loadCsrf(){try{var r=await(await fetch("/api/csrf-token")).json();csrfToken=r.token||""}catch(e){}}
/* CSRF-1: токен меняется при каждом старте платы — вкладка, открытая до перезагрузки, получала 403 молча. На 403 берём новый токен и повторяем один раз. */
async function post(url,opts){opts=opts||{};opts.method="POST";var h=opts.headers||{};opts.headers=Object.assign({},h,{"X-CSRF-Token":csrfToken});var r=await fetch(url,opts);if(r.status===403){await loadCsrf();opts.headers=Object.assign({},h,{"X-CSRF-Token":csrfToken});r=await fetch(url,opts)}return r}
/* S-01 (1.2.30): тяжёлые GET требуют CSRF-токен; токен меняется при каждом старте платы — на 403 берём новый и повторяем один раз. */
async function gget(url,opts){opts=opts||{};if(!csrfToken)await loadCsrf();var f=function(){return fetch(url,Object.assign({},opts,{headers:{"X-CSRF-Token":csrfToken}}))};var r=await f();if(r.status===403){await loadCsrf();r=await f();}return r;}

/* #OPT-13 (OPT, этап 1): общий каркас i18n. Словарь DICT = {ru:{...},en:{...}} остаётся на странице и передаётся в i18nInit(DICT,{onApply,onLang}).
   t()/tr()/tfmt() ищут ключ в языке LANG, затем в ru, затем возвращают сам ключ. onApply() вызывается в конце applyI18n (страничные хвосты: заголовок,
   подписи кнопок), onLang(l) - в конце setLang (перерисовка страницы). Ключ localStorage - "aswf-lang" (один на все страницы). Атрибуты разметки:
   data-i18n (textContent), data-i18n-html (innerHTML), data-i18n-placeholder, data-i18n-title. Имена функций - для test/web/undef_calls_test.mjs. */
var LANG = (function () { try { return localStorage.getItem("aswf-lang") || "ru"; } catch (e) { return "ru"; } })();
var I18N = { dict: null, onApply: null, onLang: null };
function i18nInit(dict, hooks) {
    I18N.dict = dict;
    hooks = hooks || {};
    I18N.onApply = hooks.onApply || null;
    I18N.onLang = hooks.onLang || null;
}
function tr(k) {
    var D = I18N.dict || { ru: {} };
    var d = D[LANG] || D.ru;
    return (d[k] !== undefined) ? d[k] : (D.ru[k] || k);
}
function t(k) { return tr(k); }
function tfmt(k) {
    var s = tr(k);
    for (var i = 1; i < arguments.length; i++) s = s.replace("%" + i, arguments[i]);
    return s;
}
function applyI18n() {
    var els, el, key, i;
    els = document.querySelectorAll("[data-i18n]");
    for (i = 0; i < els.length; i++) { el = els[i]; key = el.getAttribute("data-i18n"); el.textContent = tr(key); }
    els = document.querySelectorAll("[data-i18n-html]");
    for (i = 0; i < els.length; i++) { el = els[i]; key = el.getAttribute("data-i18n-html"); el.innerHTML = tr(key); }
    els = document.querySelectorAll("[data-i18n-placeholder]");
    for (i = 0; i < els.length; i++) { el = els[i]; key = el.getAttribute("data-i18n-placeholder"); el.placeholder = tr(key); }
    els = document.querySelectorAll("[data-i18n-title]");
    for (i = 0; i < els.length; i++) { el = els[i]; key = el.getAttribute("data-i18n-title"); el.title = tr(key); }
    document.documentElement.lang = LANG;
    var b = document.getElementById("langBtn");
    if (b) b.textContent = (LANG === "ru") ? "EN" : "RU";
    if (I18N.onApply) I18N.onApply();
}
function setLang(l) {
    if (l !== "ru" && l !== "en") l = "ru";
    LANG = l;
    try { localStorage.setItem("aswf-lang", l); } catch (e) {}
    applyI18n();
    if (I18N.onLang) I18N.onLang(l);
}

/* #OPT-11 (OPT, этап 1): единственная реализация форматирования времени/длительности, локали и пояса отображения (раньше 5 функций длительности, pad2 x4, жёсткий ru-RU, пояс читала только вкладка «Мониторинг»). Тест: test/web/time_fmt_test.mjs */

// Форматирование числа с ведущим нулём
function pad2(n) {
    return n < 10 ? "0" + n : "" + n;
}

// Возврат локали в зависимости от языка
function lcl() {
    return LANG === "ru" ? "ru-RU" : "en-US";
}

// Форматирование числа с разделителями
function fmtNum(n) {
    return Math.round(n).toLocaleString(lcl());
}

// Единицы измерения длительности
var DUR_UNITS = {
    ru: { d: "д", h: "ч", m: "м", s: "с" },
    en: { d: "d", h: "h", m: "m", s: "s" }
};

// Форматирование длительности словами
function fmtDur(s) {
    if (typeof s !== "number" || !isFinite(s)) {
        return "\u2014";
    }
    s = Math.max(0, Math.round(s));
    var u = DUR_UNITS[LANG] || DUR_UNITS.ru;
    var d = Math.floor(s / 86400);
    var h = Math.floor((s % 86400) / 3600);
    var m = Math.floor((s % 3600) / 60);
    var x = s % 60;
    var p = [];
    if (d > 0) p.push(d + u.d);
    if (d > 0 || h > 0) p.push(h + u.h);
    if (d > 0 || h > 0 || m > 0) p.push(m + u.m);
    p.push(x + u.s);
    return p.join(" ");
}

// Форматирование длительности в формате ЧЧ:ММ:СС
function fmtClock(s) {
    if (typeof s !== "number" || !isFinite(s)) {
        return "--:--:--";
    }
    s = Math.max(0, Math.floor(s));
    var u = DUR_UNITS[LANG] || DUR_UNITS.ru;
    var d = Math.floor(s / 86400);
    var prefix = d > 0 ? d + u.d + " " : "";
    return prefix + pad2(Math.floor((s % 86400) / 3600)) + ":" + pad2(Math.floor((s % 3600) / 60)) + ":" + pad2(s % 60);
}

// Ключ для хранения часового пояса
var TZ_KEY = "aswf-tz";

// Получение режима часового пояса
function tzMode() {
    try {
        return localStorage.getItem(TZ_KEY) || "auto";
    } catch (e) {
        return "auto";
    }
}

// Установка часового пояса
function tzSet(v) {
    try {
        localStorage.setItem(TZ_KEY, v);
    } catch (e) {}
}

// Сдвиг даты на часовой пояс
function tzShift(d) {
    var m = tzMode();
    if (m === "auto") return d;
    var off = parseInt(m, 10);
    if (!isFinite(off)) return d;
    return new Date(d.getTime() + (off + d.getTimezoneOffset()) * 60000);
}

// Получение смещения часового пояса в минутах
function tzOffMin() {
    var m = tzMode();
    if (m !== "auto") {
        var o = parseInt(m, 10);
        if (isFinite(o)) return o;
    }
    return -new Date().getTimezoneOffset();
}

// Формирование метки часового пояса
function tzOffLabel(min) {
    var s = min < 0 ? "-" : "+";
    var a = Math.abs(min);
    return "UTC" + s + pad2(Math.floor(a / 60)) + ":" + pad2(a % 60);
}

// Получение метки текущего часового пояса
function tzLabel() {
    return tzOffLabel(tzOffMin());
}

// Форматирование времени ЧЧ:ММ:СС
function hms(d) {
    d = tzShift(d);
    return pad2(d.getHours()) + ":" + pad2(d.getMinutes()) + ":" + pad2(d.getSeconds());
}

// Форматирование даты и времени локально
function isoLocal(d) {
    d = tzShift(d);
    return d.getFullYear() + "-" + pad2(d.getMonth() + 1) + "-" + pad2(d.getDate()) + " " + pad2(d.getHours()) + ":" + pad2(d.getMinutes()) + ":" + pad2(d.getSeconds());
}

// Форматирование даты и времени по метке времени
function fmtDateTime(ms) {
    return tzShift(new Date(ms)).toLocaleString(lcl());
}

// Форматирование времени суток
function fmtTimeOfDay(d) {
    return tzShift(d).toLocaleTimeString(lcl());
}

/* #OPT-9 (OPT, этап 1): единственная реализация энергетического полинома E(ch), обратной функции и проверки монотонности (раньше ch2kev x3 через Math.pow, kev2ch x2 с разными диапазонами и 30 итерациями, calInv 45 итераций, проверка монотонности x2). Блок CAL-BEGIN..END вырезают тесты rebin_test, import_parse_test, cal_poly_test. */
/*CAL-BEGIN*/
function calE(c, x) {
    var e = 0;
    for (var i = c.length - 1; i >= 0; i--) e = e * x + c[i];
    return e;
}

function calMono(c, n) {
    var p = -Infinity;
    for (var j = 0; j <= 64; j++) {
        var e = calE(c, -0.5 + j * n / 64);
        if (!(e > p)) return false;
        p = e;
    }
    return true;
}

function calSame(a, b) {
    var p = [0, 2048, 4096, 6144, 8191];
    for (var i = 0; i < p.length; i++) {
        if (!(Math.abs(calE(a, p[i]) - calE(b, p[i])) < 0.5)) return false;
    }
    return true;
}

function calInv(c, E, n, lo, hi) {
    // диапазон по умолчанию - границы ячеек шкалы; оси страниц передают lo=0, hi=n
    if (lo === undefined) lo = -0.5;
    if (hi === undefined) hi = n - 0.5;
    for (var i = 0; i < 45; i++) {
        var m = (lo + hi) / 2;
        if (calE(c, m) < E) lo = m;
        else hi = m;
    }
    return (lo + hi) / 2;
}

function calKev(c, ch) {
    if (c) return calE(c, ch);
    return ch;
}

function calChan(c, kev, n) {
    return calInv(c || [0, 1], kev, n, 0, n);
}
/*CAL-END*/
