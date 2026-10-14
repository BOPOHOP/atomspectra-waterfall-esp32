Write a Node.js ES module test script (file `test/web/i18n_core_test.mjs`). Output ONLY the JavaScript code, no markdown fences, no explanations.

Purpose: `web/common-time.js` (a classic browser script) defines the shared i18n skeleton as top-level globals: `var LANG` (read from `localStorage` key "aswf-lang", default "ru"), `var I18N`, `function i18nInit(dict, hooks)` (hooks: `onApply`, `onLang`, both optional), `function tr(k)`, `function t(k)`, `function tfmt(k, ...args)`, `function applyI18n()`, `function setLang(l)`. Test them with browser stubs.

Requirements:
1. Imports: `readFileSync` from "node:fs". `process.argv[2]` = path to the JS file (default `web/common-time.js`). Run from repo root. First comment line: `// OPT-13: shared i18n skeleton in common-time.js (tr/t/tfmt fallback chain, applyI18n attributes, setLang validation/storage/hooks). Usage: node test/web/i18n_core_test.mjs [path-to-common-time.js]`.
2. Build the environment from the file text with `new Function("document", "localStorage", "fetch", "setInterval", "window", "console", SRC + "\n;return { i18nInit, tr, t, tfmt, applyI18n, setLang, getLang: function () { return LANG; } };")`. The stubs: `store = {}`; `localStorage = { getItem: k => (k in store ? store[k] : null), setItem: (k, v) => { store[k] = String(v); } }`; `fetch = async () => ({ json: async () => ({}) })`; `setInterval = () => 0`; `window = {}`; `console = { log() {} }`; `document` with `readyState: "complete"`, `addEventListener() {}`, `documentElement: { lang: "" }`, `getElementById(id)` returning the stub element `langBtn` (`{ textContent: "" }`) for id "langBtn" and null otherwise, and `querySelectorAll(sel)` returning an array of stub elements selected by the attribute selector: keep a list of 4 stub elements `{ attrs: {"data-i18n": "a"}, textContent: "", innerHTML: "", placeholder: "", title: "", getAttribute(n) { return this.attrs[n]; } }` with attribute names `data-i18n`, `data-i18n-html`, `data-i18n-placeholder`, `data-i18n-title` (keys "a", "b", "c", "d"); `querySelectorAll("[data-i18n]")` returns the element that has the attribute `data-i18n` (match `[name]` exactly), and so on. The `store` must be able to be pre-filled before building the environment (build a function `make(initialStore)`), because LANG is read at load time.
3. Dictionary for the tests: `DICT = { ru: { a: "А", b: "<b>Б</b>", c: "В", d: "Г", only_ru: "только ру", fmt: "x=%1 y=%2" }, en: { a: "A", b: "<b>B</b>", c: "C", d: "D", fmt: "X=%1 Y=%2" } }`.
4. Checks (collect with `check(name, ok)` printing `OK   <name>` or `FAIL <name>` and counting failures):
   - `LANG default ru` (empty store) and `LANG from storage en` (store has "aswf-lang" = "en").
   - `tr before i18nInit returns key`: `tr("zzz") === "zzz"` (no dictionary yet, must not throw).
   - After `i18nInit(DICT)` in ru: `tr("a") === "А"`, `t("a") === "А"`, `tr("missing") === "missing"`.
   - In en: `tr("a") === "A"`, fallback to ru for a key missing in en: `tr("only_ru") === "только ру"`.
   - `tfmt("fmt", 1, 2) === "x=1 y=2"` in ru and `"X=1 Y=2"` in en.
   - `setLang("en")`: `getLang() === "en"`, `store["aswf-lang"] === "en"`, `onLang` hook called once with "en", `onApply` hook called once, `document.documentElement.lang === "en"`, langBtn text is "RU"; after `setLang("ru")` langBtn text is "EN".
   - `setLang("xx")` falls back to "ru" (`getLang() === "ru"` and storage "ru").
   - `applyI18n` applies the four attributes: after `setLang("en")` the four stub elements hold textContent "A", innerHTML "<b>B</b>", placeholder "C", title "D".
   - `i18nInit(DICT)` without hooks does not break `setLang` (no exception).
5. Print `failed: <n>` at the end and `process.exit(n ? 1 : 0)`. Keep it under 90 lines.
