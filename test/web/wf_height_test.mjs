// 1.2.33 (#AWF-UI-1): высота окна и масштаб строк 1:1 (блок WFH-MATH в web/waterfall.html). Usage: node test/web/wf_height_test.mjs [путь]
// Мутанты: test/web/mutate_wf_height.sh. Эталон 1:1 — формула 1.2.31 (git 04f304e): gIdx=bottom-y, растяжения нет.
import { readFileSync } from "node:fs";
const page = readFileSync(process.argv[2] || "web/waterfall.html", "utf8");
let failed = 0;
const ok = (name, c) => { console.log((c ? "OK   " : "FAIL ") + name); if (!c) failed++; };
const blk = page.match(/\/\*WFH-MATH-BEGIN[\s\S]*?WFH-MATH-END\*\//);
ok("блок WFH-MATH найден", !!blk);
const M = new Function(blk[0] + "; return {wfhClamp,wfhParse,wfhRowAt,wfhWin};")();
ok("clamp: 159->160, 160, 520, 900, 901->900, 300.4->300", [159, 160, 520, 900, 901, 300.4].map(M.wfhClamp).join() === "160,160,520,900,900,300");
ok("clamp: NaN/undefined/Infinity -> умолчание 520", [NaN, undefined, Infinity].every((v) => M.wfhClamp(v) === 520));
ok("parse: null/пусто/мусор/520px/-5/1e3/12345 -> 520", [null, "", "abc", "520px", "-5", "1e3", "12345", 300].every((s) => M.wfhParse(s) === 520));
ok("parse: '300'->300, '50'->160 (граница), '9999'->900 (граница)", M.wfhParse("300") === 300 && M.wfhParse("50") === 160 && M.wfhParse("9999") === 900);
ok("растяжения нет: в коде нет visRows/wfhVisRows/MINROWS", !/visRows|VisRows|MINROWS/i.test(page));
let same = true, cnt = 0;   // формула 1.2.31 при любом числе строк и высоте
for (const h of [160, 520, 900]) for (const bi of [0, 40]) for (const n of [1, 15, 64, 256, 600]) for (let y = 0; y < h; y += 0.5) { cnt++; if (M.wfhRowAt(y, bi + n - 1, bi) !== Math.floor(bi + n - 1 - Math.floor(y)) - bi) same = false; }
ok("wfhRowAt == формула 1.2.31 на " + cnt + " точках (h 160/520/900, n 1..600)", same);
ok("15 строк: y=0 -> 14 (новейшая), y=14 -> 0, y=15 -> -1 (тёмное), y=519 -> <0", M.wfhRowAt(0, 14, 0) === 14 && M.wfhRowAt(14, 14, 0) === 0 && M.wfhRowAt(15, 14, 0) === -1 && M.wfhRowAt(519, 14, 0) < 0);
ok("win: min(h,n): (15,520)->15, (300,160)->160, (300,520)->300", M.wfhWin(15, 520) === 15 && M.wfhWin(300, 160) === 160 && M.wfhWin(300, 520) === 300);
if (failed) { console.log("FAILED: " + failed); process.exit(1); }
console.log("ALL OK");
