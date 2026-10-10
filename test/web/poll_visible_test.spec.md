Write a Node.js ES module test script (file `test/web/poll_visible_test.mjs`). Output ONLY the JavaScript code, no markdown fences, no explanations.

Purpose: `web/common-time.js` defines `window.pollWhileVisible = function pollWhileVisible(fn, ms) {...};` (a setInterval wrapper that skips calls while `document.hidden` is true, calls `fn` immediately on `visibilitychange` to visible, and returns the setInterval id). Test it in a sandbox with fake timers and a fake document.

Requirements:
1. Imports: `readFileSync` from "node:fs". CLI arg `process.argv[2]` = path to the JS file (default `web/common-time.js`). Run from repo root.
2. Extract the helper source: `const s = readFileSync(file, "utf8"); const a = s.indexOf("window.pollWhileVisible = function pollWhileVisible"); if (a < 0) throw new Error("helper not found"); const b = s.indexOf("\n};", a) + 3; const src = s.slice(a, b);`
3. Function `make(hasApi)` builds a fresh sandbox and returns an object with these members:
   - `timers`: array of `{fn, ms, id}`; fake `setInterval(fn, ms)` pushes an entry with `id = timers.length + 1` and returns that id.
   - `listeners`: array of `{type, fn}`.
   - `document`: `{ addEventListener(type, fn) { listeners.push({type, fn}); } }`; if `hasApi` is true it also has `hidden: false` (a normal writable property); if `hasApi` is false there is no `hidden` property at all.
   - `window = {}`; evaluate `new Function("window", "document", "setInterval", src)(window, document, setInterval)`; then `poll = window.pollWhileVisible`.
   - `tick(id)`: calls `fn` of the timer with that id (simulates one interval firing).
   - `fire(type)`: calls every listener with that type (simulates dispatchEvent).
   - `calls`: counter incremented by the polled function `f = () => { sb.calls++; }`; register it with `const id = poll(f, 2000)`; store `id` as `sb.id`, and `sb.timers`, `sb.doc`.
4. Checks (each prints `OK   <name>` or `FAIL <name>: <detail>` and increments `failed` on failure). Use a helper `check(name, cond, detail)`:
   - T1 visible: `make(true)`; `tick(id)` twice -> calls === 2; the registered interval has `ms === 2000`; returned id is a number equal to 1.
   - T2 hidden skip: `make(true)`; set `doc.hidden = true`; `tick(id)` three times -> calls === 0.
   - T3 return from hidden: continue T2 scenario: set `doc.hidden = false`; `fire("visibilitychange")` -> calls === 1 immediately (without tick); then `tick(id)` -> calls === 2.
   - T4 visibilitychange to hidden does not call: `make(true)`; set `doc.hidden = true`; `fire("visibilitychange")` -> calls === 0.
   - T5 no API: `make(false)` (no `hidden` property); `tick(id)` three times -> calls === 3 (behaves like plain setInterval).
5. Print `failed: <n>` at the end and `process.exit(failed ? 1 : 0)`. Failure lines must start exactly with `FAIL ` (at line start) so that a shell script can count them with `grep -c '^FAIL '`.
6. Keep it under 60 lines. First line comment: `// LOAD-2: pollWhileVisible must skip calls in a hidden tab and poll immediately on return. Usage: node test/web/poll_visible_test.mjs [path-to-common-time.js]`
