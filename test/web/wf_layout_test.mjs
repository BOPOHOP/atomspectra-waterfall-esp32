// 1.2.33 (#AWF-UI-3/4): последний канал не выводится; поля среза совпадают с областью данных спектрограммы. Usage: node test/web/wf_layout_test.mjs [путь]
import { readFileSync } from "node:fs";
const page = readFileSync(process.argv[2] || "web/waterfall.html", "utf8");
let failed = 0;
const ok = (name, c) => { console.log((c ? "OK   " : "FAIL ") + name); if (!c) failed++; };
const blk = page.match(/\/\*WFH-MATH-BEGIN[\s\S]*?WFH-MATH-END\*\//);
const f = new Function(blk[0] + "; return wfChMax;")();
ok("wfChMax: 8192->8191, 4096->4095, 1->1 (не 0)", f(8192) === 8191 && f(4096) === 4095 && f(1) === 1);
ok("старт chHi=wfChMax(CH); chHiClamp режет по wfChMax(CH)", /var chLo=0, chHi=wfChMax\(CH\);/.test(page) && /var cm=wfChMax\(CH\);/.test(page) && /if\(chHi>cm\)chHi=cm;/.test(page));
ok("сброс зума = wfChMax(CH); в HTML chHi max/value 8191", /chLo=0;chHi=wfChMax\(CH\);/.test(page) && /id="chHi" min="1" max="8191" value="8191"/.test(page));
ok("срез: cHi столбца не выходит за chHi", /if\(cHi>chHi\)cHi=chHi;/.test(page));
ok("поля: #wf-row gap и #wf-scroll width/flex из --wf-gap/--wf-slider", /#wf-row\{[^}]*--wf-slider:16px;[^}]*--wf-gap:8px;[^}]*gap:var\(--wf-gap\)/.test(page) && /#wf-scroll\{[^}]*width:var\(--wf-slider\);\s*flex:0 0 var\(--wf-slider\)/.test(page));
ok("поля: #slc-box = 16 + слайдер + зазор справа, 16 слева (как .pad спектрограммы)", /#slc-box\{[^}]*--wf-slider:16px;[^}]*--wf-gap:8px;[^}]*padding:0 calc\(16px \+ var\(--wf-slider\) \+ var\(--wf-gap\)\) 14px 16px;/.test(page) && /<div id="slc-box">/.test(page) && /\.pad\{ padding:13px 16px; \}/.test(page));
if (failed) { console.log("FAILED: " + failed); process.exit(1); }
console.log("ALL OK");
