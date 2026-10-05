// 1.2.33 (#AWF-UI-5): оверлей — 1 строка буфера = 1 CSS px по высоте, без растяжения; маркеры только в области данных. Usage: node test/web/wf_overlay_test.mjs [путь]
import { readFileSync } from "node:fs";
const page = readFileSync(process.argv[2] || "web/waterfall.html", "utf8");
let failed = 0;
const ok = (name, c) => { console.log((c ? "OK   " : "FAIL ") + name); if (!c) failed++; };
const blk = page.match(/\/\*WFH-MATH-BEGIN[\s\S]*?WFH-MATH-END\*\//);
const M = new Function(blk[0] + "; return {wfOverlaySize,wfZoomLayout,wfDataH};")();
const z = M.wfOverlaySize(1100, 800, 900, 180);
ok("размер 1100x800: w=1078, h=586 (область под срезом 192+10)", z.w === 1078 && z.h === 586 && Math.abs(z.regionH - 598) < 1e-9);
const m = M.wfOverlaySize(375, 812, 900, 180);
ok("размер 375x812: w=367, h=714", m.w === 367 && m.h === 714);
let bad = 0, cnt = 0;   // раскладка: на экране 1 строка = 1 CSS px, 1 канал-столбец w px; bitmap вмещается, центрирован
for (const [W, H] of [[375, 812], [1100, 800], [1920, 1080], [320, 480]]) for (const k of [1, 1.5, 2, 3]) {
  const s = M.wfOverlaySize(W, H, 900, 180), L = M.wfZoomLayout(W, H, Math.round(s.w * k), Math.round(s.h * k), 900, 180, k);
  cnt++; if (Math.abs(L.totH - s.h) > 1 / k || Math.abs(L.Wd - s.w) > 1 / k || L.totH > L.regionH + 1e-6 || L.Wd > W || Math.abs(L.ox * 2 + L.Wd - W) > 1e-6) bad++;
}
ok("totH == число строк h (1 строка = 1 px), Wd == w, вмещается, по центру: " + cnt + " конфигураций", bad === 0);
ok("без среза: gap=0, slcH=0", M.wfZoomLayout(1100, 800, 1000, 500, 0, 0, 1).slcH === 0 && M.wfZoomLayout(1100, 800, 1000, 500, 0, 0, 1).gap === 0);
ok("wfDataH: 15 строк -> 15; 600 -> 520; пусто -> 0; baseIndex 40", M.wfDataH(14, 0, 520) === 15 && M.wfDataH(599, 0, 520) === 520 && M.wfDataH(-1, 0, 520) === 0 && M.wfDataH(54, 40, 520) === 15);
ok("нуклиды: линии до dH, подпись, не влезающая в dH, пропускается", /g\.moveTo\(x,0\); g\.lineTo\(x,dH\)/.test(page) && /if\(ly\+10>dH\)\{g\.globalAlpha=1;continue;\}/.test(page));
ok("оверлей строит bitmap сам (ovBitmap), compose удалён, sourcesFn(W,H)", /function ovBitmap\(W,H\)/.test(page) && !/compose\(/.test(page) && /sourcesFn\(W,H\)/.test(page));
if (failed) { console.log("FAILED: " + failed); process.exit(1); }
console.log("ALL OK");
