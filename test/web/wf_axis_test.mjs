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
function run(mode, calibOn) {   // -> массив подписей fillText, которые рисует drawAxis (ctx-заглушка, шкала 0..8192 кан. = 0..4778 кэВ)
  const out = [], ctx = { clearRect() {}, fillRect() {}, beginPath() {}, moveTo() {}, lineTo() {}, stroke() {}, fillText: (s) => out.push(String(s)) };
  new Function("curx", "WFW", "WFHd", "chLo", "chHi", "axisMode", "calib", "ch2kev", "wfAxisNum", "t", da[0] + "; drawAxis(curx,900,520);")(ctx, 900, 520, 0, 8192, mode, calibOn, (c) => c * 4778 / 8192, num, (k) => ({ "ax.kev": "кэВ" })[k] || k);
  return out;
}
const kev = run("kev", true);
ok("кэВ-ось: 9 подписей, ни одной с суффиксом 'k'", kev.length === 9 && kev.every((s) => !/\dk\b/.test(s)));
ok("кэВ-ось: единица 'кэВ' ровно один раз, на последней подписи ('4 778 кэВ')", kev.filter((s) => /кэВ/.test(s)).length === 1 && kev[8] === "4 778 кэВ");
ok("кэВ-ось: средний тик 4096 кан. -> '2 389'", kev[4] === "2 389");
ok("канальная ось: подписи = целые каналы, без единицы", run("ch", false).join() === "0,1024,2048,3072,4096,5120,6144,7168,8192");
ok("словарь: ax.kev есть в обоих языках (ru 'кэВ', en 'keV')", /"ax\.kev":"кэВ"/.test(page) && /"ax\.kev":"keV"/.test(page));
if (failed) { console.log("FAILED: " + failed); process.exit(1); }
console.log("ALL OK");
