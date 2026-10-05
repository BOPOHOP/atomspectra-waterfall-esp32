Write a Node.js ES module test script (file `test/web/csrf_retry_test.mjs`). Output ONLY the JavaScript code, no markdown fences, no explanations.

Purpose: every web page must have a JS function `post` that, when the server answers HTTP 403 (stale CSRF token), calls `loadCsrf()` once to refresh the global `csrfToken` and repeats the request exactly once.

Requirements:
1. Imports: `readFileSync` from "node:fs", `execSync` from "node:child_process".
2. Optional CLI arg `process.argv[2]` = git ref. If given, read each page with `execSync("git show " + ref + ":" + path).toString()`; otherwise `readFileSync(path, "utf8")`. Run from repo root.
3. Pages: `web/index.html`, `web/saved.html`, `web/service.html`, `web/system.html`, `web/waterfall.html`.
4. Extract the source of the `post` function from the page text: find the index of the substring `function post(url,`; if the 6 characters before it are `async ` include them. Then take characters from the start up to the matching closing brace of the function body (count `{` and `}` starting at the first `{` after `function post(`, stop when depth returns to 0). Ignore braces inside strings — the real code has none that would unbalance it, so plain counting is acceptable. Throw an Error with the path if not found.
5. For each page build a sandbox:
   - variable `board = "NEW"` (the token the board currently accepts), counter `calls = 0`.
   - `ctx = { csrfToken: "STALE" }`.
   - async `fetch(url, o)`: increments `calls`; if `url === "/api/csrf-token"` returns `{ json: async () => ({ token: board }) }`; otherwise returns `{ status: o.headers["X-CSRF-Token"] === board ? 200 : 403 }`.
   - async `loadCsrf()`: `const r = await (await fetch("/api/csrf-token")).json(); ctx.csrfToken = r.token;`
   - Instantiate the extracted function with `new Function("fetch", "loadCsrf", "ctx", "with(ctx){ " + src + "; return post; }")(fetch, loadCsrf, ctx)`. (`with` works because `new Function` bodies are non-strict.)
6. Argument: for `waterfall` pass `{ a: 1 }`, for others pass `{ body: "-sto" }`.
7. Case A: `r1 = await post("/api/command", arg)` with stale token — expect `r1.status === 200`.
   Case B: reset `calls = 0`, then `r2 = await post("/api/command", arg)` — expect `r2.status === 200` and `calls === 1` (no extra requests with a valid token).
8. Print one line per page: `OK   <path> stale-><r1.status> fresh-><r2.status> calls=<calls>` or `FAIL ...` same format. Count failures.
9. Finally print `failed: <n>` and `process.exit(n ? 1 : 0)`.
10. Use top-level await. Keep it under 60 lines. First line comment: `// CSRF-1: post() must refetch the CSRF token and retry once on 403. Usage: node test/web/csrf_retry_test.mjs [git-ref]`
