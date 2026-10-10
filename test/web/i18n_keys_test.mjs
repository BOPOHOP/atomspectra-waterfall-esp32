// I18N-1: dictionaries of web pages - ru/en key parity, used keys exist, shared keys equal. Usage: node test/web/i18n_keys_test.mjs
import { readFileSync } from "node:fs";
const pages = ["web/index.html","web/monitor.html","web/saved.html","web/service.html","web/system.html","web/waterfall.html","web/setup.html"];
let failed = 0;
const dicts = {};
for (const p of pages) {
  const text = readFileSync(p, "utf8");
  const m = text.match(/var DICT=(\{[\s\S]*?\n\});/);
  if (!m) throw new Error(`No DICT in ${p}`);
  const d = new Function("return " + m[1])();
  dicts[p] = d;
  const ruKeys = Object.keys(d.ru), enKeys = Object.keys(d.en);
  const ruSet = new Set(ruKeys), enSet = new Set(enKeys);
  for (const k of ruKeys) if (!enSet.has(k)) { console.log(`FAIL ${p} ru/en: key "${k}" only in ru`); failed++; }
  for (const k of enKeys) if (!ruSet.has(k)) { console.log(`FAIL ${p} ru/en: key "${k}" only in en`); failed++; }
  const used = new Set();
  for (const mm of text.matchAll(/(?<![\w$.])(?:t|tr|tfmt)\(\s*["']([^"']+)["'](?!\s*\+)/g)) used.add(mm[1]);
  for (const mm of text.matchAll(/data-i18n(?:-html|-placeholder|-title)?="([^"]+)"/g)) used.add(mm[1]);
  for (const k of used) {
    if (!d.ru[k]) { console.log(`FAIL ${p} used key "${k}" missing in ru`); failed++; }
    if (!d.en[k]) { console.log(`FAIL ${p} used key "${k}" missing in en`); failed++; }
  }
  console.log(`${p}: ru=${ruKeys.length} en=${enKeys.length} used=${used.size}`);
}
const shared = ["ax.kev","btn.start","btn.stop"];
for (const k of shared) {
  for (const lang of ["ru","en"]) {
    const vals = pages.filter(p => dicts[p][lang][k] !== undefined).map(p => [p, dicts[p][lang][k]]);
    if (vals.length < 2) continue;
    const [fp, fv] = vals[0];
    for (let i = 1; i < vals.length; i++) {
      if (vals[i][1] !== fv) { console.log(`FAIL shared key "${k}" ${lang}: ${fp}="${fv}" vs ${vals[i][0]}="${vals[i][1]}"`); failed++; }
    }
  }
}
const caseKeys = ["cal.set","cal.unset"];
for (const k of caseKeys) {
  for (const lang of ["ru","en"]) {
    const vals = pages.filter(p => dicts[p][lang][k] !== undefined).map(p => [p, dicts[p][lang][k]]);
    if (vals.length < 2) continue;
    const [fp, fv] = vals[0];
    const fCase = fv[0] === fv[0].toUpperCase();
    for (let i = 1; i < vals.length; i++) {
      const vCase = vals[i][1][0] === vals[i][1][0].toUpperCase();
      if (vCase !== fCase) { console.log(`FAIL case "${k}" ${lang}: ${fp}="${fv}" vs ${vals[i][0]}="${vals[i][1]}"`); failed++; }
    }
  }
}
console.log(`failed: ${failed}`);
process.exit(failed ? 1 : 0);
