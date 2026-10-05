// 1.2.33 (#AWF-UI-2): подписи оси энергий водопада — целые кэВ без суффикса «k», единица один раз. Usage: node test/web/wf_axis_test.mjs [путь]. Мутанты: test/web/mutate_wf_height.sh
import { readFileSync } from "node:fs";
const page = readFileSync(process.argv[2] || "web/waterfall.html", "utf8");
let failed = 0;
const ok = (name, c) => { console.log((c ? "OK   " : "FAIL ") + name); if (!c) failed++; };
const blk = page.match(/\/\*WFH-MATH-BEGIN[\s\S]*?WFH-MATH-END\*\//), da = page.match(/function drawAxis\(g,w,h\)\{[\s\S]*?\n\}/);
ok("блок WFH-MATH и drawAxis найдены", !!(blk && da));
const num = new Function(blk[0] + "; return wfAxisNum;")();
ok("wfAxisNum: 459.4->'459', 4777.6->'4 778', 0->'0', 999.5->'1 000', -3.2->'-3'", [459.4, 4777.6, 0, 999.5, -3.2].map((v) => num(v).replace("−", "-")).join("|") === "459|4 778|0|1 000|-3");
ok("wfAxisNum: в результате нет буквы k/К", [1, 459, 4778, 12345].every((v) => !/[kKкК]/.test(num(v))));
const ovl = new Function(blk[0] + "; return wfAxisOverlap;")();
const boxes = [];   // боксы подписей последнего run(): {s,l,r}; ширина символа 5.5 px (измерено в проходе A: «4 189» = 27.5 px)
function run(mode, calibOn, w = 900) {   // -> массив подписей fillText, которые рисует drawAxis (ctx-заглушка, шкала 0..8192 кан. = 0..4778 кэВ)
  const out = [], ctx = { textAlign: "center", clearRect() {}, fillRect() {}, beginPath() {}, moveTo() {}, lineTo() {}, stroke() {}, measureText: (s) => ({ width: String(s).length * 5.5 }),
    fillText(s, x) { s = String(s); const wd = s.length * 5.5; out.push(s); boxes.push({ s, l: this.textAlign === "right" ? x - wd : x - wd / 2, r: this.textAlign === "right" ? x : x + wd / 2 }); } };
  boxes.length = 0;
  new Function("curx", "WFW", "WFHd", "chLo", "chHi", "axisMode", "calib", "ch2kev", "wfAxisNum", "wfAxisOverlap", "t", "w", da[0] + "; drawAxis(curx,w,520);")(ctx, w, 520, 0, 8192, mode, calibOn, (c) => c * 4778 / 8192, num, ovl, (k) => ({ "ax.kev": "кэВ" })[k] || k, w);
  return out;
}
for (const w of [313, 367, 530, 752, 900, 1078]) { run("kev", true, w); const b = boxes.slice().sort((p, q) => p.l - q.l); let gap = 1e9; for (let i = 1; i < b.length; i++) gap = Math.min(gap, b[i].l - b[i - 1].r);
  ok("w=" + w + ": подписи оси не пересекаются (мин. зазор " + gap.toFixed(1) + " px >= 2), последняя 'кэВ' на месте", gap >= 2 && /кэВ$/.test(b[b.length - 1].s)); }
ok("перекрытие (проход A): 313/367 -> предпоследний скрыт (8 подписей), 900 -> все 9", (run("kev", true, 313), boxes.length === 8) && (run("kev", true, 367), boxes.length === 8) && (run("kev", true, 900), boxes.length === 9));
ok("wfAxisOverlap: 27.5/49.5 при w=313,367 true; w=752,900 false", ovl(313, 8, 27.5, 49.5, 2) && ovl(367, 8, 27.5, 49.5, 2) && !ovl(752, 8, 27.5, 49.5, 2) && !ovl(900, 8, 27.5, 49.5, 2));
const kev = run("kev", true);
ok("кэВ-ось: 9 подписей, ни одной с суффиксом 'k'", kev.length === 9 && kev.every((s) => !/\dk\b/.test(s)));
ok("кэВ-ось: единица 'кэВ' ровно один раз, на последней подписи ('4 778 кэВ')", kev.filter((s) => /кэВ/.test(s)).length === 1 && kev[8] === "4 778 кэВ");
ok("кэВ-ось: средний тик 4096 кан. -> '2 389'", kev[4] === "2 389");
ok("канальная ось: подписи = целые каналы, без единицы", run("ch", false).join() === "0,1024,2048,3072,4096,5120,6144,7168,8192");
ok("словарь: ax.kev есть в обоих языках (ru 'кэВ', en 'keV')", /"ax\.kev":"кэВ"/.test(page) && /"ax\.kev":"keV"/.test(page));
if (failed) { console.log("FAILED: " + failed); process.exit(1); }
console.log("ALL OK");
