// 1.2.31: вызовы функций без определения в inline-скриптах страниц (дефект calibSame→calSame пропустили
// проверки текста: они видят строку, а не то, что функции нет). Запуск: node test/web/undef_calls_test.mjs [страница.html ...]
import fs from "node:fs";
const SKIP = new Set(("if for while switch catch function return typeof new await async do else try finally throw delete void in of " +
  "alert confirm prompt fetch setTimeout setInterval clearTimeout clearInterval parseInt parseFloat isFinite isNaN encodeURIComponent " +
  "decodeURIComponent String Number Boolean Array Object Math JSON Date Uint8Array Uint16Array Uint32Array Int32Array Float32Array " +
  "Float64Array ArrayBuffer DataView Blob Promise Error RegExp Set Map URL requestAnimationFrame cancelAnimationFrame getComputedStyle atob btoa Symbol " +
  "FileReader XMLHttpRequest WebSocket TextEncoder TextDecoder " +
  "T").split(" "));   // T — буква в регулярных литералах дат (\d{4})-(\d\d)T(\d\d), не вызов
const pages = process.argv.slice(2).length ? process.argv.slice(2) : fs.readdirSync("web").filter(f => f.endsWith(".html")).map(f => "web/" + f);
const common = fs.existsSync("web/common-time.js") ? fs.readFileSync("web/common-time.js", "utf8") : "";
const DEF = [/function\s+([A-Za-z_$][\w$]*)/g, /(?:var|let|const)\s+([A-Za-z_$][\w$]*)/g, /(?:[,(]\s*)([A-Za-z_$][\w$]*)(?=\s*[,)])/g,
  /(?:^|[;{},\n])\s*([A-Za-z_$][\w$]*)\s*=[^=]/g];
let bad = 0;
for (const p of pages) {
  const src = [...fs.readFileSync(p, "utf8").matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m => m[1]).join("\n") + "\n" + common;
  const code = src.replace(/\/\*[\s\S]*?\*\//g, "").replace(/\/\/[^\n]*/g, "").replace(/(["'`])(?:\\.|(?!\1)[^\\\n])*\1/g, '""');
  const defd = new Set(DEF.flatMap(re => [...code.matchAll(re)].map(m => m[1]))), seen = new Set();
  for (const m of code.matchAll(/(?<![.\w$])([A-Za-z_$][\w$]*)\s*\(/g)) {
    if (SKIP.has(m[1]) || defd.has(m[1]) || seen.has(m[1])) continue;
    seen.add(m[1]); console.log("FAIL " + p + ": вызов без определения: " + m[1] + "()"); bad++;
  }
}
console.log(bad ? "FAILED " + bad : "ALL PASSED"); process.exit(bad ? 1 : 0);
