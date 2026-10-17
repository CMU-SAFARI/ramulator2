/**
 * Headless check of the Bank State reconstruction (app/utils/bankState.ts) on a RAM2BIN trace.
 *
 *   node scripts/verify-bank-state.ts <trace.ram2bin> [t0_ns:t1_ns ...]
 *
 * Prints JSON: resolved rules, invariant violations, and per data-bus-domain window statistics
 * (bus %, tFAW %, bank-time shares) in the form of the Bank State panel titles. Without
 * windows, the whole trace is used. Exits non-zero if an invariant is violated.
 */
import { readFileSync } from 'node:fs';
import { parseTrace } from '../app/composables/useTrace.ts';
import {
  BANK_KIND,
  bankAccumulate,
  bankCoverage,
  bankBuildState,
  bankDomCount,
  bankDomOf,
  bankEmptyStats,
  type BankIntervalSet,
  type BankStateData,
} from '../app/utils/bankState.ts';

const [file, ...windowArgs] = process.argv.slice(2);
if (!file) {
  console.error('usage: node scripts/verify-bank-state.ts <trace.ram2bin> [t0_ns:t1_ns ...]');
  process.exit(2);
}

const buf = readFileSync(file);
const trace = parseTrace(buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength));
const { spec, arrays, header } = trace;
const tckIdx = spec.timingNames.indexOf('tCK_ps');
const nsPerTick = tckIdx >= 0 ? spec.timingValues[tckIdx]! / 1000 : 1;

const t0 = performance.now();
const bs = bankBuildState(spec, arrays, header.numEntries, header.dramType);
const buildMs = performance.now() - t0;

const violations: string[] = [];
const fail = (msg: string) => {
  if (violations.length < 50) violations.push(msg);
};

function checkSpans(s: BankStateData) {
  const L = s.layout;
  for (let b = 0; b < L.numBanks; b++) {
    const lo = s.spanOffsets[b]!;
    const hi = s.spanOffsets[b + 1]!;
    if (hi - lo < 2) fail(`bank ${b}: no spans`);
    for (let k = lo; k < hi; k++) {
      const st = s.spanStart[k]!;
      if (!Number.isFinite(st)) fail(`bank ${b} span ${k}: non-finite start`);
      if (k > lo && st < s.spanStart[k - 1]!) fail(`bank ${b} span ${k}: start goes backwards`);
      if (k > lo && k < hi - 1 && st === s.spanStart[k - 1]!) fail(`bank ${b} span ${k}: zero-length span`);
      const kind = s.spanKind[k]!;
      if (k < hi - 1 && kind > BANK_KIND.OPEN) fail(`bank ${b} span ${k}: bad kind ${kind}`);
    }
  }
}

function checkIntervals(name: string, set: BankIntervalSet | null) {
  if (!set) return;
  const D = set.offsets.length - 1;
  for (let d = 0; d < D; d++) {
    for (let j = set.offsets[d]!; j < set.offsets[d + 1]!; j++) {
      if (!(set.end[j]! > set.start[j]!)) fail(`${name}[${d}] #${j}: empty or inverted`);
      if (j > set.offsets[d]! && set.start[j]! <= set.end[j - 1]!) fail(`${name}[${d}] #${j}: overlaps previous`);
    }
  }
}

checkSpans(bs);
checkIntervals('bus', bs.bus);
checkIntervals('bg', bs.bg);
checkIntervals('faw', bs.faw);

const windows: [number, number][] = windowArgs.length
  ? windowArgs.map((w) => {
      const [a, b] = w.split(':').map(Number);
      return [a! / nsPerTick, b! / nsPerTick] as [number, number];
    })
  : [[bs.firstClk, bs.endClk]];

const L = bs.layout;
const pct = (x: number, tot: number) => (tot > 0 ? (x / tot) * 100 : 0);
const results = windows.map(([w0, w1]) => {
  const domains = [];
  for (let d = 0; d < bankDomCount(L, L.busLevel); d++) {
    const lanes: number[] = [];
    for (let b = 0; b < L.numBanks; b++) if (bankDomOf(L, b, L.busLevel) === d) lanes.push(b);
    const acc = bankAccumulate(bs, lanes, w0, w1, bankEmptyStats(w0, w1));
    const tot = acc.banks * (w1 - w0);
    const sum = acc.idle + acc.act + acc.pre + acc.wrrec + acc.ref + acc.open;
    if (w0 >= bs.firstClk && w1 <= bs.endClk && Math.abs(sum - tot) > 1e-6 * tot) {
      fail(`window ${w0}-${w1} domain ${d}: bank-time ${sum} != ${tot}`);
    }
    if (Math.abs(acc.starved + acc.bus + acc.bg - acc.open) > 1e-6 * Math.max(1, tot)) {
      fail(`window ${w0}-${w1} domain ${d}: open split does not add up`);
    }
    const fawD = L.fawLevel !== null ? bankDomOf(L, lanes[0] ?? 0, L.fawLevel) : -1;
    domains.push({
      domain: d,
      banks: acc.banks,
      busPct: bankCoverage(bs.bus, d, w0, w1) * 100,
      fawPct: fawD >= 0 ? bankCoverage(bs.faw, fawD, w0, w1) * 100 : null,
      shares: {
        starved: pct(acc.starved, tot),
        bus: pct(acc.bus, tot),
        bg: pct(acc.bg, tot),
        act: pct(acc.act, tot),
        pre: pct(acc.pre, tot),
        idle: pct(acc.idle, tot),
        wrrec: pct(acc.wrrec, tot),
        ref: pct(acc.ref, tot),
      },
    });
  }
  return { t0Ns: w0 * nsPerTick, t1Ns: w1 * nsPerTick, t0: w0, t1: w1, domains };
});

console.log(JSON.stringify({
  file,
  dramType: header.dramType,
  family: bs.rules.family,
  numEntries: bs.numEntries,
  nsPerTick,
  levels: L.levelNames.map((n, i) => `${n}=${L.levelSizes[i]}`),
  numBanks: L.numBanks,
  levels_used: { firstLevel: L.firstLevel, bankLevel: L.bankLevel, busLevel: L.busLevel, fawLevel: L.fawLevel, bgLevel: L.bgLevel },
  timings: bs.rules.timings,
  notes: bs.rules.notes,
  spans: bs.spanStart.length,
  intervals: { bus: bs.bus.start.length, bg: bs.bg?.start.length ?? 0, faw: bs.faw?.start.length ?? 0 },
  buildMs: Math.round(buildMs),
  violations,
  windows: results,
}, null, 2));

if (violations.length) process.exit(1);
