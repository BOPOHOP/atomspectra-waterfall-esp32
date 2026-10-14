// OPT-13: shared i18n skeleton in common-time.js (tr/t/tfmt fallback chain, applyI18n attributes, setLang validation/storage/hooks). Usage: node test/web/i18n_core_test.mjs [path-to-common-time.js]
import { readFileSync } from "node:fs";
const SRC = readFileSync(process.argv[2] || "web/common-time.js", "utf8");
const DICT = { ru: { a: "А", b: "<b>Б</b>", c: "В", d: "Г", only_ru: "только ру", fmt: "x=%1 y=%2" }, en: { a: "A", b: "<b>B</b>", c: "C", d: "D", fmt: "X=%1 Y=%2" } };
let failed = 0;
const check = (n, ok) => { console.log((ok ? "OK   " : "FAIL ") + n); if (!ok) failed++; };
function make(initialStore) {
  const store = { ...initialStore };
  const localStorage = { getItem: k => (k in store ? store[k] : null), setItem: (k, v) => { store[k] = String(v); } };
  const fetch = async () => ({ json: async () => ({}) });
  const setInterval = () => 0;
  const window = {};
  const console = { log() {} };
  const langBtn = { textContent: "" };
  const stubs = [
    { attrs: { "data-i18n": "a" }, textContent: "", innerHTML: "", placeholder: "", title: "", getAttribute(n) { return this.attrs[n]; } },
    { attrs: { "data-i18n-html": "b" }, textContent: "", innerHTML: "", placeholder: "", title: "", getAttribute(n) { return this.attrs[n]; } },
    { attrs: { "data-i18n-placeholder": "c" }, textContent: "", innerHTML: "", placeholder: "", title: "", getAttribute(n) { return this.attrs[n]; } },
    { attrs: { "data-i18n-title": "d" }, textContent: "", innerHTML: "", placeholder: "", title: "", getAttribute(n) { return this.attrs[n]; } }
  ];
  const document = {
    readyState: "complete",
    addEventListener() {},
    documentElement: { lang: "" },
    getElementById(id) { return id === "langBtn" ? langBtn : null; },
    querySelectorAll(sel) {
      const m = sel.match(/^\[([^\]]+)\]$/);
      if (!m) return [];
      const name = m[1];
      return stubs.filter(s => name in s.attrs);
    }
  };
  const api = new Function("document", "localStorage", "fetch", "setInterval", "window", "console", SRC + "\n;return { i18nInit, tr, t, tfmt, applyI18n, setLang, getLang: function () { return LANG; } };")(document, localStorage, fetch, setInterval, window, console);
  return { api, store, document, langBtn, stubs };
}
let env = make({});
check("LANG default ru", env.api.getLang() === "ru");
env = make({ "aswf-lang": "en" });
check("LANG from storage en", env.api.getLang() === "en");
env = make({});
check("tr before i18nInit returns key", env.api.tr("zzz") === "zzz");
env.api.i18nInit(DICT);
check("tr a ru", env.api.tr("a") === "А");
check("t a ru", env.api.t("a") === "А");
check("tr missing ru", env.api.tr("missing") === "missing");
env.api.setLang("en");
check("tr a en", env.api.tr("a") === "A");
check("tr only_ru fallback", env.api.tr("only_ru") === "только ру");
env.api.setLang("ru");
check("tfmt ru",env.api.tfmt("fmt", 1, 2) === "x=1 y=2");
env.api.setLang("en");
check("tfmt en", env.api.tfmt("fmt", 1, 2) === "X=1 Y=2");
let onLangCalls = [], onApplyCalls = [];
env = make({});
env.api.i18nInit(DICT, { onLang: l => onLangCalls.push(l), onApply: () => onApplyCalls.push(1) });
env.api.setLang("en");
check("setLang en getLang", env.api.getLang() === "en");
check("setLang en store", env.store["aswf-lang"] === "en");
check("onLang called once", onLangCalls.length === 1 && onLangCalls[0] === "en");
check("onApply called once", onApplyCalls.length === 1);
check("documentElement lang en", env.document.documentElement.lang === "en");
check("langBtn RU", env.langBtn.textContent === "RU");
env.api.setLang("ru");
check("langBtn EN", env.langBtn.textContent === "EN");
env.api.setLang("xx");
check("setLang xx fallback ru", env.api.getLang() === "ru" && env.store["aswf-lang"] === "ru");
env.api.setLang("en");
env.api.applyI18n();
check("applyI18n textContent", env.stubs[0].textContent === "A");
check("applyI18n innerHTML", env.stubs[1].innerHTML === "<b>B</b>");
check("applyI18n placeholder", env.stubs[2].placeholder === "C");
check("applyI18n title", env.stubs[3].title === "D");
env = make({});
env.api.i18nInit(DICT);
let noHookOk = true;
try { env.api.setLang("en"); } catch (e) { noHookOk = false; }
check("i18nInit no hooks setLang ok", noHookOk);
console.log("failed: " + failed);
process.exit(failed ? 1 : 0);
