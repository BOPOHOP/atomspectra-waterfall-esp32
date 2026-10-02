// 1.2.31: разбор файла фона в браузере (web/saved.html, блок IMPORT-BEGIN..END) и кодирование тела ASI1.
// Usage: node test/web/import_parse_test.mjs [путь к saved.html]. Образец bins — боевой: tests/host/fixtures/imp_real_bins.h.
import { readFileSync } from "node:fs";
import zlib from "node:zlib";
const page = readFileSync(process.argv[2] || "web/saved.html", "utf8");
const a = page.indexOf("/*IMPORT-BEGIN*/"), b = page.indexOf("/*IMPORT-END*/");
if (a < 0 || b < a) throw new Error("IMPORT-BEGIN/END not found");
const L = new Function(page.substring(a, b) + "; return {impParse,impEncode,impDetect,impCrc,impCalibOk};")();
const fx = readFileSync("tests/host/fixtures/imp_real_bins.h", "utf8");
const bins = fx.substring(fx.indexOf("IMP_REAL_BINS[8192]")).match(/\d+/g).slice(1).map(Number);
const cal = fx.match(/IMP_REAL_CALIB\[5\] = \{([^}]*)\}/)[1].split(",").map(Number);
const T = Number(fx.match(/IMP_REAL_TIME (\d+)/)[1]), SUM = Number(fx.match(/IMP_REAL_SUM (\d+)/)[1]);
let failed = 0;
function ok(name, c) { console.log((c ? "OK   " : "FAIL ") + name); if (!c) failed++; }
function fails(name, fn, k) { try { fn(); ok(name, false); } catch (e) { ok(name + " -> " + (e && e.k), e && e.k === k); } }
ok("fixture: 8192 bins, sum " + SUM, bins.length === 8192 && bins.reduce((x, y) => x + y, 0) === SUM);
// Экспорты платы побайтно по форматным строкам web_server.c: render_spectrum_json / _xml / _n42 / _csv (один и тот же спектр).
const SA = 1790000000, LOST = 5, pad = (n) => String(n).padStart(2, "0");
function ldt(t) { const d = new Date(t * 1000); return [d.getFullYear(), pad(d.getMonth() + 1), pad(d.getDate()), pad(d.getHours()), pad(d.getMinutes()), pad(d.getSeconds())]; }
const jsonTxt = `{"bins":[${bins.join(",")}],"total":${SUM},"cpu":3,"cps":${Math.floor(SUM / T)},"lost":${LOST},"time":${T},"live":${T}.0,"serial":"SN-123","dead":0,"calib":[${cal.map((c) => c.toPrecision(15)).join(",")}],"calib_set":true,"saved_at":${SA}}`;
function xmlTxt(name, nch) {
    const s = ldt(SA - T), e = ldt(SA), f = (x) => `${x[0]}-${x[1]}-${x[2]}T${x[3]}:${x[4]}:${x[5]}`;
    return `<?xml version="1.0" encoding="utf-8"?>\r\n<ResultDataFile><ResultDataList><ResultData>\r\n<SampleInfo>\r\n<Name>${name}</Name>\r\n</SampleInfo>\r\n<StartTime>${f(s)}</StartTime>\r\n<EndTime>${f(e)}</EndTime>\r\n` +
        `<EnergySpectrum><NumberOfChannels>${nch}</NumberOfChannels><EnergyCalibration><PolynomialOrder>4</PolynomialOrder><Coefficients>${cal.map((c) => `<Coefficient>${c.toPrecision(15)}</Coefficient>`).join("\r\n")}</Coefficients></EnergyCalibration>` +
        `<ValidPulseCount>${SUM}</ValidPulseCount><TotalPulseCount>${SUM + LOST}</TotalPulseCount><MeasurementTime>${T}</MeasurementTime><LiveTime>${T}.0</LiveTime><Spectrum>\r\n` +
        bins.map((v) => `<DataPoint>${v}</DataPoint>\r\n`).join("") + `</Spectrum></EnergySpectrum></ResultData></ResultDataList></ResultDataFile>\r\n`;
}
const n42Txt = (() => { const s = ldt(SA - T); return `﻿<?xml version="1.0"?>\r\n<RadInstrumentData xmlns="http://physics.nist.gov/N42/2011/N42">\r\n  <EnergyCalibration id="SpectrumCalibration-0">\r\n    <CoefficientValues>${cal.map((c) => c.toPrecision(15) + " ").join("")}</CoefficientValues>\r\n  </EnergyCalibration>\r\n` +
    `  <RadMeasurement id="SpectrumMeasurement-0">\r\n    <StartDateTime>${s[2]}.${s[1]}.${s[0]} ${s[3]}:${s[4]}:${s[5]}</StartDateTime>\r\n    <RealTimeDuration>PT${T}S</RealTimeDuration>\r\n    <Spectrum id="SpectrumData">\r\n      <LiveTimeDuration>PT${T}.0S</LiveTimeDuration>\r\n      <ChannelData compressionCode="None">${bins.map((v) => v + " ").join("")}</ChannelData>\r\n    </Spectrum>\r\n    <GrossCounts><TotalCounts>${SUM + LOST}</TotalCounts></GrossCounts>\r\n  </RadMeasurement>\r\n</RadInstrumentData>`; })();
const csvTxt = `Channel,Counts (TotalTime=${T}.0s)\r\n` + bins.map((v, i) => `${i},${v}\r\n`).join("");
const MT = 1700000000000;     // File.lastModified — запасная дата, из файла должна браться дата набора
const P = { json: L.impParse(jsonTxt, "a.json", MT), xml: L.impParse(xmlTxt("SN-123", 8192), "a.xml", MT), n42: L.impParse(n42Txt, "a.n42", MT), csv: L.impParse(csvTxt, "a.csv", MT) };
for (const f of ["json", "xml", "n42", "csv"]) {
    ok(f + ": формат определён", P[f].format === f && L.impDetect(f === "csv" ? csvTxt : f === "json" ? jsonTxt : f === "n42" ? n42Txt : xmlTxt("x", 8192)) === f);
    ok(f + ": bins и time совпали с образцом", P[f].time === T && P[f].bins.length === 8192 && P[f].bins.every((v, i) => v === bins[i]) && P[f].sum === SUM);
}
ok("lost: json/xml/n42 = 5, csv = 0", P.json.lost === LOST && P.xml.lost === LOST && P.n42.lost === LOST && P.csv.lost === 0);
ok("калибровка: json/xml/n42 5 коэфф., csv нет (+предупреждение)", [P.json, P.xml, P.n42].every((p) => p.calib && p.calib.length === 5 && Math.abs(p.calib[1] - cal[1]) < 1e-12) && P.csv.calib === null && P.csv.warn.includes("imp.warnNoCal"));
ok("дата конца набора: json/xml/n42 = saved_at, csv = lastModified", P.json.savedAt === SA && P.xml.savedAt === SA && P.n42.savedAt === SA && P.csv.savedAt === MT / 1000);
ok("серийник: json/xml = SN-123", P.json.serial === "SN-123" && P.xml.serial === "SN-123");
ok("серийник IMP: не наращивается при повторном импорте", L.impParse(jsonTxt.replace('"SN-123"', '"IMP:SN-123"'), "a.json", MT).serial === "SN-123");
// Кодирование ASI1: длина, заголовок, CRC (независимо — node:zlib.crc32), общий вектор с C-реализацией (tests/host).
const body = new Uint8Array(L.impEncode({ bins, time: T, lost: LOST, calib: cal, savedAt: SA, serial: "SN-123" }));
const dv = new DataView(body.buffer);
ok("ASI1: длина 32896", body.length === 32896);
ok("ASI1: magic/version/hdr/channels", dv.getUint32(0, true) === 0x31495341 && dv.getUint16(4, true) === 1 && dv.getUint16(6, true) === 128 && dv.getUint32(8, true) === 8192);
ok("ASI1: time/lost/order/saved_at/flags", dv.getUint32(12, true) === T && dv.getUint32(16, true) === LOST && dv.getInt32(20, true) === 4 && dv.getUint32(64, true) === SA && dv.getUint32(68, true) === 0 && dv.getUint32(120, true) === 1);
ok("ASI1: crc = node:zlib.crc32(hdr[0..124)+bins)", dv.getUint32(124, true) === zlib.crc32(body.subarray(128), zlib.crc32(body.subarray(0, 124))));
ok("ASI1: serial с NUL-добивкой", String.fromCharCode(...body.subarray(72, 78)) === "SN-123" && body[78] === 0 && body[119] === 0);
ok("ASI1: общий вектор с C (0x85ea3f0d проверяется и в tests/host/test_spectrum_import_plan.c)", dv.getUint32(124, true) === 0x85ea3f0d);
const nc = new Uint8Array(L.impEncode({ ...P.csv }));
ok("ASI1: без калибровки order = -1", new DataView(nc.buffer).getInt32(20, true) === -1);
// Отказы (до отправки на плату).
const jb = (f) => { const o = JSON.parse(jsonTxt); f(o); return JSON.stringify(o); };
fails("json: 4096 каналов", () => L.impParse(jb((o) => { o.bins = o.bins.slice(0, 4096); }), "x", MT), "imp.err.channels");
fails("json: отрицательный счёт", () => L.impParse(jb((o) => { o.bins[10] = -1; }), "x", MT), "imp.err.counts");
fails("json: дробный счёт", () => L.impParse(jb((o) => { o.bins[10] = 1.5; }), "x", MT), "imp.err.counts");
fails("json: все нули", () => L.impParse(jb((o) => { o.bins = o.bins.map(() => 0); }), "x", MT), "imp.err.counts");
fails("json: сумма > 2^32-1", () => L.impParse(jb((o) => { o.bins[1] = 4294967295; o.bins[2] = 4294967295; }), "x", MT), "imp.err.counts");
fails("json: time = 0", () => L.impParse(jb((o) => { o.time = 0; }), "x", MT), "imp.err.time");
fails("json: time > 10 лет", () => L.impParse(jb((o) => { o.time = 315360001; }), "x", MT), "imp.err.time");
fails("xml: NumberOfChannels != числу DataPoint", () => L.impParse(xmlTxt("SN-123", 4096), "x", MT), "imp.err.format");
fails("не спектр", () => L.impParse("hello", "x.txt", MT), "imp.err.format");
const badCal = L.impParse(jb((o) => { o.calib = [0, -1]; }), "x", MT);
ok("калибровка не монотонна: отброшена, импорт идёт, есть предупреждение", badCal.calib === null && badCal.warn.includes("imp.warnCalibBad"));
ok("calib_set=false: калибровка не берётся", L.impParse(jb((o) => { o.calib_set = false; }), "x", MT).calib === null);
ok("total в файле != сумме каналов: предупреждение", L.impParse(jb((o) => { o.total = SUM * 2; }), "x", MT).warn.includes("imp.warnTotal"));
ok("xml: часы платы не синхронизированы (1970) -> дата из lastModified", L.impParse(xmlTxt("x", 8192).replace(/<EndTime>[^<]*/, "<EndTime>1970-01-01T00:00:10"), "x", MT).savedAt === MT / 1000);
ok("xml: имя с &amp; и кириллицей -> ASCII", L.impParse(xmlTxt("A&amp;B Ж", 8192), "x", MT).serial === "A&B ?");
// Реальный N42 водопада (scripts/example-waterfall.n42): много измерений -> отказ; одно измерение (CountedZeroes) -> 8192 канала.
const wf = readFileSync("scripts/example-waterfall.n42", "utf8");
fails("n42 водопада: много спектров -> отказ", () => L.impParse(wf, "wf.n42", MT), "imp.err.format");
const m0 = wf.indexOf("<RadMeasurement"), m1 = wf.indexOf("</RadMeasurement>") + "</RadMeasurement>".length;
const one = L.impParse(wf.substring(0, m0) + wf.substring(m0, m1) + "\n</RadInstrumentData>", "one.n42", MT);
ok("n42 водопада: одна строка (CountedZeroes) = 8192 канала, 1 с, калибровка 5 коэфф.", one.bins.length === 8192 && one.time === 1 && one.calib.length === 5 && one.sum > 0);
console.log(failed ? `FAILED: ${failed}` : "ALL PASSED");
process.exit(failed ? 1 : 0);
