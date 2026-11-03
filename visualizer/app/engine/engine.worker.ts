// The engine worker: owns the trace (a file read in pieces, or a live stream), runs the ingest pass
// and serves tile detail, rebuilding passed tiles from the nearest checkpoint.
import type { TraceSpec } from '../composables/useTrace';
import { chunkBytes, decodeChunk, encodeChunk, sliceChunk, type Chunk } from './chunk';
import { Ingest, makeContext, type Checkpoint, type Context } from './ingest';
import { blobSource, CHUNK_ENTRIES, httpSource, openTrace, type TraceFile } from './source';
import { SUBBINS, TILE, type CrossEnd, type TileDetail, type TileSummary } from './types';
import type { EngineRequest, EngineResponse, EngineSource } from './protocol';
import { decodeIndex, encodeIndex, hashKey, INDEX_VERSION, type TraceIndex } from './indexFile';

const CHECKPOINT_EVERY = 4;
/** Tiles at the start of a trace whose detail is kept for the first paint. */
const FIRST_TILES = 4;
/** Live: the newest completed records whose detail goes to the page unasked (next to the live edge). */
const LIVE_RECENT = 2;
const POST_INTERVAL_MS = 60;

let ctx: Context | null = null;
let file: TraceFile | null = null;
let live: LiveStore | null = null;
let main: Ingest | null = null;
const checkpoints: Checkpoint[] = [];
/** Records the page wants detail for. */
const wanted = new Set<number>();
/** Tile index up to which records are final. */
let doneK = -Infinity;
let ended = false;
let pendingSummaries: TileSummary[] = [];
/** A file's first read keeps everything its index needs (see indexFile.ts). */
let allSummaries: TileSummary[] = [];
let allCrossEnds: CrossEnd[] = [];
let lastPost = 0;
let generation = 0;
const rebuildQueue = new Set<number>();
let rebuilding = false;

function post(msg: EngineResponse, transfer: Transferable[] = []) {
  (self as unknown as Worker).postMessage(msg, transfer);
}

function detailBuffers(d: TileDetail): ArrayBuffer[] {
  const out: ArrayBuffer[] = [];
  for (const v of Object.values(d)) {
    if (ArrayBuffer.isView(v)) out.push(v.buffer as ArrayBuffer);
    else if (v && typeof v === 'object') for (const w of Object.values(v)) if (ArrayBuffer.isView(w)) out.push(w.buffer as ArrayBuffer);
  }
  return [...new Set(out)];
}

/** Hand the main pass's new output to the page. */
function flush(force = false) {
  if (!main) return;
  const out = main.out;
  for (const s of out.summaries) {
    pendingSummaries.push(s);
    doneK = s.k + s.span;
  }
  if (file) allSummaries.push(...out.summaries);
  out.summaries = [];
  // A live ingest keeps every record (each is the live edge while it fills); only the newest and
  // the asked-for ones go to the page, the others are rebuilt on demand.
  const recent = live ? new Set(out.details.slice(-LIVE_RECENT).map(d => d.k)) : null;
  for (const d of out.details) {
    if (recent && !recent.has(d.k) && !wanted.has(d.k)) continue;
    wanted.delete(d.k);
    post({ type: 'detail', gen: generation, tiles: [d] }, detailBuffers(d));
  }
  out.details = [];
  const now = performance.now();
  if (!force && now - lastPost < POST_INTERVAL_MS) return;
  lastPost = now;
  if (pendingSummaries.length || out.crossEnds.length) {
    if (file) allCrossEnds.push(...out.crossEnds);
    post({
      type: 'summaries',
      gen: generation,
      tiles: pendingSummaries,
      crossEnds: out.crossEnds,
      maxRows: Uint16Array.from(main.req.maxRows),
      entries: main.gidx,
      firstClk: main.firstClk,
      lastClk: main.lastClk,
    });
    pendingSummaries = [];
    out.crossEnds = [];
  }
}

function chunkCount(): number {
  return file ? file.chunkCount : live!.chunkCount();
}

/** Rebuild detail for requested records the main pass has already passed. */
async function serveRebuilds() {
  if (rebuilding || !ctx) return;
  rebuilding = true;
  try {
    while (rebuildQueue.size) {
      const gen = generation;
      const keys = [...rebuildQueue].sort((a, b) => a - b);
      const k = keys[0]!;
      const cp = findCheckpoint(k);
      if (!cp) {
        rebuildQueue.delete(k);
        continue;
      }
      // One pass from the checkpoint serves every queued record before the next checkpoint.
      const next = checkpoints.find(c => c.k > cp.k)?.k ?? Infinity;
      const batch = new Set(keys.filter(x => x >= cp.k && x < next));
      const ing = new Ingest(ctx, { wantDetail: x => batch.has(x), rebuild: true }, cp);
      let j = Math.floor(cp.gidx / CHUNK_ENTRIES);
      let off = cp.gidx - j * CHUNK_ENTRIES;
      const last = Math.max(...batch);
      while (j < chunkCount() && (ing.currentTile ?? -Infinity) <= last) {
        // Live chunks are in memory or in a synchronous file: no await, so incoming batches do not
        // get ahead of the rebuild.
        const c = file ? await file.readChunk(j) : live!.chunk(j);
        if (gen !== generation) return;
        ing.push(off > 0 ? sliceChunk(c, off, c.n) : c);
        off = 0;
        j++;
      }
      if ((ing.currentTile ?? -Infinity) <= last && ended) ing.finish();
      for (const x of batch) rebuildQueue.delete(x);
      const tiles = ing.out.details;
      if (tiles.length) post({ type: 'detail', gen: generation, tiles }, tiles.flatMap(detailBuffers));
    }
  } finally {
    rebuilding = false;
  }
}

function findCheckpoint(k: number): Checkpoint | undefined {
  let best: Checkpoint | undefined;
  for (const cp of checkpoints) {
    if (cp.k <= k) best = cp;
    else break;
  }
  return best;
}

function requestDetail(keys: number[]) {
  for (const k of keys) {
    if (k < doneK) rebuildQueue.add(k);
    else wanted.add(k);
  }
  void serveRebuilds();
}

function newIngest(): Ingest {
  return new Ingest(ctx!, {
    wantDetail: k => !!live || wanted.has(k) || k < (main?.firstK ?? k) + FIRST_TILES,
    onCheckpoint: cp => checkpoints.push(cp),
    checkpointEvery: CHECKPOINT_EVERY,
  });
}

async function openFile(msg: Extract<EngineRequest, { type: 'open' }>) {
  const gen = generation;
  const s = msg.source;
  if (s.kind === 'live') return;
  const src = s.kind === 'blob' ? blobSource(s.blob) : await httpSource(s.url);
  file = await openTrace(src);
  const total = file.numEntries;
  // Where the trace starts and ends, before reading it all: the page lays out the whole trace at once.
  const first = await file.readChunk(0);
  const last = file.chunkCount > 1 ? await file.readChunk(file.chunkCount - 1) : first;
  if (gen !== generation) return;
  const firstClk = first.clk[0]!;
  const lastClk = last.clk[last.n - 1]!;
  post({ type: 'extent', gen, firstClk, lastClk });

  // A trace read before with the same settings opens from its index.
  const identity = `${total}|${file.chunkCount}|${firstClk}|${lastClk}|${settingsKey(msg)}`;
  const store = indexStore(s, settingsKey(msg));
  const saved = store ? await store.load().catch(() => null) : null;
  if (gen !== generation) return;
  if (saved && saved.identity === identity) {
    checkpoints.push(...saved.checkpoints);
    ended = true;
    doneK = Infinity;
    post({
      type: 'summaries', gen, tiles: saved.summaries, crossEnds: saved.crossEnds, maxRows: Uint16Array.from(saved.maxRows),
      entries: saved.entries, firstClk: saved.firstClk, lastClk: saved.lastClk,
    });
    post({ type: 'done', gen, entries: total, lastClk: saved.lastClk });
    void serveRebuilds();
    return;
  }

  main = newIngest();
  for (let j = 0; j < file.chunkCount; j++) {
    const c = j === 0 ? first : await file.readChunk(j);
    if (gen !== generation) return;
    main.push(c);
    flush();
    post({ type: 'progress', gen, entries: Math.min(total, c.first + c.n), total });
    if (rebuildQueue.size) await serveRebuilds();
  }
  main.finish();
  ended = true;
  doneK = Infinity;
  flush(true);
  post({ type: 'done', gen, entries: total, lastClk: main.lastClk });
  void serveRebuilds();
  if (store) {
    const index: TraceIndex = {
      version: INDEX_VERSION, identity, summaries: allSummaries, crossEnds: allCrossEnds, checkpoints,
      maxRows: [...main.req.maxRows], entries: total, firstClk: main.firstClk, lastClk: main.lastClk,
    };
    void store.save(encodeIndex(index)).catch(() => {});
  }
}

/** What a trace's records depend on besides the file: its spec and timing overrides, and the engine. */
function settingsKey(msg: Extract<EngineRequest, { type: 'open' }>): string {
  return hashKey(JSON.stringify(
    { v: INDEX_VERSION, tile: TILE, subbins: SUBBINS, every: CHECKPOINT_EVERY, spec: msg.spec, readLatency: msg.readLatency, dramType: msg.dramType },
    (_key, v) => (ArrayBuffer.isView(v) ? Array.from(v as unknown as ArrayLike<number>) : v),
  ));
}

const INDEX_DIR = 'ramviz-index';
/** Indexes of uploaded traces kept in the browser's storage; the oldest go first. */
const INDEX_KEEP = 8;

interface IndexStore {
  load(): Promise<TraceIndex | null>;
  save(bytes: Uint8Array<ArrayBuffer>): Promise<void>;
}

/**
 * Where a trace's index is kept: on the server for its traces (shared by everyone who opens them),
 * in this browser's storage for uploaded files. Nowhere for other sources.
 */
function indexStore(s: EngineSource, key: string): IndexStore | null {
  if (s.kind === 'url' && s.index) {
    const url = `${s.index}&key=${key}`;
    return {
      async load() {
        const res = await fetch(url);
        return res.ok ? decodeIndex(await res.arrayBuffer()) : null;
      },
      async save(bytes) {
        await fetch(url, { method: 'PUT', headers: { 'content-type': 'application/octet-stream' }, body: bytes });
      },
    };
  }
  if (s.kind === 'blob' && s.blob instanceof File) {
    const name = `${hashKey(`${s.blob.name}|${s.blob.size}|${s.blob.lastModified}|${key}`)}.idx`;
    const dir = async () => (await navigator.storage.getDirectory()).getDirectoryHandle(INDEX_DIR, { create: true });
    return {
      async load() {
        const handle = await (await dir()).getFileHandle(name);
        return decodeIndex(await (await handle.getFile()).arrayBuffer());
      },
      async save(bytes) {
        const d = await dir();
        const h = await (await d.getFileHandle(name, { create: true }) as unknown as { createSyncAccessHandle(): Promise<SyncHandle> }).createSyncAccessHandle();
        try {
          h.truncate(0);
          h.write(bytes, { at: 0 });
          h.flush();
        } finally {
          h.close();
        }
        const files: [string, number][] = [];
        for await (const n of (d as unknown as { keys(): AsyncIterable<string> }).keys()) {
          files.push([n, (await (await d.getFileHandle(n)).getFile()).lastModified]);
        }
        files.sort((x, y) => y[1] - x[1]);
        for (const [n] of files.slice(INDEX_KEEP)) await d.removeEntry(n).catch(() => {});
      },
    };
  }
  return null;
}

/** Without the origin-private file system, live views keep this many commands in memory. */
const MEMORY_ENTRIES = 10_000_000;
const SPILL_DIR = 'ramviz-live';

/** The parts of FileSystemSyncAccessHandle used here (dedicated workers only). */
interface SyncHandle {
  read(buffer: Uint8Array, options: { at: number }): number;
  write(buffer: Uint8Array, options: { at: number }): number;
  truncate(size: number): void;
  flush(): void;
  close(): void;
}

/**
 * Complete chunks of a live session in a file of the origin-private file system, slot j holding
 * chunk j in the format-1.2 layout. The file is removed when the session closes; files left by
 * pages that closed without that are removed on the next open.
 */
class SpillFile {
  private constructor(private dir: FileSystemDirectoryHandle, private name: string, private h: SyncHandle, private slot: number) {}

  static async open(slot: number): Promise<SpillFile | null> {
    try {
      const dir = await (await navigator.storage.getDirectory()).getDirectoryHandle(SPILL_DIR, { create: true });
      const name = `live-${Date.now()}-${Math.random().toString(36).slice(2)}.bin`;
      const file = await dir.getFileHandle(name, { create: true });
      const h = await (file as unknown as { createSyncAccessHandle(): Promise<SyncHandle> }).createSyncAccessHandle();
      return new SpillFile(dir, name, h, slot);
    } catch {
      return null;
    }
  }

  /** Throws when the browser's storage is full. */
  write(j: number, bytes: Uint8Array) {
    if (this.h.write(bytes, { at: j * this.slot }) !== bytes.length) throw new Error('short write');
  }

  read(j: number, length: number): ArrayBuffer {
    const buf = new ArrayBuffer(length);
    this.h.read(new Uint8Array(buf), { at: j * this.slot });
    return buf;
  }

  async remove() {
    this.h.close();
    await this.dir.removeEntry(this.name).catch(() => {});
  }
}

/** Remove spill files no open page holds (a file in use is locked, and its removal fails). */
async function sweepSpills() {
  try {
    const dir = await (await navigator.storage.getDirectory()).getDirectoryHandle(SPILL_DIR, { create: true });
    const names: string[] = [];
    for await (const name of (dir as unknown as { keys(): AsyncIterable<string> }).keys()) names.push(name);
    await Promise.all(names.map(n => dir.removeEntry(n).catch(() => {})));
  } catch {
    // No origin-private file system here.
  }
}

/**
 * Live events in CHUNK_ENTRIES-entry chunks, kept for rebuilding passed tiles. Complete chunks go
 * to a spill file, so a live view grows as long as the browser has storage; only the chunk being
 * filled stays in memory. Without the file system, chunks stay in memory up to MEMORY_ENTRIES.
 */
class LiveStore {
  count = 0;
  /** Why the store stopped taking events, or null. */
  full: 'memory' | 'storage' | null = null;
  private disk: SpillFile | null = null;
  private diskReady: Promise<void>;
  /** Chunks 0 .. onDisk-1 are in the spill file; sealed chunks after them are in `mem`. */
  private onDisk = 0;
  private mem: Chunk[] = [];
  private cur: Chunk;
  private bytes: ArrayBuffer;
  private closed = false;

  constructor(private levels: number) {
    this.cur = {
      first: 0, n: 0,
      clk: new Float64Array(CHUNK_ENTRIES), arrive: new Float64Array(CHUNK_ENTRIES), reqId: new Float64Array(CHUNK_ENTRIES),
      cmd: new Uint8Array(CHUNK_ENTRIES), type: new Int8Array(CHUNK_ENTRIES), src: new Int16Array(CHUNK_ENTRIES),
      addr: Array.from({ length: levels }, () => new Int32Array(CHUNK_ENTRIES)),
    };
    this.bytes = new ArrayBuffer(chunkBytes(CHUNK_ENTRIES, levels));
    this.diskReady = sweepSpills().then(() => SpillFile.open(this.bytes.byteLength)).then((d) => {
      if (this.closed) void d?.remove();
      else this.disk = d;
      this.spillMem();
    });
  }

  private get sealed(): number {
    return this.onDisk + this.mem.length;
  }

  /** Move chunks held in memory to the spill file. */
  private spillMem() {
    while (this.disk && this.mem.length && !this.full) {
      if (!this.writeChunk(this.mem[0]!)) return;
      this.mem.shift();
    }
  }

  private writeChunk(c: Chunk): boolean {
    try {
      encodeChunk(c, this.bytes, this.levels);
      this.disk!.write(this.onDisk, new Uint8Array(this.bytes));
      this.onDisk++;
      return true;
    } catch {
      this.full = 'storage';
      return false;
    }
  }

  /** Copies of the current chunk's entries. */
  private copyCur(): Chunk {
    const c = this.cur;
    const n = c.n;
    return {
      first: this.sealed * CHUNK_ENTRIES, n,
      clk: c.clk.slice(0, n), arrive: c.arrive.slice(0, n), reqId: c.reqId!.slice(0, n),
      cmd: c.cmd.slice(0, n), type: c.type.slice(0, n), src: c.src.slice(0, n),
      addr: c.addr.map(a => a.slice(0, n)),
    };
  }

  private seal() {
    if (this.disk && !this.mem.length) {
      this.cur.first = this.onDisk * CHUNK_ENTRIES;
      this.writeChunk(this.cur);
    } else {
      this.mem.push(this.copyCur());
    }
    this.cur.n = 0;
  }

  /** Append a batch of stream columns; returns them as a chunk for the ingest, or null once full. */
  append(col: LiveColumns): Chunk | null {
    if (this.full) return null;
    if (!this.disk && this.count + col.n > MEMORY_ENTRIES) {
      this.full = 'memory';
      return null;
    }
    const first = this.count;
    const n = col.n;
    const reqId = new Float64Array(n);
    const out: Chunk = {
      first, n,
      clk: new Float64Array(n), arrive: new Float64Array(n), reqId,
      cmd: Uint8Array.from(col.cmd), type: Int8Array.from(col.type), src: Int16Array.from(col.src),
      addr: col.addr.map(a => Int32Array.from(a)),
    };
    let clk = 0;
    let req = 0;
    for (let i = 0; i < n; i++) {
      clk += col.clk[i]!;
      req += col.req[i]!;
      out.clk[i] = clk;
      out.arrive[i] = col.wait[i]! < 0 ? -1 : clk - col.wait[i]!;
      reqId[i] = req;
    }
    const c = this.cur;
    for (let i = 0; i < n;) {
      const m = Math.min(n - i, CHUNK_ENTRIES - c.n);
      c.clk.set(out.clk.subarray(i, i + m), c.n);
      c.arrive.set(out.arrive.subarray(i, i + m), c.n);
      c.reqId!.set(reqId.subarray(i, i + m), c.n);
      c.cmd.set(out.cmd.subarray(i, i + m), c.n);
      c.type.set(out.type.subarray(i, i + m), c.n);
      c.src.set(out.src.subarray(i, i + m), c.n);
      for (let k = 0; k < this.levels; k++) c.addr[k]!.set(out.addr[k]!.subarray(i, i + m), c.n);
      c.n += m;
      i += m;
      if (c.n === CHUNK_ENTRIES) this.seal();
    }
    this.count += n;
    return out;
  }

  chunkCount(): number {
    return this.sealed + (this.cur.n ? 1 : 0);
  }

  chunk(j: number): Chunk {
    if (j < this.onDisk) return decodeChunk(this.disk!.read(j, this.bytes.byteLength), j * CHUNK_ENTRIES, CHUNK_ENTRIES, this.levels);
    if (j < this.sealed) return this.mem[j - this.onDisk]!;
    return this.copyCur();
  }

  /** Close and remove the spill file. */
  async close() {
    this.closed = true;
    await this.diskReady;
    await this.disk?.remove();
    this.disk = null;
  }
}

interface LiveColumns {
  n: number;
  clk: number[];
  wait: number[];
  req: number[];
  cmd: number[];
  type: number[];
  src: number[];
  addr: number[][];
}

let tailTimer: ReturnType<typeof setTimeout> | null = null;
let fullPosted = false;

function postTail() {
  tailTimer = null;
  if (!main || ended) return;
  const p = main.preview(main.lastClk + 1);
  if (p) post({ type: 'tail', gen: generation, summary: p.summary, detail: p.detail }, detailBuffers(p.detail));
}

function liveAppend(json: string) {
  if (!live || !main || ended) return;
  let col: LiveColumns;
  try {
    col = JSON.parse(json);
  } catch {
    return;
  }
  const c = live.append(col);
  if (!c) {
    if (!fullPosted) post({ type: 'live-full', gen: generation, reason: live.full!, entries: live.count });
    fullPosted = true;
    return;
  }
  main.push(c);
  flush();
  tailTimer ??= setTimeout(postTail, 100);
}

function reset() {
  generation++;
  ctx = null;
  file = null;
  void live?.close();
  live = null;
  fullPosted = false;
  main = null;
  checkpoints.length = 0;
  wanted.clear();
  rebuildQueue.clear();
  doneK = -Infinity;
  ended = false;
  pendingSummaries = [];
  allSummaries = [];
  allCrossEnds = [];
  if (tailTimer) clearTimeout(tailTimer);
  tailTimer = null;
}

self.onmessage = (e: MessageEvent<EngineRequest>) => {
  const msg = e.data;
  try {
    switch (msg.type) {
      case 'open':
        reset();
        ctx = makeContext(msg.spec as TraceSpec, msg.readLatency, msg.dramType);
        if (msg.source.kind === 'live') {
          live = new LiveStore(ctx.levels);
          main = newIngest();
        } else {
          void sweepSpills();
          const gen = generation;
          openFile(msg).catch(err => post({ type: 'error', gen, message: err instanceof Error ? err.message : String(err) }));
        }
        break;
      case 'detail':
        requestDetail(msg.keys);
        break;
      case 'live':
        liveAppend(msg.columns);
        break;
      case 'live-end':
        if (main && live && !ended) {
          main.finish();
          ended = true;
          doneK = Infinity;
          flush(true);
          post({ type: 'done', gen: generation, entries: live.count, lastClk: main.lastClk });
        }
        break;
      case 'close': {
        const store = live;
        live = null;
        reset();
        // The page waits for this before ending the worker, so the spill file is gone.
        void (store?.close() ?? Promise.resolve()).then(() => post({ type: 'closed', gen: generation }));
        break;
      }
    }
  } catch (err) {
    post({ type: 'error', gen: generation, message: err instanceof Error ? err.message : String(err) });
  }
};
