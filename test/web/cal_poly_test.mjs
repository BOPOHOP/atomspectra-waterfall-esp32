// OPT-9: energy polynomial module in common-time.js (block CAL-BEGIN..CAL-END: calE, calMono, calMonoCh, calSame, calInv, calKev, calChan). Usage: node test/web/cal_poly_test.mjs [path-to-common-time.js]
import { readFileSync } from "node:fs";
const src = readFileSync(process.argv[2] || "web/common-time.js", "utf8");
const a = src.indexOf("/*CAL-BEGIN*/"), b = src.indexOf("/*CAL-END*/");
if (a < 0 || b < a) throw new Error("CAL-BEGIN/END not found");
const L = new Function(src.substring(a, b) + "; return { calE, calMono, calMonoCh, calSame, calInv, calKev, calChan };")();
let failed = 0;
const check = (n, ok) => { console.log(ok ? "OK   " + n : "FAIL " + n); if (!ok) failed++; };
const near = (x, y, e) => Math.abs(x - y) <= e;
const C = [0.25, 0.3, 1e-6];
const ref = (c, x) => c.reduce((s, p, i) => s + p * Math.pow(x, i), 0);
check("calE basic", L.calE([5], 100) === 5 && L.calE([0, 1], 7) === 7 && L.calE([1, 2, 3], 2) === 17);
check("calE poly", [0, 1, 1655, 4096, 8191].every(ch => near(L.calE(C, ch), ref(C, ch), 1e-9)));
check("calMono true", L.calMono(C, 8192) === true && L.calMono([0, 0.4], 8192) === true && L.calMono([-0.1, 0.4], 8192) === true);
check("calMono false", L.calMono([100, -1, 1e-4], 8192) === false && L.calMono([0, -0.4], 8192) === false && L.calMono([5], 8192) === false && L.calMono([0, 1, -1e-4], 8192) === false);
// 1.2.36: calMonoCh = прежняя проверка импорта (64 целых канала 0..n-1); эталон - дословно старый цикл с Math.pow
const oldMono = (c, n) => { let p = -Infinity; for (let j = 0; j < 64; j++) { const ch = Math.round(j * (n - 1) / 63); let e = 0; for (let i = 0; i < c.length; i++) e += c[i] * Math.pow(ch, i); if (!(e > p)) return false; p = e; } return true; };
check("calMonoCh == старый цикл: примеры", L.calMonoCh(C, 8192) === true && L.calMonoCh([0, -0.4], 8192) === false && L.calMonoCh([5], 8192) === false && L.calMonoCh([4160.25, -129, 1], 8192) === true && L.calMono([4160.25, -129, 1], 8192) === false);
let seed = 12345; const rnd = () => (seed = (seed * 1103515245 + 12345) % 2147483648) / 2147483648;
let diffNew = 0, diffOld = 0, same = 0;
for (let k = 0; k < 20000; k++) {   // параболы с минимумом/максимумом у краёв шкалы: именно там сетки расходятся
    const p = rnd() < 0.5 ? rnd() * 200 : 8000 + rnd() * 300, a = (rnd() < 0.5 ? 1 : -1) * 1e-3 * (0.2 + rnd()), c = [a * p * p + 1, -2 * a * p, a];
    const o = oldMono(c, 8192), m = L.calMonoCh(c, 8192), g = L.calMono(c, 8192);
    if (o !== m) { same = -1e9; } else if (o !== g) { if (o) diffNew++; else diffOld++; }
}
check("calMonoCh == старый цикл на 20000 параболах у краёв (расхождений 0)", same >= 0);
check("calMono на тех же расходится со старым (доказательство, что замена была не эквивалентной): принимал старый " + diffNew + ", принимал новый " + diffOld, diffNew > 0);
check("calSame true", L.calSame(C, C.slice()) === true && L.calSame([0, 0.4], [0.2, 0.4]) === true);
check("calSame false", L.calSame([0, 0.4], [0.6, 0.4]) === false && L.calSame([0, 0.4], [0, 0.4001]) === false);
check("calInv default", [10, 662, 1332, 2400].every(E => near(L.calE(C, L.calInv(C, E, 8192)), E, 1e-6)));
check("calInv bounds", near(L.calInv([0, 0.4], -50, 8192), -0.5, 1e-6) && near(L.calInv([0, 0.4], 1e6, 8192), 8191.5, 1e-6));
check("calInv explicit", near(L.calInv([0, 0.4], -50, 8192, 0, 8192), 0, 1e-6) && near(L.calInv([0, 0.4], 1e6, 8192, 0, 8192), 8192, 1e-6) && near(L.calInv([0, 0.4], 100, 8192, 0, 8192), 250, 1e-6) && near(L.calInv([0, 1], 1e6, 4096, 0, 4096), 4096, 1e-6));
check("calInv precision", near(L.calInv([0, 1], 1234.5678, 8192), 1234.5678, 1e-9));
check("calKev", L.calKev(null, 123) === 123 && L.calKev(undefined, 5) === 5 && L.calKev([0, 2], 10) === 20);
check("calChan", near(L.calChan(null, 700, 8192), 700, 1e-6) && near(L.calChan(null, 99999, 8192), 8192, 1e-6) && near(L.calChan([0, 0.4], 100, 8192), 250, 1e-6) && near(L.calChan([0, 0.4], -5, 8192), 0, 1e-6) && near(L.calChan(C, L.calE(C, 3000), 8192), 3000, 1e-6));
check("round trip", [0, 17, 1655, 4000, 8190].every(ch => near(L.calChan(C, L.calKev(C, ch), 8192), ch, 1e-5)));
console.log("failed: " + failed);
process.exit(failed ? 1 : 0);
