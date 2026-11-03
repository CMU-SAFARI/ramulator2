// Window statistics from tile summaries: whole records add their totals, and the two records at the
// window's edges are counted from their detail. Without that detail an edge is estimated from its
// sub-bins and the result is marked inexact.
import { BANK_KIND, bankDomOf } from '../utils/bankState';
import { CMD_CLASS } from './timing';
import { BANK_TIME, BANK_TIMES, SUBBINS, TILE, type IntervalPieces, type TileDetail, type TileSummary } from './types';
import type { Context } from './ingest';

export interface Records {
  /** Summaries in tile order. */
  tiles: TileSummary[];
  detail(k: number): TileDetail | undefined;
}

export function recordStart(r: TileSummary): number {
  return r.k * TILE;
}

export function recordEnd(r: TileSummary): number {
  return (r.k + r.span) * TILE;
}

/** Index of the last record starting at or before t (-1 if none). */
export function recordAt(tiles: TileSummary[], t: number): number {
  let lo = 0;
  let hi = tiles.length;
  while (lo < hi) {
    const mid = (lo + hi) >>> 1;
    if (recordStart(tiles[mid]!) <= t) lo = mid + 1;
    else hi = mid;
  }
  return lo - 1;
}

/** Records [i0, i1) whose tile range intersects [t0, t1). */
export function recordRange(tiles: TileSummary[], t0: number, t1: number): [number, number] {
  let i0 = recordAt(tiles, t0);
  if (i0 < 0 || recordEnd(tiles[i0]!) <= t0) i0++;
  let i1 = i0;
  while (i1 < tiles.length && recordStart(tiles[i1]!) < t1) i1++;
  return [i0, i1];
}

/** First index in [lo, hi) with arr[i] >= x. */
export function lowerBound(arr: ArrayLike<number>, x: number, lo = 0, hi = arr.length): number {
  while (lo < hi) {
    const mid = (lo + hi) >>> 1;
    if (arr[mid]! < x) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}

/** Fraction of sub-bin `sb` of record r inside [t0, t1). */
function subShare(r: TileSummary, sb: number, t0: number, t1: number): number {
  const w = (r.span * TILE) / SUBBINS;
  const a = recordStart(r) + sb * w;
  return Math.max(0, Math.min(t1, a + w) - Math.max(t0, a)) / w;
}

// ── Commands ──────────────────────────────────────────────────────

export interface CommandWindow {
  durationCycles: number;
  totalCommands: number;
  readCommands: number;
  writeCommands: number;
  otherCommands: number;
  accessingCommands: number;
  cmdBusThroughput: number;
  exact: boolean;
}

/** Commands issued in [t0, t1). */
export function commandWindow(rec: Records, ctx: Context, t0: number, t1: number): CommandWindow | null {
  if (!(t1 > t0) || rec.tiles.length === 0) return null;
  const [i0, i1] = recordRange(rec.tiles, t0, t1);
  let reads = 0;
  let writes = 0;
  let others = 0;
  let exact = true;
  for (let i = i0; i < i1; i++) {
    const r = rec.tiles[i]!;
    if (recordStart(r) >= t0 && recordEnd(r) <= t1) {
      reads += r.reads;
      writes += r.writes;
      others += r.others;
      continue;
    }
    const d = rec.detail(r.k);
    if (d) {
      const a = lowerBound(d.cmdT, t0 - d.origin);
      const b = lowerBound(d.cmdT, t1 - d.origin);
      for (let j = a; j < b; j++) {
        const c = ctx.cls[d.cmdId[j]!]!;
        if (c === CMD_CLASS.READ) reads++;
        else if (c === CMD_CLASS.WRITE) writes++;
        else others++;
      }
    } else {
      exact = false;
      for (let sb = 0; sb < SUBBINS; sb++) {
        const f = subShare(r, sb, t0, t1);
        reads += f * r.subReads[sb]!;
        writes += f * r.subWrites[sb]!;
        others += f * r.subOthers[sb]!;
      }
    }
  }
  if (!exact) {
    reads = Math.round(reads);
    writes = Math.round(writes);
    others = Math.round(others);
  }
  const total = reads + writes + others;
  return {
    durationCycles: t1 - t0,
    totalCommands: total,
    readCommands: reads,
    writeCommands: writes,
    otherCommands: others,
    accessingCommands: reads + writes,
    cmdBusThroughput: total / (t1 - t0),
    exact,
  };
}

// ── Bus utilisation ───────────────────────────────────────────────

/**
 * Data- and command-bus cycles in [t0, t1): bursts count where they start on the bus, commands
 * where they issue.
 */
export function busCycles(rec: Records, ctx: Context, t0: number, t1: number): { data: number; cmd: number; exact: boolean } {
  const [i0, i1] = recordRange(rec.tiles, t0, t1);
  let data = 0;
  let cmd = 0;
  let exact = true;
  for (let i = i0; i < i1; i++) {
    const r = rec.tiles[i]!;
    const whole = recordStart(r) >= t0 && recordEnd(r) <= t1;
    const d = whole ? undefined : rec.detail(r.k);
    if (d) {
      for (let j = lowerBound(d.busT, t0 - d.origin), e = lowerBound(d.busT, t1 - d.origin); j < e; j++) data += d.busLen[j]!;
      for (let j = lowerBound(d.cmdT, t0 - d.origin), e = lowerBound(d.cmdT, t1 - d.origin); j < e; j++) cmd += ctx.cycles[d.cmdId[j]!]!;
      continue;
    }
    if (!whole) exact = false;
    for (let sb = 0; sb < SUBBINS; sb++) {
      const f = whole ? 1 : subShare(r, sb, t0, t1);
      data += f * r.subDataCycles[sb]!;
      cmd += f * r.subCmdCycles[sb]!;
    }
  }
  return { data, cmd, exact };
}

// ── Bank State ────────────────────────────────────────────────────

export interface BankWindow {
  t0: number;
  t1: number;
  banks: number;
  idle: number;
  act: number;
  pre: number;
  wrrec: number;
  ref: number;
  open: number;
  starved: number;
  bus: number;
  bg: number;
  exact: boolean;
}

function emptyBank(t0: number, t1: number): BankWindow {
  return { t0, t1, banks: 0, idle: 0, act: 0, pre: 0, wrrec: 0, ref: 0, open: 0, starved: 0, bus: 0, bg: 0, exact: true };
}

/** First piece of domain d (pieces sorted by domain, then start). */
function domStart(p: IntervalPieces, d: number): number {
  return lowerBound(p.dom, d);
}

/** Length of [s, e) ∩ (domain d's pieces), times relative to the record origin. */
function pieceOverlap(p: IntervalPieces | null, d: number, s: number, e: number): number {
  if (!p) return 0;
  let sum = 0;
  for (let q = domStart(p, d); q < p.dom.length && p.dom[q] === d; q++) {
    const a = p.s[q]!;
    if (a >= e) break;
    const b = p.e[q]!;
    if (b > s) sum += Math.min(b, e) - Math.max(a, s);
  }
  return sum;
}

/** Bank-time breakdown of the banks in `lanes` over [t0, t1). */
export function bankWindow(rec: Records, ctx: Context, lanes: ArrayLike<number>, t0: number, t1: number): BankWindow {
  const acc = emptyBank(t0, t1);
  const b = ctx.bank;
  if (!b || !(t1 > t0)) return acc;
  acc.banks = lanes.length;
  const [i0, i1] = recordRange(rec.tiles, t0, t1);
  const add = (v: Float32Array | number[], base: number, f: number) => {
    acc.idle += f * v[base + BANK_TIME.IDLE]!;
    acc.act += f * v[base + BANK_TIME.ACT]!;
    acc.pre += f * v[base + BANK_TIME.PRE]!;
    acc.wrrec += f * v[base + BANK_TIME.WRREC]!;
    acc.ref += f * v[base + BANK_TIME.REF]!;
    const st = v[base + BANK_TIME.OPEN_STARVED]!;
    const bu = v[base + BANK_TIME.OPEN_BUS]!;
    const bg = v[base + BANK_TIME.OPEN_BG]!;
    acc.starved += f * st;
    acc.bus += f * bu;
    acc.bg += f * bg;
    acc.open += f * (st + bu + bg);
  };
  for (let i = i0; i < i1; i++) {
    const r = rec.tiles[i]!;
    if (!r.bankTime) continue;
    if (recordStart(r) >= t0 && recordEnd(r) <= t1) {
      for (let q = 0; q < lanes.length; q++) add(r.bankTime, lanes[q]! * BANK_TIMES, 1);
      continue;
    }
    const d = rec.detail(r.k);
    if (d) {
      const lo = t0 - d.origin;
      const hi = t1 - d.origin;
      const v = new Array<number>(BANK_TIMES);
      for (let q = 0; q < lanes.length; q++) {
        const lane = lanes[q]!;
        v.fill(0);
        for (let j = lowerBound(d.spanLane, lane); j < d.spanLane.length && d.spanLane[j] === lane; j++) {
          const s = Math.max(d.spanS[j]!, lo);
          const e = Math.min(d.spanE[j]!, hi);
          if (e <= s) continue;
          const kind = d.spanKind[j]!;
          if (kind !== BANK_KIND.OPEN) {
            v[kind]! += e - s;
            continue;
          }
          const busD = bankDomOf(b.L, lane, b.L.busLevel);
          const busy = pieceOverlap(d.bus, busD, s, e);
          let bgOnly = 0;
          if (d.bg && b.L.bgLevel !== null) {
            const bgD = bankDomOf(b.L, lane, b.L.bgLevel);
            for (let p = domStart(d.bg, bgD); p < d.bg.dom.length && d.bg.dom[p] === bgD; p++) {
              if (d.bg.s[p]! >= e) break;
              const x = Math.max(d.bg.s[p]!, s);
              const y = Math.min(d.bg.e[p]!, e);
              if (y > x) bgOnly += (y - x) - pieceOverlap(d.bus, busD, x, y);
            }
          }
          v[BANK_TIME.OPEN_BUS]! += busy;
          v[BANK_TIME.OPEN_BG]! += bgOnly;
          v[BANK_TIME.OPEN_STARVED]! += e - s - busy - bgOnly;
        }
        add(v, 0, 1);
      }
    } else {
      // Estimate: the record's own breakdown, scaled by the share of it inside the window.
      acc.exact = false;
      const f = Math.max(0, Math.min(t1, recordEnd(r)) - Math.max(t0, recordStart(r))) / (r.span * TILE);
      for (let q = 0; q < lanes.length; q++) add(r.bankTime, lanes[q]! * BANK_TIMES, f);
    }
  }
  return acc;
}

/** Fraction of [t0, t1) that domain `dom` of the bus-busy (or tFAW) intervals covers. */
export function coverageWindow(rec: Records, set: 'bus' | 'faw', dom: number, t0: number, t1: number): { value: number; exact: boolean } {
  if (!(t1 > t0)) return { value: 0, exact: true };
  const [i0, i1] = recordRange(rec.tiles, t0, t1);
  let sum = 0;
  let exact = true;
  for (let i = i0; i < i1; i++) {
    const r = rec.tiles[i]!;
    const cov = set === 'bus' ? r.busCov : r.fawCov;
    if (!cov) continue;
    if (recordStart(r) >= t0 && recordEnd(r) <= t1) {
      sum += cov[dom]!;
      continue;
    }
    const d = rec.detail(r.k);
    const p = d ? (set === 'bus' ? d.bus : d.faw) : null;
    if (d && p) {
      sum += pieceOverlap(p, dom, t0 - d.origin, t1 - d.origin);
    } else {
      exact = false;
      const sub = set === 'bus' ? r.busCovSub : r.fawCovSub;
      if (sub) for (let sb = 0; sb < SUBBINS; sb++) sum += subShare(r, sb, t0, t1) * sub[dom * SUBBINS + sb]!;
    }
  }
  return { value: sum / (t1 - t0), exact };
}

/** Commands per bin over [t0, t1), for the overview strip. */
export function density(tiles: TileSummary[], t0: number, t1: number, bins: number): Float32Array {
  const out = new Float32Array(bins);
  const w = (t1 - t0) / bins;
  if (!(w > 0)) return out;
  for (const r of tiles) {
    if (r.nCmd === 0) continue;
    const sw = (r.span * TILE) / SUBBINS;
    for (let sb = 0; sb < SUBBINS; sb++) {
      const n = r.subReads[sb]! + r.subWrites[sb]! + r.subOthers[sb]!;
      if (n === 0) continue;
      const mid = recordStart(r) + (sb + 0.5) * sw;
      const bin = Math.floor((mid - t0) / w);
      if (bin >= 0 && bin < bins) out[bin]! += n;
    }
  }
  return out;
}
