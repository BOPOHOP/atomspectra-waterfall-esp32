// 1.2.33: правки по проходу A (audit/sterile-1.2.33-passA.md) №3 (reclamp viewBottom) и №6 (updGrip из setLang). Usage: node test/web/wf_passA_test.mjs [путь]
import { readFileSync } from "node:fs";
const page = readFileSync(process.argv[2] || "web/waterfall.html", "utf8");
let failed = 0;
const ok = (name, c) => { console.log((c ? "OK   " : "FAIL ") + name); if (!c) failed++; };
const M = new Function(page.match(/\/\*WFH-MATH-BEGIN[\s\S]*?WFH-MATH-END\*\//)[0] + "; return {wfhReclamp,wfhWin,wfhRowAt};")();
const n = 542, last = n - 1;   // воспроизведение прохода A: окно 160, вид на самой старой допустимой строке (159), затем окно 900
ok("№3: окно 160, vb=159 допустимо (пол = 159)", M.wfhReclamp(159, last, 0, n, 160) === 159);
ok("№3: после 160->900 viewBottom -> live (-1), не 159", M.wfhReclamp(159, last, 0, n, 900) === -1);
let dark = 0; for (let y = 0; y < 900; y++) { const li = M.wfhRowAt(y, last, 0); if (li < 0 || li >= n) dark++; }
ok("№3: при live тёмных строк = 900-542 = 358 (было 740)", dark === 358);
ok("№3: 160->300 при vb=200: пол 299 -> vb=299; слайдер mx=n-win=242", M.wfhReclamp(200, last, 0, n, 300) === 299 && n - M.wfhWin(n, 300) === 242);
ok("№3: live остаётся live; пустой буфер; baseIndex 40", M.wfhReclamp(-1, last, 0, n, 900) === -1 && M.wfhReclamp(5, -1, 0, 0, 300) === -1 && M.wfhReclamp(100, 581, 40, 542, 300) === 339);
ok("№3: setWFH вызывает wfhReclamp", /viewBottom=wfhReclamp\(viewBottom,baseIndex\+rows\.length-1,baseIndex,rows\.length,h\)/.test(page));
const sl = page.match(/function setLang\(l\)\{[\s\S]*?\n\}/);   // №6: реальный код setLang со стабами
let grip = 0, lang = "";
new Function("localStorage", "applyI18n", "updGrip", "draw", "set", sl[0] + "; setLang('en'); set(LANG);")({ setItem() {} }, () => {}, () => grip++, () => {}, (v) => (lang = v));
ok("№6: setLang('en') вызывает updGrip и меняет язык", grip === 1 && lang === "en");
if (failed) { console.log("FAILED: " + failed); process.exit(1); }
console.log("ALL OK");
