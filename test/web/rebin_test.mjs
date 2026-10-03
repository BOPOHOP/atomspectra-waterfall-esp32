// 1.2.31: перенос оверлея по энергии (web/index.html, блок REBIN-BEGIN..END). Usage: node test/web/rebin_test.mjs [путь к index.html]
import { readFileSync } from "node:fs";
const page = readFileSync(process.argv[2] || "web/index.html", "utf8");
const a = page.indexOf("/*REBIN-BEGIN*/"), b = page.indexOf("/*REBIN-END*/");
if (a < 0 || b < a) throw new Error("REBIN-BEGIN/END not found");
const L = new Function(page.substring(a, b) + "; return {rebinByEnergy,calSame,calE};")();
const fx = readFileSync("tests/host/fixtures/imp_real_bins.h", "utf8");
const real = fx.substring(fx.indexOf("IMP_REAL_BINS[8192]")).match(/\d+/g).slice(1).map(Number);
const calReal = fx.match(/IMP_REAL_CALIB\[5\] = \{([^}]*)\}/)[1].split(",").map(Number);
let failed = 0;
function ok(name, c) { console.log((c ? "OK   " : "FAIL ") + name); if (!c) failed++; }
const sum = (x) => x.reduce((p, q) => p + q, 0);
const centroid = (x, lo, hi) => { let s = 0, m = 0; for (let i = lo; i < hi; i++) { s += x[i]; m += x[i] * i; } return m / s; };
const line = (ch, v) => { const o = new Array(8192).fill(0); o[ch] = v; return o; };
const A = [0, 0.4], B = [0, 0.35];
const r1 = L.rebinByEnergy(line(1655, 1000), A, B);       // 662 кэВ: канал 1655 при 0,4 кэВ/кан -> 1891,4 при 0,35
ok("линия 662 кэВ переезжает 1655 -> 1891 (центроид +-1)", Math.abs(centroid(r1, 1880, 1900) - 1891.43) <= 1 && r1[1655] === 0);
ok("счёт сохраняется (линия)", Math.abs(sum(r1) - 1000) < 1e-6);
ok("счёт сохраняется (боевой спектр, другая калибровка)", Math.abs(sum(L.rebinByEnergy(real, calReal, [0.3, 0.41, 1e-6])) / sum(real) - 1) < 1e-9);
const same = L.rebinByEnergy(real, calReal, calReal.slice());
ok("равные калибровки: исходный массив без изменений (та же ссылка)", same === real);
const nm = L.rebinByEnergy(real, [100, -1, 1e-4], B);
ok("немонотонная калибровка записи: исходник без изменений", nm === real);
ok("немонотонная калибровка живого: исходник без изменений", L.rebinByEnergy(real, A, [100, -1, 1e-4]) === real);
const ov = line(8191, 5e5); ov[1655] = 1000;
const r2 = L.rebinByEnergy(ov, A, B);
ok("канал переполнения остаётся последним", r2[8191] >= 5e5 && Math.abs(sum(r2) - 501000) < 1e-6);
const r3 = L.rebinByEnergy(line(7000, 1000), A, [0, 0.3]);   // 2800 кэВ выше шкалы приёмника (2457) -> в переполнение
ok("выше шкалы приёмника -> канал переполнения", Math.abs(r3[8191] - 1000) < 1e-6 && Math.abs(sum(r3) - 1000) < 1e-6);
const r4 = L.rebinByEnergy(line(0, 1000), A, [100, 0.4]);    // ниже шкалы приёмника -> канал 0
ok("ниже шкалы приёмника -> канал 0", Math.abs(r4[0] - 1000) < 1e-6 && Math.abs(sum(r4) - 1000) < 1e-6);
ok("calSame: 0,3 кэВ в одной точке — разные; 0,2 кэВ — одинаковые", !L.calSame([0, 0.4], [0.6, 0.4]) && L.calSame([0, 0.4], [0.2, 0.4]));
// Боевой спектр: «другая плата» с шкалой энергии x1,1 — самая мощная область (окно 40 каналов в 200..3000) обязана уехать на E_a/1,1.
let best = 0, pk = 200;
for (let s = 200; s < 3000; s++) { let w = 0; for (let i = s; i < s + 40; i++) w += real[i]; if (w > best) { best = w; pk = s + 20; } }
const cb = calReal.map((c) => c * 1.1), rr = L.rebinByEnergy(real, calReal, cb);
let lo = 0, hi = 8191; const eT = L.calE(calReal, pk) / 1.1;
for (let i = 0; i < 50; i++) { const m = (lo + hi) / 2; if (L.calE(calReal, m) < eT) lo = m; else hi = m; }
const exp = (lo + hi) / 2, got = centroid(rr, Math.round(exp) - 25, Math.round(exp) + 25), was = centroid(real, pk - 20, pk + 20);
ok(`боевой спектр: пик ${pk} -> ожидаемо ${exp.toFixed(1)}, получено ${got.toFixed(1)} (раньше ${was.toFixed(1)}), окно ${best} счётов`, Math.abs(got - exp) <= 1.5 && Math.abs(exp - pk) > 20);
console.log(failed ? `FAILED: ${failed}` : "ALL PASSED");
process.exit(failed ? 1 : 0);
