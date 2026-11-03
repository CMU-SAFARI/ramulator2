// A run of consecutive trace entries, decoded for processing. Clocks and ids are held as doubles,
// which are exact up to 2^53.

export interface Chunk {
  /** Global index of the first entry. */
  first: number;
  n: number;
  clk: Float64Array;
  arrive: Float64Array;
  /** Request id per entry (format 1.2+), -1 for none; null for older formats. */
  reqId: Float64Array | null;
  cmd: Uint8Array;
  type: Int8Array;
  src: Int16Array;
  addr: Int32Array[];
}

/** Little-endian int64 column at `byteOffset` of `buf` as doubles. */
export function int64Column(buf: ArrayBuffer, byteOffset: number, n: number): Float64Array {
  const out = new Float64Array(n);
  if (byteOffset % 4 === 0) {
    const u = new Uint32Array(buf, byteOffset, n * 2);
    const s = new Int32Array(buf, byteOffset, n * 2);
    for (let i = 0; i < n; i++) out[i] = s[2 * i + 1]! * 4294967296 + u[2 * i]!;
  } else {
    const v = new DataView(buf, byteOffset, n * 8);
    for (let i = 0; i < n; i++) out[i] = v.getInt32(8 * i + 4, true) * 4294967296 + v.getUint32(8 * i, true);
  }
  return out;
}

/** Bytes of n entries in the format-1.2 chunk layout: an 8-byte header (n, reserved), then the columns. */
export function chunkBytes(n: number, levels: number): number {
  return 8 + n * (28 + 4 * levels);
}

/**
 * A format-1.2 chunk (header and columns) in `buf`: clk, arrive, req_id (int64), cmd (u8),
 * type (i8), src (i16), then one int32 column per address level. Every column of a chunk that
 * starts 8-byte aligned is aligned for its element size.
 */
export function decodeChunk(buf: ArrayBuffer, first: number, n: number, levels: number): Chunk {
  const s = 8;
  return {
    first,
    n,
    clk: int64Column(buf, s, n),
    arrive: int64Column(buf, s + 8 * n, n),
    reqId: int64Column(buf, s + 16 * n, n),
    cmd: new Uint8Array(buf, s + 24 * n, n),
    type: new Int8Array(buf, s + 25 * n, n),
    src: new Int16Array(buf, s + 26 * n, n),
    addr: Array.from({ length: levels }, (_, k) => new Int32Array(buf, s + (28 + 4 * k) * n, n)),
  };
}

/** Write a chunk into `out` (at least chunkBytes(c.n, levels) long) in the format-1.2 layout. */
export function encodeChunk(c: Chunk, out: ArrayBuffer, levels: number) {
  const n = c.n;
  const s = 8;
  new Uint32Array(out, 0, 2).set([n, 0]);
  const put64 = (col: Float64Array | null, at: number) => {
    const lo = new Uint32Array(out, at, 2 * n);
    const hi = new Int32Array(out, at, 2 * n);
    for (let i = 0; i < n; i++) {
      const x = col ? col[i]! : -1;
      const h = Math.floor(x / 4294967296);
      hi[2 * i + 1] = h;
      lo[2 * i] = x - h * 4294967296;
    }
  };
  put64(c.clk, s);
  put64(c.arrive, s + 8 * n);
  put64(c.reqId, s + 16 * n);
  new Uint8Array(out, s + 24 * n, n).set(c.cmd);
  new Int8Array(out, s + 25 * n, n).set(c.type);
  new Int16Array(out, s + 26 * n, n).set(c.src);
  for (let k = 0; k < levels; k++) new Int32Array(out, s + (28 + 4 * k) * n, n).set(c.addr[k]!);
}

/** Entries [a, b) of a chunk (views, not copies). */
export function sliceChunk(c: Chunk, a: number, b: number): Chunk {
  return {
    first: c.first + a,
    n: b - a,
    clk: c.clk.subarray(a, b),
    arrive: c.arrive.subarray(a, b),
    reqId: c.reqId ? c.reqId.subarray(a, b) : null,
    cmd: c.cmd.subarray(a, b),
    type: c.type.subarray(a, b),
    src: c.src.subarray(a, b),
    addr: c.addr.map(x => x.subarray(a, b)),
  };
}
