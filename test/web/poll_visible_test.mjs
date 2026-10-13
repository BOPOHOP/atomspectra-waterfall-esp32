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
function make(hasApi, fn, rnd) {
  const clock = { t: 1000000 };
  const timers = [];
  const listeners = [];
  const doc = { addEventListener: (t, f) => listeners.push({ type: t, fn: f }) };
  if (hasApi) doc.hidden = false;
  const win = {};
  const setTimeout = (fn, ms) => { const id = timers.length + 1; timers.push({ fn, ms, id, done: false }); return id; };
  new Function("window", "document", "setTimeout", "Date", src)(win, doc, setTimeout, { now: () => clock.t });
  const poll = win.pollWhileVisible;
  const sb = { timers, listeners, doc, clock, calls: 0, id: null };
  const f = fn ? () => { sb.calls++; return fn(); } : () => { sb.calls++; };
  sb.id = poll(f, 2000, rnd || (() => 0.5));
  // 1.2.36: опрос - цепочка setTimeout; tick выполняет ПОСЛЕДНИЙ ожидающий таймер (аргумент id оставлен для читаемости вызовов)
  sb.tick = () => { const t = timers[timers.length - 1]; t.fn(); };
  sb.fire = (type) => { listeners.filter(l => l.type === type).forEach(l => l.fn()); };
  return sb;
}
let sb = make(true);
sb.tick(sb.id); sb.tick(sb.id);
check("T1 visible", sb.calls === 2 && sb.timers[0].ms === 300 && sb.timers[1].ms === 2000 && sb.id === 1, `calls=${sb.calls}, ms0=${sb.timers[0].ms}, ms1=${sb.timers[1].ms}, id=${sb.id}`);
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
// 1.2.36: разброс опросов. Периоды берём из ms последовательных setTimeout; rnd подставлен (детерминированно)
function lcg(seed) { let x = seed >>> 0; return () => { x = (Math.imul(x, 1664525) + 1013904223) >>> 0; return x / 4294967296; }; }
function periods(rnd, n) { const q = make(true, null, rnd); for (let i = 0; i < n; i++) q.tick(); return q.timers.map(x => x.ms); }
let ms12 = periods(lcg(12345), 4001), per = ms12.slice(1);
const ext = periods(() => 0, 2).slice(1), ext2 = periods(() => 0.999999, 2).slice(1);
check("T12 jitter bounds +-12% (random sample and generator extremes)", per.length === 4001 && per.every(m => m >= 1760 - 1e-6 && m <= 2240 + 1e-6) && Math.abs(ext[0] - 1760) < 1e-9 && Math.abs(ext2[0] - 2240) < 0.01, `min=${Math.min(...per)}, max=${Math.max(...per)}, u=0 -> ${ext[0]}, u->1 -> ${ext2[0]}`);
check("T12b jitter reaches the edges", Math.min(...per) < 2000 * 0.885 && Math.max(...per) > 2000 * 1.115, `min=${Math.min(...per)}, max=${Math.max(...per)}`);
const mean = per.reduce((a, b) => a + b, 0) / per.length;
check("T13 mean period ~ base (+-1%)", Math.abs(mean - 2000) < 20, `mean=${mean}`);
const firsts = []; for (const u of [0, 0.25, 0.5, 0.999999]) firsts.push(periods(() => u, 0)[0]);
check("T14 first call within [0, 0.3*base]", firsts.every(m => m >= 0 && m <= 600) && firsts[0] === 0 && Math.abs(firsts[3] - 600) < 0.01, `firsts=${firsts}`);
const sa = periods(lcg(7), 50), sbb = periods(lcg(7), 50), sc = periods(lcg(8), 50);
check("T15 deterministic with injected generator", JSON.stringify(sa) === JSON.stringify(sbb) && JSON.stringify(sa) !== JSON.stringify(sc), "same seed must give same sequence, different seed a different one");
const hid = make(true, null, lcg(3)); hid.doc.hidden = true; hid.tick(); hid.tick();
check("T16 hidden tab keeps the chain alive, no calls", hid.calls === 0 && hid.timers.length === 3, `calls=${hid.calls}, timers=${hid.timers.length}`);
console.log(`failed: ${failed}`);
process.exit(failed ? 1 : 0);
