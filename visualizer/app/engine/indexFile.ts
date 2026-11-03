// A trace's index: what the first full read of a trace produces (record summaries, the true ends of
// cut spans, request rows and the state checkpoints), saved so a later open skips that read.
//
// File layout: "RAMVIZIX", u32 format, u32 JSON byte length, u32 array count, u32 0, then per typed
// array an (offset, byte length) pair of f64, the JSON, and the arrays, each 8-byte aligned. The
// JSON holds plain data; typed arrays and non-finite numbers stand in it as { $ta, i } / { $num }.
import type { TileSummary, CrossEnd } from './types';
import type { Checkpoint } from './ingest';

/** Bump when the engine's records or checkpoints change: older indexes are then ignored. */
export const INDEX_VERSION = 1;
const MAGIC = 'RAMVIZIX';
const FORMAT = 1;

export interface TraceIndex {
  version: number;
  /** What the index belongs to: the trace's entries, first and last clocks, and the settings key. */
  identity: string;
  summaries: TileSummary[];
  crossEnds: CrossEnd[];
  checkpoints: Checkpoint[];
  maxRows: number[];
  entries: number;
  firstClk: number;
  lastClk: number;
}

const ARRAYS = {
  Float64Array, Float32Array, Int32Array, Uint32Array, Int16Array, Uint16Array, Int8Array, Uint8Array,
} as const;
type ArrayName = keyof typeof ARRAYS;

const pad8 = (x: number) => x + ((8 - (x % 8)) % 8);

export function encodeIndex(index: TraceIndex): Uint8Array<ArrayBuffer> {
  const arrays: ArrayBufferView[] = [];
  const json = new TextEncoder().encode(JSON.stringify(index, (_key, v) => {
    if (ArrayBuffer.isView(v)) {
      arrays.push(v);
      return { $ta: v.constructor.name, i: arrays.length - 1 };
    }
    if (typeof v === 'number' && !Number.isFinite(v)) return { $num: String(v) };
    return v;
  }));
  const tableAt = 24;
  const jsonAt = tableAt + arrays.length * 16;
  let at = pad8(jsonAt + json.length);
  const offsets = arrays.map((a) => {
    const o = at;
    at = pad8(at + a.byteLength);
    return o;
  });
  const out = new Uint8Array(at);
  const view = new DataView(out.buffer);
  out.set(new TextEncoder().encode(MAGIC), 0);
  view.setUint32(8, FORMAT, true);
  view.setUint32(12, json.length, true);
  view.setUint32(16, arrays.length, true);
  arrays.forEach((a, i) => {
    view.setFloat64(tableAt + i * 16, offsets[i]!, true);
    view.setFloat64(tableAt + i * 16 + 8, a.byteLength, true);
    out.set(new Uint8Array(a.buffer, a.byteOffset, a.byteLength), offsets[i]!);
  });
  out.set(json, jsonAt);
  return out;
}

/** The index in `buf`, or null if it is not one this engine can read. */
export function decodeIndex(buf: ArrayBuffer): TraceIndex | null {
  if (buf.byteLength < 24 || new TextDecoder().decode(new Uint8Array(buf, 0, 8)) !== MAGIC) return null;
  const view = new DataView(buf);
  if (view.getUint32(8, true) !== FORMAT) return null;
  const jsonLength = view.getUint32(12, true);
  const count = view.getUint32(16, true);
  const jsonAt = 24 + count * 16;
  if (jsonAt + jsonLength > buf.byteLength) return null;
  const table = Array.from({ length: count }, (_, i) => [view.getFloat64(24 + i * 16, true), view.getFloat64(24 + i * 16 + 8, true)] as const);
  try {
    const index = JSON.parse(new TextDecoder().decode(new Uint8Array(buf, jsonAt, jsonLength)), (_key, v) => {
      if (v && typeof v === 'object' && typeof v.$ta === 'string') {
        const Ctor = ARRAYS[v.$ta as ArrayName];
        const [offset, length] = table[v.i as number]!;
        // A copy: the arrays outlive the buffer and may be handed on to the page.
        return new Ctor(buf.slice(offset, offset + length));
      }
      if (v && typeof v === 'object' && typeof v.$num === 'string') return Number(v.$num);
      return v;
    }) as TraceIndex;
    return index.version === INDEX_VERSION ? index : null;
  } catch {
    return null;
  }
}

/** A short hash (FNV-1a, two 32-bit lanes) for cache keys; not a security measure. */
export function hashKey(text: string): string {
  let a = 0x811c9dc5;
  let b = 0x01000193 ^ text.length;
  for (let i = 0; i < text.length; i++) {
    const c = text.charCodeAt(i);
    a = Math.imul(a ^ c, 0x01000193);
    b = Math.imul(b ^ c, 0x5bd1e995) ^ (b >>> 15);
  }
  return (a >>> 0).toString(16).padStart(8, '0') + (b >>> 0).toString(16).padStart(8, '0');
}
