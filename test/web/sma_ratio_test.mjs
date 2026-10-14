// test/web/sma_ratio_test.mjs
import { readFileSync } from "node:fs";
const src = readFileSync(process.argv[2] || "web/common-time.js", "utf8");
const a = src.indexOf("/*SMA-BEGIN*/"), b = src.indexOf("/*SMA-END*/");
if (a < 0 || b < a) throw new Error("SMA-BEGIN/END not found");
const L = new Function(src.substring(a, b) + "; return { smaRatioSeries };")();

let failed = 0;
const check = (n, ok) => { console.log(ok ? "OK   " + n : "FAIL " + n); if (!ok) failed++; };

const STAND_CPS = 262;
let seed = 0x5eed0081;
const rnd = () => (seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0) >>> 8;

let moments = 0;
let defPairs = 0;
let diffMean = 0;

// Test 1: sma_ratio_def
seed = 0x5eed0081;
const N = 400;
const counts = new Array(N);
const durs = new Array(N);
for (let k = 0; k < N; k++) {
    durs[k] = 10 + (rnd() % 4);
    counts[k] = durs[k] * STAND_CPS + (rnd() % 81) - 40;
}

let allEqual = true;
const wins = [1, 2, 5, 10, 50, 200, 500];
for (const win of wins) {
    const out = L.smaRatioSeries(counts, durs, win);
    if (out.length !== N) { allEqual = false; continue; }
    for (let i = 0; i < N; i++) {
        defPairs++;
        const start = Math.max(0, i - win + 1);
        let sumC = 0, sumD = 0;
        for (let j = start; j <= i; j++) {
            sumC += counts[j];
            sumD += durs[j];
        }
        const expected = sumD === 0 ? 0 : sumC / sumD;
        if (out[i] !== expected) {
            allEqual = false;
        } else {
            // Calculate diffMean: ratio of sums vs mean of per-bucket ratios
            let sumRatios = 0;
            let validCount = 0;
            for (let j = start; j <= i; j++) {
                if (durs[j] !== 0) {
                    sumRatios += counts[j] / durs[j];
                    validCount++;
                }
            }
            const meanOfRatios = validCount > 0 ? sumRatios / validCount : 0;
            if (Math.abs(expected - meanOfRatios) > 1e-9) {
                diffMean++;
            }
        }
    }
}

// Zero durations check
const zeroOut = L.smaRatioSeries([5, 0, 7], [0, 0, 0], 2);
if (zeroOut.length === 3 && zeroOut[0] === 0 && zeroOut[1] === 0 && zeroOut[2] === 0 && zeroOut.every(v => Number.isFinite(v))) {
    // ok
} else {
    allEqual = false;
}

check("sma_ratio_def", allEqual);

// Test 2: sma_ratio_bucket_close
seed = 0x5eed0081;
const M = 1200;
const samples = new Array(M);
for (let k = 0; k < M; k++) {
    samples[k] = { counts: STAND_CPS + (rnd() % 33) - 16, dur: 1 };
}

const boardSma = (samples, upto, W) => {
    let sumC = 0, sumD = 0;
    for (let i = upto; i >= 0; i--) {
        sumC += samples[i].counts;
        sumD += samples[i].dur;
        if (sumD >= W) break;
    }
    return sumD === 0 ? 0 : sumC / sumD;
};

let allEqual2 = true;
const ivs = [1, 5, 10, 30];
const wins2 = [2, 10, 50];
for (const iv of ivs) {
    for (const win of wins2) {
        const maxM = Math.floor(M / iv);
        for (let m = 1; m <= maxM; m++) {
            const u = iv * m - 1;
            // Build buckets up to m
            const bc = new Array(m);
            const bd = new Array(m);
            for (let b = 0; b < m; b++) {
                let cSum = 0;
                for (let s = 0; s < iv; s++) {
                    cSum += samples[b * iv + s].counts;
                }
                bc[b] = cSum;
                bd[b] = iv; // dur is always 1 per sample, so bucket dur is iv
            }
            
            const site = L.smaRatioSeries(bc, bd, win);
            const siteLast = site[site.length - 1];
            const boardVal = boardSma(samples, u, win * iv);
            
            moments++;
            if (Math.abs(siteLast - boardVal) > 1e-9 * Math.max(1, Math.abs(boardVal))) {
                allEqual2 = false;
            }
        }
    }
}
check("sma_ratio_bucket_close", allEqual2);

// Test 3: sma_ratio_edges
let allEqual3 = true;

// Empty input
const emptyOut = L.smaRatioSeries([], [], 10);
if (!Array.isArray(emptyOut) || emptyOut.length !== 0) {
    allEqual3 = false;
}

// win=1, equal durations
const c3 = [2620, 2631, 2598];
const d3 = [10, 10, 10];
const out3 = L.smaRatioSeries(c3, d3, 1);
if (out3.length === 3) {
    for (let i = 0; i < 3; i++) {
        if (Math.abs(out3[i] - c3[i] / 10) > 1e-9 * Math.max(1, Math.abs(c3[i] / 10))) {
            allEqual3 = false;
        }
    }
} else {
    allEqual3 = false;
}

// win=100 (larger than length), equal durations
const out4 = L.smaRatioSeries(c3, d3, 100);
if (out4.length === 3) {
    const sumC = c3[0] + c3[1] + c3[2];
    const sumD = d3[0] + d3[1] + d3[2];
    const expected = sumC / sumD;
    const last = out4[2];
    if (Math.abs(last - expected) > 1e-9 * Math.max(1, Math.abs(expected))) {
        allEqual3 = false;
    }
} else {
    allEqual3 = false;
}

check("sma_ratio_edges", allEqual3);

console.log("INFO sma_ratio: bucket_close_moments=" + moments + " def_pairs=" + defPairs + " diff_ratio_vs_mean_of_ratios=" + diffMean);
console.log("failed: " + failed);
process.exit(failed ? 1 : 0);
