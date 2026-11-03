// Reading a .ram2bin trace in pieces, so a file never has to fit in memory: format 1.2 is read
// chunk by chunk; 1.0 / 1.1 files (one block of columns) are served as virtual chunks.
import { FLAG_COMPLETE, HEADER_SIZE, parseHeader, readSpec, type TraceHeader, type TraceSpec } from '../composables/useTrace';
import { decodeChunk, int64Column, type Chunk } from './chunk';

export const CHUNK_ENTRIES = 65536;

/** Random-access bytes: a File / Blob, an in-memory buffer, or a URL served with Range support. */
export interface ByteSource {
  size: number;
  read(offset: number, length: number): Promise<ArrayBuffer>;
}

export function blobSource(blob: Blob): ByteSource {
  return { size: blob.size, read: (o, n) => blob.slice(o, o + n).arrayBuffer() };
}

export function bufferSource(buf: ArrayBuffer): ByteSource {
  return { size: buf.byteLength, read: async (o, n) => buf.slice(o, o + n) };
}

/** A file served over HTTP; the server must honour Range requests. */
export async function httpSource(url: string): Promise<ByteSource> {
  const head = await fetch(url, { headers: { Range: 'bytes=0-0' } });
  if (!head.ok) throw new Error(head.status === 404 ? 'The file is no longer there.' : `HTTP ${head.status}`);
  const total = Number(head.headers.get('content-range')?.split('/')[1] ?? head.headers.get('content-length'));
  await head.body?.cancel();
  if (!Number.isFinite(total)) throw new Error('The server did not report the file size.');
  return {
    size: total,
    async read(o, n) {
      const res = await fetch(url, { headers: { Range: `bytes=${o}-${o + n - 1}` } });
      if (res.status !== 206 && !(res.status === 200 && o === 0 && n >= total)) throw new Error(`HTTP ${res.status} reading the trace`);
      return res.arrayBuffer();
    },
  };
}

export interface TraceFile {
  header: TraceHeader;
  spec: TraceSpec;
  /** The recording finished (always true before format 1.2). */
  complete: boolean;
  /** Entries readable from the file. */
  numEntries: number;
  chunkCount: number;
  /** Global index of chunk j's first entry. */
  chunkFirst(j: number): number;
  readChunk(j: number): Promise<Chunk>;
}

const MAX_SPEC_BYTES = 1 << 20;

function truncatedBlock(needed: number, size: number): Error {
  return new Error(
    `The trace file is truncated: its data needs ${needed.toLocaleString()} bytes, but the file has `
    + `${size.toLocaleString()}. It may have been cut off while copying.`,
  );
}

/** Header, spec and chunk layout of a trace; entries are read later, chunk by chunk. */
export async function openTrace(src: ByteSource): Promise<TraceFile> {
  if (src.size < HEADER_SIZE) throw new Error('The file is too short to be a RAM2BIN trace.');
  const head = await src.read(0, Math.min(src.size, MAX_SPEC_BYTES));
  const header = parseHeader(new DataView(head));
  const spec = readSpec(head, header);
  const L = header.levelCount;
  return header.version[1] < 2 ? openBlock(src, header, spec, L) : openChunked(src, head, header, spec, L);
}

/** Format 1.0 / 1.1: columns of numEntries entries each, starting at data_offset. */
function openBlock(src: ByteSource, header: TraceHeader, spec: TraceSpec, L: number): TraceFile {
  const D = header.dataOffset;
  const N = header.numEntries;
  if (N === 0) {
    throw new Error(
      'The trace contains no commands. Recorders before format 1.2 write nothing if the simulation '
      + 'is stopped early; record again with a current Ramulator build.',
    );
  }
  const end = D + (20 + 4 * L) * N;
  if (end > src.size) throw truncatedBlock(end, src.size);
  const chunkCount = Math.ceil(N / CHUNK_ENTRIES);
  return {
    header,
    spec,
    complete: true,
    numEntries: N,
    chunkCount,
    chunkFirst: j => j * CHUNK_ENTRIES,
    async readChunk(j) {
      const a = j * CHUNK_ENTRIES;
      const n = Math.min(CHUNK_ENTRIES, N - a);
      const col = (base: number, size: number) => src.read(D + base * N + a * size, n * size);
      const [clk, arrive, cmd, type, srcId, ...addr] = await Promise.all([
        col(0, 8), col(8, 8), col(16, 1), col(17, 1), col(18, 2), ...Array.from({ length: L }, (_, k) => col(20 + 4 * k, 4)),
      ]);
      return {
        first: a,
        n,
        clk: int64Column(clk!, 0, n),
        arrive: int64Column(arrive!, 0, n),
        reqId: null,
        cmd: new Uint8Array(cmd!),
        type: new Int8Array(type!),
        src: new Int16Array(srcId!),
        addr: addr.map(b => new Int32Array(b)),
      };
    },
  };
}

/**
 * Format 1.2: chunks from data_offset on. Every chunk but the last holds CHUNK_ENTRIES entries, so
 * chunk j starts at data_offset + j * (full chunk size). A recording that did not finish keeps its
 * complete chunks; a trailing partial chunk is ignored.
 */
async function openChunked(src: ByteSource, head: ArrayBuffer, header: TraceHeader, spec: TraceSpec, L: number): Promise<TraceFile> {
  const D = header.dataOffset;
  const stride = 28 + 4 * L;
  const pad8 = (x: number) => x + ((8 - (x % 8)) % 8);
  const full = pad8(8 + CHUNK_ENTRIES * stride);
  const complete = (new DataView(head).getUint16(10, true) & FLAG_COMPLETE) !== 0;
  if (complete) header.incomplete = undefined;
  else header.incomplete = true;

  const avail = Math.max(0, src.size - D);
  let chunks = Math.floor(avail / full);
  let lastN = CHUNK_ENTRIES;
  if (avail - chunks * full >= 8) {
    const h = new DataView(await src.read(D + chunks * full, 8));
    const n = h.getUint32(0, true);
    if (n > 0 && n < CHUNK_ENTRIES && h.getUint32(4, true) === 0 && D + chunks * full + 8 + n * stride <= src.size) {
      lastN = n;
      chunks++;
    }
  }
  if (chunks > 0 && lastN === CHUNK_ENTRIES) {
    // Check that the last full chunk really is full; otherwise the layout is not the recorder's.
    const h = new DataView(await src.read(D + (chunks - 1) * full, 8));
    if (h.getUint32(0, true) !== CHUNK_ENTRIES) throw new Error('The trace file is damaged: unexpected chunk layout.');
  }
  const total = chunks === 0 ? 0 : (chunks - 1) * CHUNK_ENTRIES + lastN;

  if (complete && total !== header.numEntries) {
    throw new Error(total < header.numEntries
      ? `The trace file is truncated: its header lists ${header.numEntries.toLocaleString()} commands, but only `
        + `${total.toLocaleString()} are in the file. It may have been cut off while copying.`
      : 'The trace file is damaged: its chunks hold more commands than its header lists.');
  }
  if (total === 0) {
    throw new Error(complete
      ? 'The trace contains no commands.'
      : 'The trace contains no commands: the simulation stopped before the recorder wrote its first block of commands.');
  }
  header.numEntries = total;

  return {
    header,
    spec,
    complete,
    numEntries: total,
    chunkCount: chunks,
    chunkFirst: j => j * CHUNK_ENTRIES,
    async readChunk(j) {
      const n = j === chunks - 1 ? lastN : CHUNK_ENTRIES;
      const buf = await src.read(D + j * full, 8 + n * stride);
      const got = new DataView(buf).getUint32(0, true);
      if (got !== n) throw new Error('The trace file is damaged: unexpected chunk layout.');
      return decodeChunk(buf, j * CHUNK_ENTRIES, n, L);
    },
  };
}
