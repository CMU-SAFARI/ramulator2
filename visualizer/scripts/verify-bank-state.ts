/**
 * Headless check of the Bank State reconstruction (app/engine) on a RAM2BIN trace. Needs Node 22.7
 * or later (TypeScript run directly):
 *
 *   node --experimental-transform-types --import ./scripts/ts-loader.mjs scripts/verify-bank-state.ts <trace.ram2bin> [t0_ns:t1_ns ...]
 *
 * Prints JSON: resolved rules, invariant violations, and per data-bus-domain window statistics
 * (bus %, tFAW %, bank-time shares) in the form of the Bank State panel titles. Without
 * windows, the whole trace is used. Exits non-zero if an invariant is violated.
 */
import { readFileSync } from 'node:fs';
import { bankDomCount, bankDomOf, BANK_KIND } from '../app/utils/bankState';
import { Ingest, makeContext } from '../app/engine/ingest';
import { bufferSource, openTrace } from '../app/engine/source';
import { bankWindow, coverageWindow, type Records } from '../app/engine/stats';
import { TILE, type IntervalPieces, type TileDetail } from '../app/engine/types';

const [file, ...windowArgs] = process.argv.slice(2);
if (!file) {
  console.error('usage: node --experimental-transform-types --import ./scripts/ts-loader.mjs scripts/verify-bank-state.ts <trace.ram2bin> [t0_ns:t1_ns ...]');
  process.exit(2);
}

const buf = readFileSync(file);
const trace = await openTrace(bufferSource(buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength)));
const { spec, header } = trace;
const tckIdx = spec.timingNames.indexOf('tCK_ps');
const nsPerTick = tckIdx >= 0 ? spec.timingValues[tckIdx]! / 1000 : 1;
const ctx = makeContext(spec, header.readLatency, header.dramType);
if (!ctx.bank) {
  console.error('Bank State does not support this trace.');
  process.exit(2);
}
const { L, rules } = ctx.bank;

const t0 = performance.now();
const ingest = new Ingest(ctx, { wantDetail: () => true });
for (let j = 0; j < trace.chunkCount; j++) ingest.push(await trace.readChunk(j));
ingest.finish();
const buildMs = performance.now() - t0;
const details = new Map<number, TileDetail>(ingest.out.details.map(d => [d.k, d]));
const records: Records = { tiles: ingest.out.summaries, detail: k => details.get(k) };

const violations: string[] = [];
const fail = (msg: string) => {
  if (violations.length < 50) violations.push(msg);
};

/** Each tile's spans tile it exactly, bank by bank, with valid kinds. */
function checkSpans(d: TileDetail) {
  const end = d.span * TILE;
  let j = 0;
  for (let b = 0; b < L.numBanks; b++) {
    let at = d.k === ingest.firstK ? d.spanS[j] ?? 0 : 0;
    if (j >= d.spanLane.length || d.spanLane[j] !== b) {
      fail(`tile ${d.k} bank ${b}: no spans`);
      continue;
    }
    for (; j < d.spanLane.length && d.spanLane[j] === b; j++) {
      if (d.spanS[j] !== at) fail(`tile ${d.k} bank ${b}: gap or overlap at ${d.origin + d.spanS[j]!}`);
      if (!(d.spanE[j]! > d.spanS[j]!)) fail(`tile ${d.k} bank ${b}: empty span`);
      if (d.spanKind[j]! > BANK_KIND.OPEN) fail(`tile ${d.k} bank ${b}: bad kind ${d.spanKind[j]}`);
      at = d.spanE[j]!;
    }
    if (at < end && d !== ingest.out.details[ingest.out.details.length - 1]) fail(`tile ${d.k} bank ${b}: spans end at ${at} of ${end}`);
  }
}

function checkIntervals(name: string, d: TileDetail, p: IntervalPieces | null) {
  if (!p) return;
  for (let j = 0; j < p.s.length; j++) {
    if (!(p.e[j]! > p.s[j]!)) fail(`tile ${d.k} ${name}[${p.dom[j]}]: empty or inverted`);
    if (j > 0 && p.dom[j] === p.dom[j - 1] && p.s[j]! < p.e[j - 1]!) fail(`tile ${d.k} ${name}[${p.dom[j]}]: overlaps previous`);
  }
}

for (const d of ingest.out.details) {
  checkSpans(d);
  checkIntervals('bus', d, d.bus);
  checkIntervals('bg', d, d.bg);
  checkIntervals('faw', d, d.faw);
}

const firstClk = ingest.firstClk;
const endClk = ingest.lastClk + 1;
const windows: [number, number][] = windowArgs.length
  ? windowArgs.map((w) => {
      const [a, b] = w.split(':').map(Number);
      return [a! / nsPerTick, b! / nsPerTick] as [number, number];
    })
  : [[firstClk, endClk]];

const pct = (x: number, tot: number) => (tot > 0 ? (x / tot) * 100 : 0);
const results = windows.map(([w0, w1]) => {
  const domains = [];
  for (let d = 0; d < bankDomCount(L, L.busLevel); d++) {
    const lanes: number[] = [];
    for (let b = 0; b < L.numBanks; b++) if (bankDomOf(L, b, L.busLevel) === d) lanes.push(b);
    const acc = bankWindow(records, ctx, lanes, w0, w1);
    const tot = acc.banks * (w1 - w0);
    const sum = acc.idle + acc.act + acc.pre + acc.wrrec + acc.ref + acc.open;
    if (Math.abs(sum - tot) > 1e-6 * tot) fail(`window ${w0}-${w1} domain ${d}: bank-time ${sum} != ${tot}`);
    if (Math.abs(acc.starved + acc.bus + acc.bg - acc.open) > 1e-6 * Math.max(1, tot)) {
      fail(`window ${w0}-${w1} domain ${d}: open split does not add up`);
    }
    const fawD = L.fawLevel !== null ? bankDomOf(L, lanes[0] ?? 0, L.fawLevel) : -1;
    domains.push({
      domain: d,
      banks: acc.banks,
      busPct: coverageWindow(records, 'bus', d, w0, w1).value * 100,
      fawPct: fawD >= 0 && ctx.bank!.fawDoms ? coverageWindow(records, 'faw', fawD, w0, w1).value * 100 : null,
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
  family: rules.family,
  numEntries: trace.numEntries,
  nsPerTick,
  levels: L.levelNames.map((n, i) => `${n}=${L.levelSizes[i]}`),
  numBanks: L.numBanks,
  levels_used: { firstLevel: L.firstLevel, bankLevel: L.bankLevel, busLevel: L.busLevel, fawLevel: L.fawLevel, bgLevel: L.bgLevel },
  timings: rules.timings,
  notes: rules.notes,
  tiles: ingest.out.details.length,
  buildMs: Math.round(buildMs),
  violations,
  windows: results,
}, null, 2));

if (violations.length) process.exit(1);
