// One pass over the time-ordered command stream that cuts it into tile records: the detail every
// view draws and hit-tests, a summary for statistics and zoomed-out blocks, and the true end of
// every span or interval cut at a tile edge. The pass can stop at any tile boundary as a
// checkpoint and resume from it, so tile detail can be rebuilt later from the trace.
import type { TraceSpec } from '../composables/useTrace';
import {
  BANK_KIND,
  bankBuildLayout,
  bankDomCount,
  bankDomOf,
  bankResolveRules,
  bankTargetResolver,
  bankUnsupportedReason,
  type BankLayout,
  type BankRules,
} from '../utils/bankState';
import { BankMachine, IntervalStream, type BankMachineState, type IntervalStreamState } from './bank';
import type { Chunk } from './chunk';
import { commandLanes, laneInfo, laneOf, type LaneInfo } from './lanes';
import { RequestMachine, type CompletedRequest, type RequestMachineState } from './requests';
import { busLatencyByCmd, burstCycles, CMD_CLASS, commandClasses, commandDuration } from './timing';
import {
  BANK_TIME,
  BANK_TIMES,
  BLOCK_KINDS,
  MAX_GAP_TILES,
  NONE,
  REQ_FLAG,
  SUBBINS,
  TILE,
  tileOf,
  type CrossEnd,
  type IntervalPieces,
  type TileDetail,
  type TileSummary,
} from './types';

/** Everything derived once from a trace's spec. */
export interface Context {
  spec: TraceSpec;
  readLatency: number;
  levels: number;
  li: LaneInfo;
  /** Bank State model, null for traces it does not support. */
  bank: { L: BankLayout; rules: BankRules; busDoms: number; bgDoms: number; fawDoms: number; nFAW: number } | null;
  busLat: Uint16Array;
  burst: Uint16Array;
  cls: Uint8Array;
  cycles: Float64Array;
  accessing: Uint8Array;
}

export function makeContext(spec: TraceSpec, readLatency: number, dramType: string): Context {
  const li = laneInfo(spec);
  const rules = bankUnsupportedReason(spec) ? null : bankResolveRules(spec, dramType);
  let bank: Context['bank'] = null;
  if (rules) {
    const L = bankBuildLayout(spec, rules);
    bank = {
      L,
      rules,
      busDoms: bankDomCount(L, L.busLevel),
      bgDoms: L.bgLevel !== null ? bankDomCount(L, L.bgLevel) : 0,
      fawDoms: L.fawLevel !== null ? bankDomCount(L, L.fawLevel) : 0,
      nFAW: rules.timings.faw?.value ?? 0,
    };
  }
  return {
    spec,
    readLatency,
    levels: spec.levelNames.length,
    li,
    bank,
    busLat: busLatencyByCmd(spec),
    burst: burstCycles(spec),
    cls: commandClasses(spec),
    cycles: Float64Array.from(spec.commandNames, (_, id) => commandDuration(spec, id)),
    accessing: Uint8Array.from(spec.commandMeta, m => (m.isAccessing ? 1 : 0)),
  };
}

interface Burst {
  s: number;
  len: number;
  lane: number;
  cmd: number;
  gidx: number;
  /** Data-bus domain of the bank model, -1 without one. */
  dom: number;
}

/** State at a tile boundary: enough to resume the pass from trace entry `gidx`. */
export interface Checkpoint {
  k: number;
  /** Tiles covered by the record starting at k. */
  span: number;
  gidx: number;
  firstClk: number;
  lastClk: number;
  firstK: number;
  bank: BankMachineState | null;
  bus: IntervalStreamState | null;
  bg: IntervalStreamState | null;
  faw: IntervalStreamState | null;
  fawHist: number[][];
  pending: Burst[];
  req: RequestMachineState;
}

export interface IngestOutput {
  details: TileDetail[];
  summaries: TileSummary[];
  crossEnds: CrossEnd[];
}

export interface IngestOptions {
  /** Keep tile k's detail (summaries are always produced). */
  wantDetail: (k: number) => boolean;
  /** Called at the start of every record whose tile index is at least `checkpointEvery` past the last checkpoint. */
  onCheckpoint?: (cp: Checkpoint) => void;
  checkpointEvery?: number;
  /** Rebuilding from a checkpoint: summaries and cross ends were already produced. */
  rebuild?: boolean;
}

/** A growing column of doubles. */
class Col {
  a: Float64Array;
  n = 0;
  constructor(cap = 256) {
    this.a = new Float64Array(cap);
  }

  push(x: number) {
    if (this.n === this.a.length) {
      const b = new Float64Array(this.n * 2);
      b.set(this.a);
      this.a = b;
    }
    this.a[this.n++] = x;
  }

  clone(): Col {
    const c = new Col(Math.max(16, this.n));
    c.a.set(this.a.subarray(0, this.n));
    c.n = this.n;
    return c;
  }
}

/** Pieces of one interval kind (bus busy, bank group blocked, tFAW) in the current record. */
class IvAcc {
  s = new Col();
  e = new Col();
  dom = new Col();
  cause = new Col();
  trueS = new Col();
  trueE = new Col();

  clone(): IvAcc {
    const c = new IvAcc();
    for (const key of ['s', 'e', 'dom', 'cause', 'trueS', 'trueE'] as const) c[key] = this[key].clone();
    return c;
  }
}

const CMD_COLS = ['cmdT', 'cmdLane', 'cmdId', 'cmdReq', 'rawClk', 'rawArrive', 'rawReqId', 'rawType', 'rawSrc', 'rawAddr', 'fanT', 'fanLane', 'fanCmd', 'fanIdx', 'spanS', 'spanE', 'spanLane', 'spanKind', 'spanCause', 'spanTrueS', 'spanTrueE'] as const;

/** Growing per-record columns, packed into a TileDetail at the end of the record. */
class TileAcc {
  cmdT = new Col(4096);
  cmdLane = new Col(4096);
  cmdId = new Col(4096);
  cmdReq = new Col(4096);
  rawClk = new Col(4096);
  rawArrive = new Col(4096);
  rawReqId = new Col(4096);
  rawType = new Col(4096);
  rawSrc = new Col(4096);
  rawAddr = new Col(16384);
  fanT = new Col();
  fanLane = new Col();
  fanCmd = new Col();
  fanIdx = new Col();
  spanS = new Col(4096);
  spanE = new Col(4096);
  spanLane = new Col(4096);
  spanKind = new Col(4096);
  spanCause = new Col(4096);
  spanTrueS = new Col(4096);
  spanTrueE = new Col(4096);
  bursts: Burst[] = [];
  reqs: CompletedRequest[] = [];
  iv: [IvAcc, IvAcc, IvAcc] = [new IvAcc(), new IvAcc(), new IvAcc()];
  cmdFirst = -1;
  /** Commands and multi-bank instances in the record (counted even when the detail is not kept). */
  nCmd = 0;
  nFan = 0;
  lastT = 0;
  readonly origin: number;
  readonly end: number;
  private readonly subScale: number;
  subReads = new Uint16Array(SUBBINS);
  subWrites = new Uint16Array(SUBBINS);
  subOthers = new Uint16Array(SUBBINS);
  subCmdCycles = new Float32Array(SUBBINS);
  laneCnt: Uint16Array;
  laneTop: Uint8Array;
  laneVote: Uint16Array;

  constructor(readonly k: number, readonly span: number, lanes: number, readonly keep: boolean) {
    this.origin = k * TILE;
    this.end = (k + span) * TILE;
    this.subScale = SUBBINS / (span * TILE);
    this.laneCnt = new Uint16Array(lanes * SUBBINS);
    this.laneTop = new Uint8Array(lanes * SUBBINS).fill(255);
    this.laneVote = new Uint16Array(lanes * SUBBINS);
  }

  sb(t: number): number {
    const s = Math.floor((t - this.origin) * this.subScale);
    return s < 0 ? 0 : s >= SUBBINS ? SUBBINS - 1 : s;
  }

  /** Count an instance on (lane, sub-bin); the most frequent command per cell by majority vote. */
  vote(lane: number, sb: number, cmd: number) {
    const c = lane * SUBBINS + sb;
    if (this.laneCnt[c]! < 0xffff) this.laneCnt[c]!++;
    if (this.laneVote[c] === 0) {
      this.laneTop[c] = cmd;
      this.laneVote[c] = 1;
    } else if (this.laneTop[c] === cmd) this.laneVote[c]!++;
    else this.laneVote[c]!--;
  }

  clone(): TileAcc {
    const c = new TileAcc(this.k, this.span, this.laneCnt.length / SUBBINS, true);
    for (const key of CMD_COLS) c[key] = this[key].clone();
    c.bursts = [...this.bursts];
    c.reqs = [...this.reqs];
    c.iv = [this.iv[0].clone(), this.iv[1].clone(), this.iv[2].clone()];
    c.cmdFirst = this.cmdFirst;
    c.nCmd = this.nCmd;
    c.nFan = this.nFan;
    c.lastT = this.lastT;
    c.subReads = this.subReads.slice();
    c.subWrites = this.subWrites.slice();
    c.subOthers = this.subOthers.slice();
    c.subCmdCycles = this.subCmdCycles.slice();
    c.laneCnt = this.laneCnt.slice();
    c.laneTop = this.laneTop.slice();
    c.laneVote = this.laneVote.slice();
    return c;
  }
}

export class Ingest {
  bank: BankMachine | null;
  ivBus: IntervalStream | null;
  ivBg: IntervalStream | null;
  ivFaw: IntervalStream | null;
  fawHist: number[][];
  pending: Burst[];
  req: RequestMachine;
  acc: TileAcc | null = null;
  gidx: number;
  firstClk: number;
  lastClk: number;
  firstK: number;
  lastCheckpointK = -Infinity;
  out: IngestOutput = { details: [], summaries: [], crossEnds: [] };
  private lanesOf: ReturnType<typeof commandLanes>;
  private maxCycles: number;
  /** Bursts waiting for their bus time: pending[head..] (sorted by start). */
  private head = 0;

  constructor(readonly ctx: Context, readonly opts: IngestOptions, cp?: Checkpoint) {
    const b = ctx.bank;
    const banks = b?.L.numBanks ?? 0;
    this.bank = b ? new BankMachine(banks, 0, (lane, s, e, kind, cause) => this.onSpan(lane, s, e, kind, cause), cp?.bank ?? undefined) : null;
    this.ivBus = b ? new IntervalStream(b.busDoms, (d, s, e, c) => this.onInterval(0, d, s, e, c), cp?.bus ?? undefined) : null;
    this.ivBg = b && b.bgDoms ? new IntervalStream(b.bgDoms, (d, s, e, c) => this.onInterval(1, d, s, e, c), cp?.bg ?? undefined) : null;
    this.ivFaw = b && b.fawDoms ? new IntervalStream(b.fawDoms, (d, s, e, c) => this.onInterval(2, d, s, e, c), cp?.faw ?? undefined) : null;
    this.fawHist = cp ? cp.fawHist.map(h => [...h]) : Array.from({ length: b?.fawDoms ?? 0 }, () => []);
    this.pending = cp ? cp.pending.map(p => ({ ...p })) : [];
    this.req = new RequestMachine(ctx.li.totalLanes, ctx.li.totalLanes, cp?.req);
    this.gidx = cp?.gidx ?? 0;
    this.firstClk = cp?.firstClk ?? Number.NaN;
    this.lastClk = cp?.lastClk ?? Number.NaN;
    this.firstK = cp?.firstK ?? 0;
    const pairs = b ? b.rules.pairs : ctx.spec.commandNames.map(() => null);
    this.lanesOf = commandLanes(ctx.li, ctx.spec, pairs);
    this.maxCycles = ctx.cycles.reduce((m, x) => Math.max(m, x), 1);
    if (cp) this.startRecord(cp.k, cp.span, false);
  }

  checkpoint(): Checkpoint {
    return {
      k: this.acc!.k,
      span: this.acc!.span,
      gidx: this.gidx,
      firstClk: this.firstClk,
      lastClk: this.lastClk,
      firstK: this.firstK,
      bank: this.bank?.snapshot() ?? null,
      bus: this.ivBus?.snapshot() ?? null,
      bg: this.ivBg?.snapshot() ?? null,
      faw: this.ivFaw?.snapshot() ?? null,
      fawHist: this.fawHist.map(h => [...h]),
      pending: this.pending.slice(this.head).map(p => ({ ...p })),
      req: this.req.snapshot(),
    };
  }

  /** The tile currently being filled (undefined before the first command). */
  get currentTile(): number | undefined {
    return this.acc?.k;
  }

  // ── Sinks of the state machines ───────────────────────────────

  private onSpan(lane: number, start: number, end: number, kind: number, cause: number) {
    const a = this.acc!;
    const s = Math.max(start, a.origin);
    if (start < a.origin && !this.opts.rebuild) {
      this.out.crossEnds.push({ fromK: Math.max(tileOf(start), this.firstK), toK: a.k, set: 0, index: lane, end });
    }
    if (end <= s) return;
    a.spanS.push(s);
    a.spanE.push(end);
    a.spanLane.push(lane);
    a.spanKind.push(kind);
    a.spanCause.push(cause);
    a.spanTrueS.push(start);
    a.spanTrueE.push(end);
  }

  private onInterval(set: number, dom: number, start: number, end: number, cause: number) {
    const a = this.acc!;
    const s = Math.max(start, a.origin);
    if (start < a.origin && !this.opts.rebuild) {
      this.out.crossEnds.push({ fromK: Math.max(tileOf(start), this.firstK), toK: a.k, set: set + 1, index: dom, end });
    }
    if (end <= s) return;
    const iv = a.iv[set]!;
    iv.s.push(s);
    iv.e.push(end);
    iv.dom.push(dom);
    iv.cause.push(cause);
    iv.trueS.push(start);
    iv.trueE.push(end);
  }

  // ── Records ───────────────────────────────────────────────────

  private startRecord(k: number, span: number, checkpoint = true) {
    this.acc = new TileAcc(k, span, this.ctx.li.totalLanes, this.opts.wantDetail(k));
    const every = this.opts.checkpointEvery ?? 8;
    if (checkpoint && this.opts.onCheckpoint && k - this.lastCheckpointK >= every) {
      this.lastCheckpointK = k;
      this.opts.onCheckpoint(this.checkpoint());
    } else if (!checkpoint) {
      this.lastCheckpointK = k;
    }
  }

  /** Close the current record and the gap records up to tile k, then open tile k. */
  private advanceTo(k: number) {
    let next = this.acc!.k + this.acc!.span;
    this.finishRecord(this.acc!.end);
    while (next < k) {
      const span = Math.min(k - next, MAX_GAP_TILES);
      this.startRecord(next, span);
      this.finishRecord(this.acc!.end);
      next += span;
    }
    this.startRecord(k, 1);
  }

  /** Release bursts that start before t into the current record and the bus-interval stream. */
  private releaseBursts(t: number) {
    const a = this.acc!;
    const q = this.pending;
    while (this.head < q.length && q[this.head]!.s < t) {
      const b = q[this.head++]!;
      a.bursts.push(b);
      if (this.ivBus && b.dom >= 0) this.ivBus.add(b.dom, b.s, b.s + b.len, b.gidx);
    }
    if (this.head > 1024 && this.head * 2 > q.length) {
      q.splice(0, this.head);
      this.head = 0;
    }
  }

  /**
   * Close the current record at `end` (its last tile's end, or "now" for a live preview): commit
   * everything that happens before `end` and cut what continues past it.
   */
  private finishRecord(end: number) {
    const a = this.acc!;
    this.releaseBursts(end);
    this.bank?.advanceAllBefore(end);
    for (const iv of [this.ivBus, this.ivBg, this.ivFaw]) iv?.closeBefore(end);
    // Spans and intervals still open continue past the record: draw their part inside it.
    if (this.bank) {
      const s = this.bank.s;
      for (let b = 0; b < s.curStart.length; b++) {
        const st = Math.max(s.curStart[b]!, a.origin);
        if (st >= end) continue;
        a.spanS.push(st);
        a.spanE.push(end);
        a.spanLane.push(b);
        a.spanKind.push(s.curKind[b]!);
        a.spanCause.push(s.curCause[b]!);
        a.spanTrueS.push(s.curStart[b]!);
        a.spanTrueE.push(Number.NaN);
      }
    }
    [this.ivBus, this.ivBg, this.ivFaw].forEach((stream, set) => {
      if (!stream) return;
      const iv = a.iv[set]!;
      stream.s.open.forEach((o, d) => {
        if (!o) return;
        const st = Math.max(o.s, a.origin);
        if (st >= end) return;
        iv.s.push(st);
        iv.e.push(Math.min(o.e, end));
        iv.dom.push(d);
        iv.cause.push(o.cause);
        iv.trueS.push(o.s);
        iv.trueE.push(Number.NaN);
      });
    });
    this.emit(a, end);
  }

  // ── Commands ──────────────────────────────────────────────────

  push(c: Chunk) {
    const ctx = this.ctx;
    const b = ctx.bank;
    const targets = b ? bankTargetResolver(b.L, c.addr, c.cmd, b.rules.pairs) : null;
    const levels = ctx.levels;
    const totalLanes = ctx.li.totalLanes;
    for (let i = 0; i < c.n; i++) {
      const clk = c.clk[i]!;
      const gidx = c.first + i;
      if (this.acc === null) {
        this.firstClk = clk;
        this.firstK = tileOf(clk);
        this.startRecord(this.firstK, 1);
      }
      if (clk >= this.acc!.end) this.advanceTo(tileOf(clk));
      const a = this.acc!;
      if (a.cmdFirst < 0) a.cmdFirst = gidx;
      this.lastClk = clk;
      this.gidx = gidx + 1;

      const cmd = c.cmd[i]!;
      const lanes = this.lanesOf(c.addr, cmd, i);
      const lane = laneOf(ctx.li, c.addr, i);
      const sb = a.sb(clk);
      const cls = ctx.cls[cmd]!;
      if (cls === CMD_CLASS.READ) a.subReads[sb]!++;
      else if (cls === CMD_CLASS.WRITE) a.subWrites[sb]!++;
      else a.subOthers[sb]!++;
      a.subCmdCycles[sb]! += ctx.cycles[cmd]!;

      let reqNo = NONE;
      const arrive = c.arrive[i]!;
      const accessing = ctx.accessing[cmd] === 1;
      if (arrive >= 0) {
        const done = this.req.command(c, i, arrive, clk, lane, gidx, accessing, ctx.readLatency);
        reqNo = this.req.lastReqNo;
        if (done) a.reqs.push(done);
      }

      a.nCmd++;
      a.lastT = clk;
      if (a.keep) {
        a.cmdT.push(clk);
        a.cmdLane.push(lanes[0]!);
        a.cmdId.push(cmd);
        a.cmdReq.push(reqNo);
        a.rawClk.push(clk);
        a.rawArrive.push(arrive);
        a.rawReqId.push(c.reqId ? c.reqId[i]! : -1);
        a.rawType.push(c.type[i]!);
        a.rawSrc.push(c.src[i]!);
        for (let k = 0; k < levels; k++) a.rawAddr.push(c.addr[k]![i]!);
      }
      if (lanes[0]! < totalLanes) a.vote(lanes[0]!, sb, cmd);
      for (let j = 1; j < lanes.length; j++) {
        if (a.keep) {
          a.fanT.push(clk);
          a.fanLane.push(lanes[j]!);
          a.fanCmd.push(cmd);
          a.fanIdx.push(gidx);
        }
        a.nFan++;
        a.vote(lanes[j]!, sb, cmd);
      }

      let busDom = -1;
      if (b && targets) {
        const r = b.rules.cmd[cmd];
        if (r && !(r.action === 0 && r.bus === 0 && r.ccd === 0 && !r.faw)) {
          const bl = targets(i);
          if (bl.length > 0) {
            for (let j = 0; j < bl.length; j++) this.bank!.apply(r, bl[j]!, clk, gidx);
            const b0 = bl[0]!;
            if (r.bus > 0) busDom = bankDomOf(b.L, b0, b.L.busLevel);
            if (this.ivBg && r.ccd > 0) this.ivBg.add(bankDomOf(b.L, b0, b.L.bgLevel!), clk, clk + r.ccd, gidx);
            if (this.ivFaw && r.faw) {
              const d = bankDomOf(b.L, b0, b.L.fawLevel!);
              const h = this.fawHist[d]!;
              if (h.length >= 3) this.ivFaw.add(d, clk, h[h.length - 3]! + b.nFAW, gidx);
              h.push(clk);
              if (h.length > 3) h.shift();
            }
          }
        }
      }
      if (accessing) {
        const s = clk + ctx.busLat[cmd]!;
        const burst: Burst = { s, len: ctx.burst[cmd]!, lane, cmd, gidx, dom: busDom };
        // Bus times can run out of issue order (read and write latencies differ): insert sorted.
        const q = this.pending;
        let p = q.length;
        while (p > this.head && q[p - 1]!.s > s) p--;
        if (p === q.length) q.push(burst);
        else q.splice(p, 0, burst);
      }
    }
  }

  /** The trace is complete: close open requests and the last record (its bursts may run past it). */
  finish() {
    if (!this.acc) return;
    const a = this.acc;
    this.releaseBursts(Number.POSITIVE_INFINITY);
    // The trace ends with its last activity (the last command, or the last burst on the data bus),
    // not at the end of its last record.
    let end = this.lastClk + 1;
    for (const b of a.bursts) end = Math.max(end, b.s + b.len);
    end = Math.min(a.end, Math.max(end, a.origin + 1));
    this.req.finish(end, r => a.reqs.push(r));
    this.finishRecord(end);
  }

  /**
   * Detail and summary of the record in progress as of `now`, for a live view; the ingest itself is
   * not changed. Requests still open are shown in flight up to `now`.
   */
  preview(now: number): { detail: TileDetail; summary: TileSummary } | null {
    if (!this.acc) return null;
    const copy = new Ingest(this.ctx, { wantDetail: () => true }, this.checkpoint());
    const a = this.acc.clone();
    copy.acc = a;
    const rows = copy.req.rowEnds;
    for (const r of copy.req.open()) {
      if (r.type <= 1) a.reqs.push(copy.req.place(r, Math.max(now, r.last + 1), true, rows));
    }
    copy.finishRecord(Math.min(a.end, Math.max(now, a.origin + 1)));
    return { detail: copy.out.details[0]!, summary: copy.out.summaries[0]! };
  }

  // ── Packing ───────────────────────────────────────────────────

  private emit(a: TileAcc, end: number) {
    const ctx = this.ctx;
    const o = a.origin;
    const detail = this.opts.wantDetail(a.k) ? this.pack(a) : null;
    if (this.opts.rebuild) {
      if (detail) this.out.details.push(detail);
      return;
    }

    let minStart = o;
    let maxEnd = end;
    const subW = (a.span * TILE) / SUBBINS;
    const laneReqs = new Uint16Array(ctx.li.totalLanes * SUBBINS);
    for (const r of a.reqs) {
      if (r.arrive < minStart) minStart = r.arrive;
      if (r.depart > maxEnd) maxEnd = r.depart;
      const s0 = Math.max(0, Math.floor((r.arrive - o) / subW));
      const s1 = Math.min(SUBBINS - 1, Math.floor((Math.max(r.depart, r.arrive + 1) - 1 - o) / subW));
      for (let sb = s0; sb <= s1; sb++) {
        const c = r.lane * SUBBINS + sb;
        if (laneReqs[c]! < 0xffff) laneReqs[c]!++;
      }
    }
    const subData = new Float32Array(SUBBINS);
    const laneBusy = new Uint16Array(ctx.li.totalLanes * SUBBINS);
    for (const b of a.bursts) {
      const sb = a.sb(b.s);
      subData[sb]! += b.len;
      const c = b.lane * SUBBINS + sb;
      laneBusy[c] = Math.min(0xffff, laneBusy[c]! + b.len);
      if (b.s + b.len > maxEnd) maxEnd = b.s + b.len;
    }
    const nCmd = a.nCmd;
    if (nCmd > 0) maxEnd = Math.max(maxEnd, a.lastT + this.maxCycles);

    let reads = 0;
    let writes = 0;
    let others = 0;
    for (let s = 0; s < SUBBINS; s++) {
      reads += a.subReads[s]!;
      writes += a.subWrites[s]!;
      others += a.subOthers[s]!;
    }

    let bankKinds: Uint8Array | null = null;
    let bankTime: Float32Array | null = null;
    let busCov: Float32Array | null = null;
    let fawCov: Float32Array | null = null;
    let busCovSub: Float32Array | null = null;
    let fawCovSub: Float32Array | null = null;
    const b = ctx.bank;
    if (b) {
      const NB = b.L.numBanks;
      bankTime = new Float32Array(NB * BANK_TIMES);
      const kindTime = new Float32Array(NB * SUBBINS * BLOCK_KINDS);
      const bus = groupPieces(a.iv[0], b.busDoms);
      const bg = b.bgDoms ? groupPieces(a.iv[1], b.bgDoms) : null;
      const faw = b.fawDoms ? groupPieces(a.iv[2], b.fawDoms) : null;
      const S = a.spanS.a;
      const E = a.spanE.a;
      const LN = a.spanLane.a;
      const K = a.spanKind.a;
      for (let j = 0; j < a.spanS.n; j++) {
        const s = S[j]!;
        const e = E[j]!;
        const lane = LN[j]!;
        const kind = K[j]!;
        const len = e - s;
        if (kind === BANK_KIND.OPEN) {
          const busD = bankDomOf(b.L, lane, b.L.busLevel);
          const busy = overlap(bus, busD, s, e);
          let bgOnly = 0;
          if (bg && b.L.bgLevel !== null) {
            const d = bankDomOf(b.L, lane, b.L.bgLevel);
            for (let q = firstEndingAfter(bg, d, s); q < bg.offsets[d + 1]!; q++) {
              if (bg.s[q]! >= e) break;
              const x = Math.max(bg.s[q]!, s);
              const y = Math.min(bg.e[q]!, e);
              if (y > x) bgOnly += (y - x) - overlap(bus, busD, x, y);
            }
          }
          bankTime[lane * BANK_TIMES + BANK_TIME.OPEN_BUS]! += busy;
          bankTime[lane * BANK_TIMES + BANK_TIME.OPEN_BG]! += bgOnly;
          bankTime[lane * BANK_TIMES + BANK_TIME.OPEN_STARVED]! += len - busy - bgOnly;
        } else {
          bankTime[lane * BANK_TIMES + kind]! += len;
        }
        // Time per kind in each sub-bin, for zoomed-out shading.
        for (let sb = Math.floor((s - o) / subW); sb < SUBBINS && o + sb * subW < e; sb++) {
          const x = Math.max(s, o + sb * subW);
          const y = Math.min(e, o + (sb + 1) * subW);
          if (y > x) kindTime[(lane * SUBBINS + sb) * BLOCK_KINDS + kind]! += y - x;
        }
      }
      bankKinds = new Uint8Array(NB * SUBBINS * BLOCK_KINDS);
      for (let q = 0; q < kindTime.length; q++) bankKinds[q] = Math.round((kindTime[q]! / subW) * 255);
      busCov = coverage(bus);
      fawCov = faw ? coverage(faw) : null;
      busCovSub = coverageBySub(bus, o, subW);
      fawCovSub = faw ? coverageBySub(faw, o, subW) : null;
    }

    this.out.summaries.push({
      k: a.k,
      span: a.span,
      cmdFirst: a.cmdFirst < 0 ? this.gidx : a.cmdFirst,
      nCmd,
      reads,
      writes,
      others,
      minStart,
      maxEnd,
      nReq: a.reqs.length,
      nBurst: a.bursts.length,
      nFan: a.nFan,
      subReads: a.subReads,
      subWrites: a.subWrites,
      subOthers: a.subOthers,
      subCmdCycles: a.subCmdCycles,
      subDataCycles: subData,
      laneCnt: a.laneCnt,
      laneTop: a.laneTop,
      laneBusy,
      laneReqs,
      bankKinds,
      bankTime,
      busCov,
      fawCov,
      busCovSub,
      fawCovSub,
    });
    if (detail) this.out.details.push(detail);
  }

  private pack(a: TileAcc): TileDetail {
    const o = a.origin;
    const L = this.ctx.levels;
    const f32 = (c: Col, sub = 0, idx?: Uint32Array) => {
      const out = new Float32Array(c.n);
      for (let q = 0; q < c.n; q++) out[q] = c.a[idx ? idx[q]! : q]! - sub;
      return out;
    };
    const as = <T extends Float64Array | Uint32Array | Uint16Array | Uint8Array | Int8Array | Int16Array | Int32Array>(
      make: (n: number) => T, c: Col, idx?: Uint32Array,
    ): T => {
      const out = make(c.n);
      if (idx) for (let q = 0; q < c.n; q++) out[q] = c.a[idx[q]!]!;
      else out.set(c.a.subarray(0, c.n));
      return out;
    };

    const reqs = a.reqs.length > 1 ? [...a.reqs].sort((x, y) => x.arrive - y.arrive || x.reqNo - y.reqNo) : a.reqs;
    const R = reqs.length;
    const reqCmdStart = new Uint32Array(R + 1);
    for (let j = 0; j < R; j++) reqCmdStart[j + 1] = reqCmdStart[j]! + reqs[j]!.cmds.length;
    const reqCmds = new Float64Array(reqCmdStart[R]!);
    const reqAddr = new Int32Array(R * L);
    for (let j = 0; j < R; j++) {
      reqCmds.set(reqs[j]!.cmds, reqCmdStart[j]!);
      reqAddr.set(reqs[j]!.addr, j * L);
    }

    // Spans by (bank, start); intervals by (domain, start). Each group is already in time order.
    const so = groupOrder(a.spanLane);
    const ivPieces = (iv: IvAcc, present: boolean): IntervalPieces | null => {
      if (!present) return null;
      const io = groupOrder(iv.dom);
      return {
        s: f32(iv.s, o, io),
        e: f32(iv.e, o, io),
        dom: as(n => new Uint16Array(n), iv.dom, io),
        cause: as(n => new Float64Array(n), iv.cause, io),
        trueS: as(n => new Float64Array(n), iv.trueS, io),
        trueE: as(n => new Float64Array(n), iv.trueE, io),
      };
    };
    const B = a.bursts;

    return {
      k: a.k,
      span: a.span,
      origin: o,
      cmdFirst: a.cmdFirst < 0 ? this.gidx : a.cmdFirst,
      cmdT: f32(a.cmdT, o),
      cmdLane: as(n => new Uint16Array(n), a.cmdLane),
      cmdId: as(n => new Uint8Array(n), a.cmdId),
      cmdReq: as(n => new Uint32Array(n), a.cmdReq),
      rawClk: as(n => new Float64Array(n), a.rawClk),
      rawArrive: as(n => new Float64Array(n), a.rawArrive),
      rawReqId: as(n => new Float64Array(n), a.rawReqId),
      rawType: as(n => new Int8Array(n), a.rawType),
      rawSrc: as(n => new Int16Array(n), a.rawSrc),
      rawAddr: as(n => new Int32Array(n), a.rawAddr),
      fanT: f32(a.fanT, o),
      fanLane: as(n => new Uint16Array(n), a.fanLane),
      fanCmd: as(n => new Uint8Array(n), a.fanCmd),
      fanIdx: as(n => new Float64Array(n), a.fanIdx),
      busT: Float32Array.from(B, x => x.s - o),
      busLane: Uint16Array.from(B, x => x.lane),
      busCmd: Uint8Array.from(B, x => x.cmd),
      busLen: Uint16Array.from(B, x => x.len),
      busIdx: Float64Array.from(B, x => x.gidx),
      reqT: Float32Array.from(reqs, r => r.arrive - o),
      reqDur: Float32Array.from(reqs, r => Math.max(r.depart - r.arrive, 1e-6)),
      reqQueue: Float32Array.from(reqs, r => r.first - r.arrive),
      reqLane: Uint16Array.from(reqs, r => r.lane),
      reqStack: Uint8Array.from(reqs, r => Math.min(r.row, 255)),
      reqFlags: Uint8Array.from(reqs, r => (r.type === 1 ? REQ_FLAG.WRITE : 0) | (r.inFlight ? REQ_FLAG.IN_FLIGHT : 0) | (r.type > 1 ? REQ_FLAG.OTHER_TYPE : 0)),
      reqNo: Float64Array.from(reqs, r => r.reqNo),
      reqArrive: Float64Array.from(reqs, r => r.arrive),
      reqId: Float64Array.from(reqs, r => r.id),
      reqSrc: Int16Array.from(reqs, r => r.src),
      reqAddr,
      reqCmdStart,
      reqCmds,
      spanS: f32(a.spanS, o, so),
      spanE: f32(a.spanE, o, so),
      spanLane: as(n => new Uint16Array(n), a.spanLane, so),
      spanKind: as(n => new Uint8Array(n), a.spanKind, so),
      spanCause: as(n => new Float64Array(n), a.spanCause, so),
      spanTrueS: as(n => new Float64Array(n), a.spanTrueS, so),
      spanTrueE: as(n => new Float64Array(n), a.spanTrueE, so),
      bus: ivPieces(a.iv[0], !!this.ivBus),
      bg: ivPieces(a.iv[1], !!this.ivBg),
      faw: ivPieces(a.iv[2], !!this.ivFaw),
    };
  }
}

/** Stable order of entries grouped by `key` (counting sort): time order is kept within a group. */
function groupOrder(key: Col): Uint32Array {
  const n = key.n;
  let max = 0;
  for (let q = 0; q < n; q++) if (key.a[q]! > max) max = key.a[q]!;
  const start = new Uint32Array(max + 2);
  for (let q = 0; q < n; q++) start[key.a[q]! + 1]!++;
  for (let g = 0; g <= max; g++) start[g + 1]! += start[g]!;
  const idx = new Uint32Array(n);
  for (let q = 0; q < n; q++) idx[start[key.a[q]!]!++] = q;
  return idx;
}

/** Interval pieces grouped by domain (absolute times), for overlap queries. */
interface DomPieces {
  offsets: Uint32Array;
  s: Float64Array;
  e: Float64Array;
}

function groupPieces(iv: IvAcc, doms: number): DomPieces {
  const n = iv.s.n;
  const offsets = new Uint32Array(doms + 1);
  for (let q = 0; q < n; q++) offsets[iv.dom.a[q]! + 1]!++;
  for (let d = 0; d < doms; d++) offsets[d + 1]! += offsets[d]!;
  const fill = offsets.slice(0, doms);
  const s = new Float64Array(n);
  const e = new Float64Array(n);
  for (let q = 0; q < n; q++) {
    const p = fill[iv.dom.a[q]!]!++;
    s[p] = iv.s.a[q]!;
    e[p] = iv.e.a[q]!;
  }
  return { offsets, s, e };
}

/** First piece of domain d that ends after t (pieces of a domain are disjoint and sorted). */
function firstEndingAfter(p: DomPieces, d: number, t: number): number {
  let lo = p.offsets[d]!;
  let hi = p.offsets[d + 1]!;
  while (lo < hi) {
    const mid = (lo + hi) >>> 1;
    if (p.e[mid]! <= t) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}

/** Length of [s, e) ∩ (domain d's pieces). */
function overlap(p: DomPieces, d: number, s: number, e: number): number {
  let sum = 0;
  for (let q = firstEndingAfter(p, d, s); q < p.offsets[d + 1]!; q++) {
    const a = p.s[q]!;
    if (a >= e) break;
    sum += Math.min(p.e[q]!, e) - Math.max(a, s);
  }
  return sum;
}

function coverage(p: DomPieces): Float32Array {
  const doms = p.offsets.length - 1;
  const out = new Float32Array(doms);
  for (let d = 0; d < doms; d++) {
    for (let q = p.offsets[d]!; q < p.offsets[d + 1]!; q++) out[d]! += p.e[q]! - p.s[q]!;
  }
  return out;
}

function coverageBySub(p: DomPieces, origin: number, subW: number): Float32Array {
  const doms = p.offsets.length - 1;
  const out = new Float32Array(doms * SUBBINS);
  for (let d = 0; d < doms; d++) {
    for (let q = p.offsets[d]!; q < p.offsets[d + 1]!; q++) {
      const s = p.s[q]!;
      const e = p.e[q]!;
      for (let sb = Math.max(0, Math.floor((s - origin) / subW)); sb < SUBBINS && origin + sb * subW < e; sb++) {
        const x = Math.max(s, origin + sb * subW);
        const y = Math.min(e, origin + (sb + 1) * subW);
        if (y > x) out[d * SUBBINS + sb]! += y - x;
      }
    }
  }
  return out;
}
