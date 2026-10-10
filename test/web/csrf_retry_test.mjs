// CSRF-1 / OPT-12: общие post()/gget() в web/common-time.js обязаны на 403 (устаревший токен) взять новый через loadCsrf() и повторить запрос ровно один раз; postJson() в waterfall.html - обёртка над post. Usage: node test/web/csrf_retry_test.mjs [common-time.js [waterfall.html]]
import { readFileSync } from "node:fs";
const jsPath = process.argv[2] || "web/common-time.js";
const htmlPath = process.argv[3] || "web/waterfall.html";
const jsSrc = readFileSync(jsPath, "utf8");
const htmlSrc = readFileSync(htmlPath, "utf8");
function grab(content, name) {
  const idx = content.indexOf(`function ${name}(`);
  if (idx === -1) throw new Error(`Function ${name} not found`);
  let start = idx;
  if (content.slice(idx - 6, idx) === "async ") start -= 6;
  let depth = 0, i = content.indexOf("{", start);
  for (; i < content.length; i++) {
    if (content[i] === "{") depth++;
    else if (content[i] === "}") { depth--; if (depth === 0) break; }
  }
  return content.slice(start, i + 1);
}
const loadCsrfSrc = grab(jsSrc, "loadCsrf");
const postSrc = grab(jsSrc, "post");
const ggetSrc = grab(jsSrc, "gget");
const postJsonSrc = grab(htmlSrc, "postJson");
let failed = 0;
function check(name, ok, info) {
  if (ok) console.log(`OK   ${name} ${info}`);
  else { failed++; console.log(`FAIL ${name} ${info}`); }
}
function buildSandbox(sources, extra = {}) {
  const ctx = { csrfToken: "STALE", ...extra };
  const board = "NEW";
  let calls = 0, tokenFetches = 0, last = null;
  const fetch = async (url, o) => {
    calls++;
    if (url === "/api/csrf-token") {
      tokenFetches++;
      return { json: async () => ({ token: board }) };
    }
    last = { url, o };
    if (ctx.reject) return { status: 403 };
    return { status: o.headers["X-CSRF-Token"] === board ? 200 : 403 };
  };
  const fn = new Function("fetch", "ctx", `with(ctx){ ${sources.join(";\n")}; return { post: typeof post === 'function' ? post : null, gget: typeof gget === 'function' ? gget : null, postJson: typeof postJson === 'function' ? postJson : null }; }`);
  const api = fn(fetch, ctx);
  return { ctx, api, get calls() { return calls; }, get tokenFetches() { return tokenFetches; }, get last() { return last; } };
}
try {
  let s = buildSandbox([loadCsrfSrc, postSrc]);
  let r = await s.api.post("/api/command", { body: "-sto" });
  check("post stale->retry", r.status === 200 && s.tokenFetches === 1 && s.calls === 3 && s.last.o.method === "POST", `status=${r.status} calls=${s.calls}`);
} catch (e) { check("post stale->retry", false, e.message); }
try {
  let s = buildSandbox([loadCsrfSrc, postSrc]);
  s.ctx.csrfToken = "NEW";
  let r = await s.api.post("/api/command", { body: "-sto" });
  check("post fresh->no retry", r.status === 200 && s.calls === 1, `status=${r.status} calls=${s.calls}`);
} catch (e) { check("post fresh->no retry", false, e.message); }
try {
  let s = buildSandbox([loadCsrfSrc, postSrc], { reject: true });
  let r = await s.api.post("/api/command", { body: "-sto" });
  check("post retries once only", r.status === 403 && s.calls === 3, `status=${r.status} calls=${s.calls}`);
} catch (e) { check("post retries once only", false, e.message); }
try {
  let s = buildSandbox([loadCsrfSrc, ggetSrc]);
  let r = await s.api.gget("/api/settings/backup");
  check("gget stale->retry", r.status === 200 && s.tokenFetches === 1 && s.calls === 3, `status=${r.status} calls=${s.calls}`);
} catch (e) { check("gget stale->retry", false, e.message); }
try {
  let s = buildSandbox([loadCsrfSrc, ggetSrc]);
  s.ctx.csrfToken = "";
  let r = await s.api.gget("/api/settings/backup");
  check("gget empty token loads first", r.status === 200 && s.tokenFetches === 1 && s.calls === 2, `status=${r.status} calls=${s.calls}`);
} catch (e) { check("gget empty token loads first", false, e.message); }
try {
  let s = buildSandbox([loadCsrfSrc, postSrc, postJsonSrc]);
  let r = await s.api.postJson("/api/waterfall/config", { persist: true });
  check("postJson with object", r.status === 200 && s.last.o.headers["Content-Type"] === "application/json" && s.last.o.body === '{"persist":true}' && s.calls === 3, `status=${r.status} calls=${s.calls}`);
} catch (e) { check("postJson with object", false, e.message); }
try {
  let s = buildSandbox([loadCsrfSrc, postSrc, postJsonSrc]);
  s.ctx.csrfToken = "NEW";
  let r = await s.api.postJson("/api/waterfall/start");
  check("postJson without body", r.status === 200 && s.last.o.body === undefined && s.last.o.headers["Content-Type"] === "application/json", `status=${r.status}`);
} catch (e) { check("postJson without body", false, e.message); }
console.log(`failed: ${failed}`);
process.exit(failed ? 1 : 0);
