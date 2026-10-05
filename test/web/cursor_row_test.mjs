// 1.2.32 (pass3B #3): drawCursor и mousemove обязаны переводить y в строку одной формулой с растяжением visRows().
// Usage: node test/web/cursor_row_test.mjs [путь к waterfall.html]. Мутант: test/web/mutate_cursor_row.sh
import { readFileSync } from "node:fs";
const page = readFileSync(process.argv[2] || "web/waterfall.html", "utf8");
let failed = 0;
function ok(name, c) { console.log((c ? "OK   " : "FAIL ") + name); if (!c) failed++; }
// 1.2.33 (#AWF-UI-1): формула вынесена в wfhRowAt (блок WFH-MATH), visRows() = wfhVisRows(rows.length,WFHd); mousemove и drawCursor вызывают её одинаково
const vr = page.match(/\/\*WFH-MATH-BEGIN[\s\S]*?WFH-MATH-END\*\//);
const dc = page.match(/function drawCursor\(\)\{[\s\S]*?\n\}/);
const mm = page.match(/var li=wfhRowAt\(y,bottom,baseIndex\);\n var inWf/);
ok("блок WFH-MATH, drawCursor и вызов wfhRowAt в mousemove найдены", !!(vr && dc && mm));
const m = dc && dc[0].match(/var li=([^;]+);/);
ok("drawCursor: li считается через wfhRowAt (та же формула, что mousemove)", !!(m && m[1] === "wfhRowAt(y,bottom,baseIndex)"));
const dr = page.match(/function draw\(\)\{[\s\S]*?\n\}/), ph = page.match(/function paintHeat\([^)]*\)\{[\s\S]*?\n\}/);
ok("draw (нормировка) и paintHeat (заливка): gIdx=bottom-y (1 строка = 1 px, как 1.2.31)", !!dr && !!ph && /var gIdx=bottom-y;/.test(dr[0]) && /var gIdx=bottom-y;/.test(ph[0]));
ok("оверлей: строка = 1 px, fy*ovH (не WFHd, не растяжение)", (page.match(/Math\.floor\(fy\*ovH\)/g) || []).length === 2 && /scrollAcc\+=frac\*ovH;/.test(page));
if (vr && m) {
  const f = new Function("bottom", "baseIndex", "y", vr[0] + "; return " + m[1] + ";");
  ok("256 строк, y=200 -> строка 55 (1:1, без растяжения)", f(255, 0, 200) === 55);
  ok("256 строк, y=256 -> -1 (ниже буфера тёмное)", f(255, 0, 256) === -1);
  ok("600 строк: (bottom-y)-baseIndex", f(599, 40, 100) === 599 - 100 - 40);
}
if (failed) { console.log("FAILED: " + failed); process.exit(1); }
console.log("ALL OK");
