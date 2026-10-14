// LOAD-2 (водопад): опросы refreshStatus/refreshSystem/oflPollStat идут через pollWhileVisible, а не голым setInterval. Usage: node test/web/poll_waterfall_test.mjs [path-to-waterfall.html]
import { readFileSync } from "node:fs";
const s = readFileSync(process.argv[2] || "web/waterfall.html", "utf8");
let failed = 0;
function check(name, cond, detail) {
  if (cond) console.log(`OK   ${name}`);
  else { console.log(`FAIL ${name}: ${detail}`); failed++; }
}
for (const fn of ["refreshStatus", "refreshSystem", "oflPollStat"]) {
  const bare = new RegExp(String.raw`(^|[^.\w|(])setInterval\(\s*` + fn + String.raw`\b`, "m").test(s);
  check(`${fn} not bare setInterval`, !bare, "bare setInterval found");
  const wrapped = new RegExp(String.raw`\(window\.pollWhileVisible\|\|setInterval\)\(\s*` + fn + String.raw`\s*,`).test(s);
  check(`${fn} via pollWhileVisible`, wrapped, "pollWhileVisible wrapper not found");
}
console.log(`failed: ${failed}`);
process.exit(failed ? 1 : 0);
