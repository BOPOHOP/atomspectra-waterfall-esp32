// 1.2.32 (pass3B #3): drawCursor и mousemove обязаны переводить y в строку одной формулой с растяжением visRows().
// Usage: node test/web/cursor_row_test.mjs [путь к waterfall.html]. Мутант: test/web/mutate_cursor_row.sh
import { readFileSync } from "node:fs";
const page = readFileSync(process.argv[2] || "web/waterfall.html", "utf8");
let failed = 0;
function ok(name, c) { console.log((c ? "OK   " : "FAIL ") + name); if (!c) failed++; }
const vr = page.match(/function visRows\(\)\{[^\n]*\}/);
const dc = page.match(/function drawCursor\(\)\{[\s\S]*?\n\}/);
const mm = page.match(/var li=Math\.floor\(bottom-Math\.floor\(y\*visRows\(\)\/WFHd\)\)-baseIndex;/);
ok("visRows, drawCursor и формула mousemove найдены", !!(vr && dc && mm));
const m = dc && dc[0].match(/var li=([^;]+);/);
ok("drawCursor: li считается через visRows()", !!(m && /visRows\(\)/.test(m[1])));
if (vr && m) {
  const f = new Function("rows", "WFHd", "bottom", "baseIndex", "y", vr[0] + "; return " + m[1] + ";");
  const WFHd = 520, rows256 = new Array(256), rows600 = new Array(600);
  ok("256 строк, y=200 -> строка 98 (растяжение 256/520)", f(rows256, WFHd, 255, 0, 200) === 255 - Math.floor(200 * 256 / 520));
  ok("256 строк, y=519 (низ) -> строка 0 (не вне диапазона)", f(rows256, WFHd, 255, 0, 519) === 255 - Math.floor(519 * 256 / 520) && f(rows256, WFHd, 255, 0, 519) >= 0);
  ok("600 строк (visRows==WFHd): прежнее (bottom-y)-baseIndex", f(rows600, WFHd, 599, 40, 100) === 599 - 100 - 40);
}
if (failed) { console.log("FAILED: " + failed); process.exit(1); }
console.log("ALL OK");
