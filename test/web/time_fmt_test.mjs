// OPT-11: shared time/duration/locale/timezone helpers in common-time.js (pad2, lcl, fmtNum, fmtDur, fmtClock, tz*, hms, isoLocal, fmtDateTime, fmtTimeOfDay). Usage: node test/web/time_fmt_test.mjs [path-to-common-time.js]
process.env.TZ = "UTC";
import { readFileSync } from "node:fs";
const SRC = readFileSync(process.argv[2] || "web/common-time.js", "utf8");
let failed = 0;
function check(name, ok) {
  if (ok) console.log("OK   " + name);
  else { console.log("FAIL " + name); failed++; }
}
function make(store) {
  const localStorage = {
    getItem: (k) => (k in store ? store[k] : null),
    setItem: (k, v) => { store[k] = String(v); }
  };
  const api = new Function("document", "localStorage", "fetch", "setInterval", "window", "console",
    SRC + "\n;return { pad2, lcl, fmtNum, fmtDur, fmtClock, tzMode, tzSet, tzShift, tzOffMin, tzOffLabel, tzLabel, hms, isoLocal, fmtDateTime, fmtTimeOfDay, setLang, i18nInit };"
  )({
    readyState: "complete", addEventListener() {}, documentElement: { lang: "" },
    getElementById() { return null; }, querySelectorAll() { return []; }
  }, localStorage, async () => ({ json: async () => ({}) }), () => 0, {}, { log() {} });
  return { api, store };
}
const ru = make({ "aswf-lang": "ru" });
const en = make({ "aswf-lang": "en" });
const { pad2, lcl, fmtNum, fmtDur, fmtClock, tzMode, tzSet, tzShift, tzOffMin, tzOffLabel, tzLabel, hms, isoLocal, fmtDateTime, fmtTimeOfDay } = ru.api;
check("pad2", pad2(0) === "00" && pad2(9) === "09" && pad2(10) === "10" && pad2(59) === "59");
check("lcl", lcl() === "ru-RU" && en.api.lcl() === "en-US");
const ruNum = fmtNum(1234567.4);
check("fmtNum", ruNum.replace(/\s/g, "") === "1234567" && /\s/.test(ruNum) && en.api.fmtNum(1234567.4) === "1,234,567");
check("fmtDur ru", fmtDur(0) === "0с" && fmtDur(5) === "5с" && fmtDur(65) === "1м 5с" && fmtDur(3600) === "1ч 0м 0с" && fmtDur(3605) === "1ч 0м 5с" && fmtDur(90000) === "1д 1ч 0м 0с" && fmtDur(692524) === "8д 0ч 22м 4с" && fmtDur(59.6) === "1м 0с" && fmtDur(-5) === "0с" && fmtDur(NaN) === "—" && fmtDur(Infinity) === "—" && fmtDur("7") === "—" && fmtDur(null) === "—");
check("fmtDur en", en.api.fmtDur(3605) === "1h 0m 5s" && en.api.fmtDur(90000) === "1d 1h 0m 0s" && en.api.fmtDur(65) === "1m 5s");
check("fmtClock ru", fmtClock(5) === "00:00:05" && fmtClock(3605) === "01:00:05" && fmtClock(86399) === "23:59:59" && fmtClock(93600) === "1д 02:00:00" && fmtClock(5.9) === "00:00:05" && fmtClock(-3) === "00:00:00" && fmtClock(NaN) === "--:--:--" && fmtClock("x") === "--:--:--");
check("fmtClock en", en.api.fmtClock(93600) === "1d 02:00:00");
check("tzMode", tzMode() === "auto" && (tzSet("180"), ru.store["aswf-tz"] === "180" && tzMode() === "180"));
const badStore = {};
const badLS = { getItem: () => { throw new Error("x"); }, setItem: () => {} };
const badApi = new Function("document", "localStorage", "fetch", "setInterval", "window", "console", SRC + "\n;return { tzMode, tzSet };")({ readyState: "complete", addEventListener() {}, documentElement: { lang: "" }, getElementById() { return null; }, querySelectorAll() { return []; } }, badLS, async () => ({ json: async () => ({}) }), () => 0, {}, { log() {} });
check("tzMode bad", badApi.tzMode() === "auto" && (badApi.tzSet("1"), true));
check("tzOffLabel", tzOffLabel(180) === "UTC+03:00" && tzOffLabel(-330) === "UTC-05:30" && tzOffLabel(0) === "UTC+00:00" && tzOffLabel(840) === "UTC+14:00" && tzOffLabel(-720) === "UTC-12:00");
check("tzOffMin", (tzSet("auto"), true) && tzOffMin() === 0 && tzLabel() === "UTC+00:00" && (tzSet("180"), tzOffMin() === 180 && tzLabel() === "UTC+03:00") && (tzSet("-330"), tzOffMin() === -330) && (tzSet("abc"), tzOffMin() === 0));
const d = new Date(Date.UTC(2026, 0, 5, 12, 34, 56));
check("hms auto", (tzSet("auto"), true) && hms(d) === "12:34:56" && isoLocal(d) === "2026-01-05 12:34:56");
check("hms 180", (tzSet("180"), true) && hms(d) === "15:34:56" && isoLocal(d) === "2026-01-05 15:34:56");
const d2 = new Date(Date.UTC(2026, 0, 1, 3, 0, 0));
check("hms -330", (tzSet("-330"), true) && isoLocal(d2) === "2025-12-31 21:30:00");
check("hms abc", (tzSet("abc"), true) && hms(d) === "12:34:56");
check("tzShift", (tzSet("auto"), true) && tzShift(d) === d);
check("fmtDateTime ru", (tzSet("180"), true) && fmtDateTime(Date.UTC(2026, 0, 5, 12, 34, 56)) === "05.01.2026, 15:34:56");
check("fmtDateTime ru auto", (tzSet("auto"), true) && fmtDateTime(Date.UTC(2026, 0, 5, 12, 34, 56)) === "05.01.2026, 12:34:56");
const enRes = en.api.fmtDateTime(Date.UTC(2026, 0, 5, 12, 34, 56));
check("fmtDateTime en", /12:34:56/.test(enRes) && /1\/5\/2026/.test(enRes));
check("fmtTimeOfDay", (tzSet("180"), true) && fmtTimeOfDay(d) === "15:34:56" && (tzSet("auto"), fmtTimeOfDay(d) === "12:34:56"));
process.env.TZ = "America/New_York";   // пояс процесса не UTC: заданное смещение не должно зависеть от пояса браузера
check("tz offset independent of process TZ", (tzSet("180"), true) && hms(d) === "15:34:56" && isoLocal(d) === "2026-01-05 15:34:56" && (tzSet("auto"), tzOffMin() === -new Date().getTimezoneOffset()));
process.env.TZ = "UTC";
console.log("failed: " + failed);
process.exit(failed ? 1 : 0);
