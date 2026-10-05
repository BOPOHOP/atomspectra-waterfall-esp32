// 1.2.33: проход A №2 (кэш bitmap оверлея) и №5 (один пересчёт на кадр при pointermove). Usage: node test/web/wf_cache_test.mjs [путь]
import { readFileSync } from "node:fs";
const page = readFileSync(process.argv[2] || "web/waterfall.html", "utf8");
let failed = 0;
const ok = (name, c) => { console.log((c ? "OK   " : "FAIL ") + name); if (!c) failed++; };
const M = new Function(page.match(/\/\*WFH-MATH-BEGIN[\s\S]*?WFH-MATH-END\*\//)[0] + "; return {wfMemo,wfCoalesce};")();
let built = 0; const slot = {}, bld = () => ({ id: ++built });
const a1 = M.wfMemo(slot, "1,1100,800,2,ru", bld), a2 = M.wfMemo(slot, "1,1100,800,2,ru", bld), a3 = M.wfMemo(slot, "1,1100,800,2,ru", bld);
ok("№2: 3 вызова с одним ключом -> 1 сборка, тот же объект", built === 1 && a1 === a2 && a2 === a3);
M.wfMemo(slot, "2,1100,800,2,ru", bld); M.wfMemo(slot, "2,1100,800,2,ru", bld); M.wfMemo(slot, "2,1100,801,2,ru", bld);
ok("№2: смена drawSeq и размера -> пересборка (итого 3)", built === 3);
ok("№2: ovBitmap кэширует по [drawSeq,W,H,k,LANG], draw() увеличивает drawSeq", /wfMemo\(ovSlot,\[drawSeq,W,H,k,LANG\]\.join\(\)/.test(page) && /drawSeq\+\+;/.test(page));
const q = []; let calls = 0; const f = M.wfCoalesce((cb) => q.push(cb), () => calls++);
for (let i = 0; i < 100; i++) f();
ok("№5: 100 событий до кадра -> 1 запланированный кадр, 0 вызовов", q.length === 1 && calls === 0);
q.shift()(); f(); f();
ok("№5: после кадра 1 вызов; следующая пачка снова 1 кадр", calls === 1 && q.length === 1);
ok("№5: pointermove копит gPend и зовёт gFlush (не setWFH), pointerup дорисовывает setWFH(gPend)", /gPend=gh0\+[^;]*;gFlush\(\);/.test(page) && !/pointermove[^\n]*setWFH\(/.test(page) && /var gend=function\(e\)\{[^}]*setWFH\(gPend,false\)/.test(page));
ok("проход C: pointerdown сбрасывает gPend=WFHd (тап без движения не откатывает высоту)", /gh0=WFHd;gPend=WFHd;grip\.classList\.add\("drag"\)/.test(page));
if (failed) { console.log("FAILED: " + failed); process.exit(1); }
console.log("ALL OK");
