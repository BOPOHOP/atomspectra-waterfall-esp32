// 1.2.32: viewRestore (web/index.html) не должен затирать сохранённые настройки вида. Usage: node test/web/view_restore_test.mjs [путь к index.html]
import { readFileSync } from "node:fs";
const page = readFileSync(process.argv[2] || "web/index.html", "utf8");
const pick = (n) => { const m = page.match(new RegExp("^function " + n + "\\(.*$", "m")); if (!m) throw new Error(n + " not found"); return m[0]; };
const src = ["viewSave", "setPow", "setLog", "setCps", "setKev", "viewRestore"].map(pick).join("\n");
let failed = 0;
const ok = (name, c) => { console.log((c ? "OK   " : "FAIL ") + name); if (!c) failed++; };
function run(saved, body = "") {   // -> {st: localStorage после restore+body, mem: состояние в памяти, n: число setItem}
  const store = { "aswf-view": JSON.stringify(saved) };
  let n = 0;
  const f = new Function("localStorage", `var isLog=true,isPow=false,isCps=true,isKev=true,lastD=null;
    function setSeg(){} function draw(){} function clearCanvas(){} ${src}; viewRestore(); ${body};
    return {log:isLog,pow:isPow,cps:isCps,kev:isKev};`);
  const mem = f({ getItem: (k) => store[k] ?? null, setItem: (k, v) => { store[k] = v; n++; } });
  return { st: JSON.parse(store["aswf-view"]), mem, n };
}
for (const s of [{ log: false, pow: false, cps: false, kev: false }, { log: true, pow: false, cps: false, kev: true },
                 { log: false, pow: true, cps: true, kev: false }, { log: true, pow: false, cps: true, kev: true }]) {
  const r = run(s), tag = JSON.stringify(s);
  ok("restore не пишет в хранилище (setItem=0) " + tag, r.n === 0 && JSON.stringify(r.st) === tag);
  ok("в памяти восстановлено " + tag, r.mem.log === s.log && r.mem.pow === s.pow && r.mem.cps === s.cps && r.mem.kev === s.kev);
}
// F13: неполный/старый объект — cps/kev/log берутся из умолчаний (true/true/true), не сбрасываются в false
for (const s of [{}, { pow: false }, { log: 1, cps: "x", kev: null }]) {
  const r = run(s), tag = JSON.stringify(s);
  ok("неполный объект -> умолчания " + tag, r.mem.cps === true && r.mem.kev === true && r.mem.log === true && r.n === 0);
}
// F12: сеттеры без ns сохраняют вид (пишут в хранилище), setLog сбрасывает pow
const base = { log: true, pow: false, cps: true, kev: true };
let r = run(base, "setPow()");  ok("setPow сохраняет pow=true,log=false", r.st.pow === true && r.st.log === false);
r = run(base, "setPow();setLog(true)"); ok("setLog сбрасывает pow в памяти и хранилище", r.mem.pow === false && r.st.pow === false);
r = run(base, "setLog(false)"); ok("setLog сохраняет log=false", r.st.log === false && r.n === 1);
r = run(base, "setCps(false)"); ok("setCps сохраняет cps=false", r.st.cps === false && r.n === 1);
r = run(base, "setKev(false)"); ok("setKev сохраняет kev=false", r.st.kev === false && r.n === 1);
// вызов viewRestore на старте страницы (первая строка async-IIFE)
ok("viewRestore() вызывается на старте", /\(async function\(\)\{\s*viewRestore\(\);/.test(page));
process.exit(failed ? 1 : 0);
