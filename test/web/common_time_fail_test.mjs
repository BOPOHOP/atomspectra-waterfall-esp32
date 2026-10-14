// LOAD-ERR-1 (F1 release-gate 1.2.36): onerror of the /common-time.js script tag must reload the page once (guard in sessionStorage, 30 s window), otherwise show a visible RU/EN message. Usage: node test/web/common_time_fail_test.mjs [page.html ...]
import fs from "node:fs";
import vm from "node:vm";

const pages = process.argv.slice(2).length
  ? process.argv.slice(2)
  : ["index", "monitor", "saved", "service", "system", "waterfall"].map((n) => `web/${n}.html`);

let failed = 0;
function check(name, ok, info = "") {
  console.log(`${ok ? "OK  " : "FAIL"} ${name}${info ? " " + info : ""}`);
  if (!ok) failed++;
}

const handlers = new Set();
for (const page of pages) {
  let html;
  try {
    html = fs.readFileSync(page, "utf8");
  } catch (e) {
    check(`read ${page}`, false, e.message);
    continue;
  }
  const m = html.match(/<script src="\/common-time\.js" onerror="([^"]*)"><\/script>/);
  check(`onerror on common-time.js tag (${page})`, !!m);
  if (!m) continue;
  handlers.add(m[1]);
  const tagIndex = html.indexOf(m[0]);
  check(`tag precedes inline script (${page})`, tagIndex > 0 && html.indexOf("<script>", tagIndex) > tagIndex);
}

check("handler identical on all pages", handlers.size === 1, `size=${handlers.size}`);
if (handlers.size === 0) {
  console.log(`FAILED ${failed}`);
  process.exit(1);
}

const code = [...handlers][0].replace(/&quot;/g, '"');

function run(store, throwStore, now) {
  const timers = [];
  const reloads = [];
  const inserted = [];
  const ctx = {
    sessionStorage: throwStore
      ? new Proxy({}, {
          get() { throw new Error("blocked"); },
          set() { throw new Error("blocked"); },
        })
      : store,
    Date: { now: () => now },
    setTimeout: (f, ms) => timers.push([f, ms]),
    location: { reload: () => reloads.push(1) },
    document: { body: { insertAdjacentHTML: (pos, h) => inserted.push([pos, h]) } },
  };
  const src = `(function(){${code}}).call(this)`;
  vm.runInNewContext(src, ctx);
  for (const [f] of timers) f();
  return { inserted, reloads, timers };
}

const st = {};

let r = run(st, false, 1000000);
check(
  "first failure: one reload scheduled, no message",
  r.reloads.length === 1 && r.inserted.length === 0 && r.timers.length === 1 && r.timers[0][1] > 0,
  `reloads=${r.reloads.length} inserted=${r.inserted.length} timers=${r.timers.length}`
);

r = run(st, false, 1005000);
const msgOk = r.inserted.length === 1 && /F5/.test(r.inserted[0][1]) && /Не удалось/.test(r.inserted[0][1]) && /failed to load/i.test(r.inserted[0][1]);
check("repeat failure within 30 s: no reload, message shown", r.reloads.length === 0 && r.inserted.length === 1, `reloads=${r.reloads.length} inserted=${r.inserted.length}`);
check("message is RU+EN and mentions F5", msgOk, r.inserted.length ? r.inserted[0][1].slice(0, 80) : "no message");

r = run(st, false, 1060000);
check("failure after 30 s: reload allowed again", r.reloads.length === 1 && r.inserted.length === 0, `reloads=${r.reloads.length} inserted=${r.inserted.length}`);

r = run({}, true, 5);
check("storage blocked: no reload loop, message shown", r.reloads.length === 0 && r.inserted.length === 1, `reloads=${r.reloads.length} inserted=${r.inserted.length}`);

console.log(failed ? `FAILED ${failed}` : "ALL OK");
process.exit(failed ? 1 : 0);
