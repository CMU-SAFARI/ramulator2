// The page's handle on a trace held by the engine worker: tile summaries as they arrive, tile detail
// on request (cached up to a byte budget), and the trace's extent.
import type { TraceHeader, TraceSpec } from '../composables/useTrace';
import { makeContext, type Context } from './ingest';
import type { EngineRequest, EngineResponse, EngineSource } from './protocol';
import { recordAt, recordEnd, recordRange, type Records } from './stats';
import { NONE, TILE, type TileDetail, type TileSummary } from './types';
import EngineWorker from './engine.worker.ts?worker';

const DETAIL_BUDGET = 256 * 2 ** 20;
/** How long a closed engine's worker gets to release its storage. */
const CLOSE_TIMEOUT_MS = 2000;

function detailBytes(d: TileDetail): number {
  let n = 0;
  for (const v of Object.values(d)) {
    if (ArrayBuffer.isView(v)) n += v.byteLength;
    else if (v && typeof v === 'object') for (const w of Object.values(v)) if (ArrayBuffer.isView(w)) n += w.byteLength;
  }
  return n;
}

export class TraceEngine implements Records {
  readonly ctx: Context;
  /** Final records in tile order; `tail` is the live record still being filled. */
  final: TileSummary[] = [];
  tail: TileSummary | null = null;
  tiles: TileSummary[] = [];
  /** True end of spans / intervals cut at the end of a record: record k → (set << 20 | index) → end. */
  private crossEnds = new Map<number, Map<number, number>>();
  /** Request rows per lane. */
  maxRows: Uint16Array;
  entries = 0;
  total = 0;
  firstClk = Number.NaN;
  lastClk = Number.NaN;
  /** Latest end of anything drawn. */
  endClk = Number.NaN;
  /** A file's end, known from its last block before the file is read (NaN until then, and for live). */
  private knownEnd = Number.NaN;
  done = false;
  error: string | null = null;
  /** A live session: the trace grows until the run ends. */
  readonly live: boolean;
  /** Live: why the view stopped taking events (see the worker's LiveStore), or null. */
  liveFull: { reason: 'memory' | 'storage'; entries: number } | null = null;
  /** Bumps whenever summaries, detail or the extent change. */
  version = 0;

  private worker: Worker;
  private details = new Map<number, TileDetail>();
  private tailDetail: TileDetail | null = null;
  private bytes = 0;
  private requested = new Set<number>();
  private queued = new Set<number>();
  private listeners = new Set<() => void>();

  constructor(readonly header: TraceHeader, readonly spec: TraceSpec, source: EngineSource) {
    this.ctx = makeContext(spec, header.readLatency, header.dramType);
    this.maxRows = new Uint16Array(this.ctx.li.totalLanes).fill(1);
    this.total = header.numEntries;
    this.live = source.kind === 'live';
    this.worker = new EngineWorker();
    this.worker.onmessage = (e: MessageEvent<EngineResponse>) => this.receive(e.data);
    this.worker.onerror = e => this.fail(e.message || 'The trace could not be read.');
    this.send({ type: 'open', source, spec: toPlainSpec(spec), readLatency: header.readLatency, dramType: header.dramType });
  }

  onChange(cb: () => void): () => void {
    this.listeners.add(cb);
    return () => this.listeners.delete(cb);
  }

  private changed() {
    this.version++;
    for (const cb of this.listeners) cb();
  }

  private send(msg: EngineRequest) {
    this.worker.postMessage(msg);
  }

  private fail(message: string) {
    this.error = message;
    this.changed();
  }

  private receive(msg: EngineResponse) {
    switch (msg.type) {
      case 'summaries':
        for (const s of msg.tiles) this.final.push(s);
        if (this.tail && this.final.length && this.tail.k < this.final[this.final.length - 1]!.k + this.final[this.final.length - 1]!.span) {
          this.tail = null;
          this.tailDetail = null;
        }
        for (const c of msg.crossEnds) this.applyCrossEnd(c.fromK, c.toK, (c.set << 20) | c.index, c.end);
        this.maxRows = msg.maxRows;
        this.entries = msg.entries;
        this.firstClk = msg.firstClk;
        this.lastClk = msg.lastClk;
        this.refreshExtent();
        break;
      case 'detail':
        for (const d of msg.tiles) this.store(d);
        break;
      case 'tail':
        this.tail = msg.summary;
        this.tailDetail = msg.detail;
        this.refreshExtent();
        break;
      case 'progress':
        this.entries = msg.entries;
        this.total = msg.total;
        break;
      case 'extent':
        if (Number.isNaN(this.firstClk)) this.firstClk = msg.firstClk;
        this.knownEnd = msg.lastClk + 1;
        this.refreshExtent();
        break;
      case 'done':
        this.done = true;
        this.entries = msg.entries;
        this.lastClk = msg.lastClk;
        this.tail = null;
        this.tailDetail = null;
        this.refreshExtent();
        break;
      case 'error':
        this.fail(msg.message);
        return;
      case 'live-full':
        this.liveFull = { reason: msg.reason, entries: msg.entries };
        break;
      case 'closed':
        this.worker.terminate();
        return;
    }
    this.changed();
  }

  /** Clock up to which the trace is read: nothing beyond it is known yet (Infinity once read, and live). */
  get readFrontier(): number {
    if (this.done || this.live || this.error) return Infinity;
    const last = this.final[this.final.length - 1];
    return last ? recordEnd(last) : Number.isNaN(this.firstClk) ? -Infinity : this.firstClk;
  }

  private refreshExtent() {
    this.tiles = this.tail ? [...this.final, this.tail] : this.final;
    let end = this.endClk;
    if (!(this.knownEnd <= end)) end = this.knownEnd;
    for (let i = Math.max(0, this.tiles.length - 4); i < this.tiles.length; i++) {
      const m = this.tiles[i]!.maxEnd;
      if (!(m <= end)) end = m;
    }
    this.endClk = end;
  }

  private applyCrossEnd(fromK: number, toK: number, key: number, end: number) {
    // Records whose tile range intersects [fromK, toK).
    const [i0, i1] = recordRange(this.final, fromK * TILE, toK * TILE);
    for (let i = i0; i < i1; i++) {
      const k = this.final[i]!.k;
      let m = this.crossEnds.get(k);
      if (!m) this.crossEnds.set(k, (m = new Map()));
      m.set(key, end);
    }
  }

  /** True end of a span (set 0, index = bank) or interval (set 1 bus, 2 bg, 3 tFAW; index = domain) cut at the end of record k. */
  crossEnd(k: number, set: number, index: number): number | undefined {
    return this.crossEnds.get(k)?.get((set << 20) | index);
  }

  private store(d: TileDetail) {
    this.requested.delete(d.k);
    const old = this.details.get(d.k);
    if (old) this.bytes -= detailBytes(old);
    this.details.delete(d.k);
    this.details.set(d.k, d);
    this.bytes += detailBytes(d);
    while (this.bytes > DETAIL_BUDGET && this.details.size > 1) {
      const [k, v] = this.details.entries().next().value!;
      this.details.delete(k);
      this.bytes -= detailBytes(v);
    }
  }

  /** Detail of record k if cached; otherwise it is requested and a later change brings it. */
  detail(k: number): TileDetail | undefined {
    if (this.tail && k === this.tail.k) return this.tailDetail ?? undefined;
    const d = this.details.get(k);
    if (d) {
      // Most recently used last.
      this.details.delete(k);
      this.details.set(k, d);
      return d;
    }
    if (!this.requested.has(k)) {
      this.requested.add(k);
      this.queued.add(k);
      if (this.queued.size === 1) queueMicrotask(() => this.flushRequests());
    }
    return undefined;
  }

  /** Detail of record k only if already cached (no request). */
  peek(k: number): TileDetail | undefined {
    if (this.tail && k === this.tail.k) return this.tailDetail ?? undefined;
    return this.details.get(k);
  }

  private flushRequests() {
    if (!this.queued.size) return;
    this.send({ type: 'detail', keys: [...this.queued] });
    this.queued.clear();
  }

  /** Records intersecting [t0, t1). */
  range(t0: number, t1: number): [number, number] {
    return recordRange(this.tiles, t0, t1);
  }

  /** Index of the record containing time t, or -1. */
  recordIndexAt(t: number): number {
    const i = recordAt(this.tiles, t);
    return i >= 0 && t < recordEnd(this.tiles[i]!) ? i : -1;
  }

  /** Index of the record holding trace entry `gidx`, or -1. */
  recordOfEntry(gidx: number): number {
    const t = this.tiles;
    let lo = 0;
    let hi = t.length;
    while (lo < hi) {
      const mid = (lo + hi) >>> 1;
      if (t[mid]!.cmdFirst <= gidx) lo = mid + 1;
      else hi = mid;
    }
    // Gap records hold no commands and share cmdFirst with the next record: step back over them.
    let i = lo - 1;
    while (i >= 0 && t[i]!.nCmd === 0) i--;
    return i >= 0 && gidx < t[i]!.cmdFirst + t[i]!.nCmd ? i : -1;
  }

  // Live streams
  appendLive(columns: string) {
    this.send({ type: 'live', columns });
  }

  endLive() {
    this.send({ type: 'live-end' });
  }

  dispose() {
    // The worker ends itself once its live storage is released; the timer covers a worker that hangs.
    this.send({ type: 'close' });
    setTimeout(() => this.worker.terminate(), CLOSE_TIMEOUT_MS);
    this.listeners.clear();
    this.details.clear();
  }
}

/** The spec as plain data (it may be wrapped in reactive proxies). */
function toPlainSpec(spec: TraceSpec): TraceSpec {
  return {
    levelNames: [...spec.levelNames],
    levelSizes: Uint32Array.from(spec.levelSizes),
    commandNames: [...spec.commandNames],
    commandMeta: spec.commandMeta.map(m => ({ ...m })),
    commandCycles: Uint8Array.from(spec.commandCycles),
    timingNames: [...spec.timingNames],
    timingValues: Int32Array.from(spec.timingValues),
  };
}

export { NONE };
