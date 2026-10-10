// LOAD-2: pollWhileVisible must skip calls in a hidden tab and poll immediately on return. Usage: node test/web/poll_visible_test.mjs [path-to-common-time.js]
import { readFileSync } from "node:fs";
const file = process.argv[2] || "web/common-time.js";
const s = readFileSync(file, "utf8");
const a = s.indexOf("window.pollWhileVisible = function pollWhileVisible");
if (a < 0) throw new Error("helper not found");
const b = s.indexOf("\n};", a) + 3;
const src = s.slice(a, b);
let failed = 0;
function check(name, cond, detail) {
  if (cond) console.log(`OK   ${name}`);
  else { console.log(`FAIL ${name}: ${detail}`); failed++; }
}
function make(hasApi, fn) {
  const clock = { t: 1000000 };
  const timers = [];
  const listeners = [];
  const doc = { addEventListener: (t, f) => listeners.push({ type: t, fn: f }) };
  if (hasApi) doc.hidden = false;
  const win = {};
  const setInterval = (fn, ms) => { const id = timers.length + 1; timers.push({ fn, ms, id }); return id; };
  new Function("window", "document", "setInterval", "Date", src)(win, doc, setInterval, { now: () => clock.t });
  const poll = win.pollWhileVisible;
  const sb = { timers, listeners, doc, clock, calls: 0, id: null };
  const f = fn ? () => { sb.calls++; return fn(); } : () => { sb.calls++; };
  sb.id = poll(f, 2000);
  sb.tick = (id) => { const t = timers.find(x => x.id === id); if (t) t.fn(); };
  sb.fire = (type) => { listeners.filter(l => l.type === type).forEach(l => l.fn()); };
  return sb;
}
let sb = make(true);
sb.tick(sb.id); sb.tick(sb.id);
check("T1 visible", sb.calls === 2 && sb.timers[0].ms === 2000 && sb.id === 1, `calls=${sb.calls}, ms=${sb.timers[0].ms}, id=${sb.id}`);
sb = make(true);
sb.doc.hidden = true;
sb.tick(sb.id); sb.tick(sb.id); sb.tick(sb.id);
check("T2 hidden skip", sb.calls === 0, `calls=${sb.calls}`);
sb.doc.hidden = false;
sb.fire("visibilitychange");
check("T3 return immediate", sb.calls === 1, `calls=${sb.calls}`);
sb.tick(sb.id);
check("T3 return tick", sb.calls === 2, `calls=${sb.calls}`);
sb = make(true);
sb.doc.hidden = true;
sb.fire("visibilitychange");
check("T4 hidden no call", sb.calls === 0, `calls=${sb.calls}`);
sb = make(false);
sb.tick(sb.id); sb.tick(sb.id); sb.tick(sb.id);
check("T5 no API", sb.calls === 3, `calls=${sb.calls}`);
// S1-3 (1.2.36): fn, вернувшая Promise, не входит повторно, пока не завершилась (resolve или reject)
const flush = () => new Promise((r) => setImmediate(r));
let res, rej; const pend = () => new Promise((a, b) => { res = a; rej = b; });
sb = make(true, pend);
sb.tick(sb.id); sb.tick(sb.id); sb.fire("visibilitychange");
check("T6 async busy skips tick and return", sb.calls === 1, `calls=${sb.calls}`);
res(); await flush(); sb.tick(sb.id);
check("T7 resolved frees", sb.calls === 2, `calls=${sb.calls}`);
rej(new Error("net")); await flush(); sb.tick(sb.id);
check("T8 reject frees (flag not stuck)", sb.calls === 3, `calls=${sb.calls}`);
sb.clock.t += 29000; sb.tick(sb.id);
check("T9 hung request: still busy at 29 s", sb.calls === 3, `calls=${sb.calls}`);
sb.clock.t += 2000; sb.tick(sb.id);
check("T10 hung request: freed after 30 s", sb.calls === 4, `calls=${sb.calls}`);
let boom = true; sb = make(true, () => { if (boom) throw new Error("sync"); });
try { sb.tick(sb.id); } catch (e) {}
boom = false; sb.tick(sb.id);
check("T11 sync throw does not latch", sb.calls === 2, `calls=${sb.calls}`);
console.log(`failed: ${failed}`);
process.exit(failed ? 1 : 0);
