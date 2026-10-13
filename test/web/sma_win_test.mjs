// test/web/sma_win_test.mjs - окно SMA главной = «Интервал усреднения, с» вкладки Мониторинг (1.2.36)
// Запуск из корня репо: node test/web/sma_win_test.mjs [путь к index.html]
import { readFileSync } from "node:fs";
const src = readFileSync(process.argv[2] || "web/index.html", "utf8");
const m = src.match(/function smaWinSec\(\)\{[^\n]*\}\n/);
if (!m) throw new Error("smaWinSec not found");
let store = {};
let broken = false;
const localStorage = { getItem: (k) => { if (broken) throw new Error("denied"); return k in store ? store[k] : null; } };
const smaWinSec = new Function("localStorage", m[0] + "; return smaWinSec;")(localStorage);

let failed = 0;
const check = (n, ok) => { console.log(ok ? "OK   " + n : "FAIL " + n); if (!ok) failed++; };
const sess = (o) => { store = { "aswf-mon-sess": JSON.stringify(o) }; };

// sma_win_iv: w = iv, один интервал; ключ aswf-sma-win (отсчёты) не влияет
sess({ r: true, iv: 30 }); check("sma_win_iv", smaWinSec() === 30);
sess({ iv: 1 }); check("sma_win_iv", smaWinSec() === 1);
sess({ iv: 7 }); store["aswf-sma-win"] = "50"; check("sma_win_no_multiplier", smaWinSec() === 7);
// sma_win_default: нет данных / мусор / iv<1 / localStorage недоступен -> 10 с
store = {}; check("sma_win_default", smaWinSec() === 10);
store = { "aswf-mon-sess": "{oops" }; check("sma_win_default", smaWinSec() === 10);
sess({ iv: 0 }); check("sma_win_default", smaWinSec() === 10);
sess({}); check("sma_win_default", smaWinSec() === 10);
broken = true; check("sma_win_default", smaWinSec() === 10); broken = false;
// sma_win_int: целое, не больше 86400 (максимум поля), без "1e+21"
sess({ iv: 12.7 }); check("sma_win_int", smaWinSec() === 12);
sess({ iv: 1e21 }); check("sma_win_int", smaWinSec() === 86400);
// sma_text_only_number: видимый текст после дроби - " / " + число, без подписи SMA (единица CPS - статический span рядом)
check("sma_text_only_number", src.includes('es.textContent=(sma===null)?"":" / "+sma.toFixed(2);'));
check("sma_no_label", !src.includes('" / SMA "') && !src.includes("aswf-sma-win"));
check("sma_unit_kept", src.includes('id="big-sma" style="font-size:15px"></span><span class="mut" style="font-size:12px"> CPS</span>'));
console.log(failed ? "FAILED " + failed : "ALL OK");
process.exit(failed ? 1 : 0);
