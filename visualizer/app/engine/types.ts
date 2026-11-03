// Tiles: fixed slices of the absolute clock. Every view draws and counts per tile; a tile's times
// are Float32 offsets from its origin, exact because a tile spans far fewer than 2^24 cycles.

export const TILE_SHIFT = 16;
export const TILE = 2 ** TILE_SHIFT;
/** Summary resolution inside a tile (zoomed-out blocks, throughput, statistics at tile edges). */
export const SUBBINS = 16;
/** A run of tiles without commands is one gap record of at most this many tiles. */
export const MAX_GAP_TILES = 256;
export const NONE = 0xffffffff;
/** Request rows drawn per lane; deeper overlap shares the last row. */
export const REQ_STACKS_DRAWN = 8;

export const REQ_FLAG = { WRITE: 1, IN_FLIGHT: 2, OTHER_TYPE: 4 } as const;

/** Bank-time categories per tile and bank: the span kinds, with open split by what blocks it. */
export const BANK_TIME = { IDLE: 0, ACT: 1, PRE: 2, WRREC: 3, REF: 4, OPEN_STARVED: 5, OPEN_BUS: 6, OPEN_BG: 7 } as const;
export const BANK_TIMES = 8;
/** Span kinds shaded in zoomed-out blocks (BANK_KIND values 0..5). */
export const BLOCK_KINDS = 6;

export function tileOf(t: number): number {
  return Math.floor(t / TILE);
}

/** Merged intervals of one kind (data bus busy, bank group blocked, tFAW) within a tile. */
export interface IntervalPieces {
  s: Float32Array;
  e: Float32Array;
  dom: Uint16Array;
  cause: Float64Array;
  /** True extent of the merged interval; NaN end = continues past the tile, see crossEnd. */
  trueS: Float64Array;
  trueE: Float64Array;
}

/** Everything drawn and hit-tested in one tile record (rebuilt on demand from the trace). */
export interface TileDetail {
  k: number;
  /** Tiles covered: 1, or more for a gap without commands. */
  span: number;
  origin: number;
  // Commands on their own lane, in trace (= time) order; entry j is trace entry cmdFirst + j.
  cmdFirst: number;
  cmdT: Float32Array;
  cmdLane: Uint16Array;
  cmdId: Uint8Array;
  /** Request number of each command (NONE without a request). */
  cmdReq: Uint32Array;
  // Raw columns of those commands, for the inspector.
  rawClk: Float64Array;
  rawArrive: Float64Array;
  /** Request id in the trace (format 1.2), -1 without one. */
  rawReqId: Float64Array;
  rawType: Int8Array;
  rawSrc: Int16Array;
  /** Address of command j at level k: rawAddr[j * levels + k]. */
  rawAddr: Int32Array;
  // Extra instances of multi-bank commands on their other lanes, in time order.
  fanT: Float32Array;
  fanLane: Uint16Array;
  fanCmd: Uint8Array;
  fanIdx: Float64Array;
  // Data-bus bursts, by bus time.
  busT: Float32Array;
  busLane: Uint16Array;
  busCmd: Uint8Array;
  busLen: Uint16Array;
  busIdx: Float64Array;
  // Requests completed in this tile, by arrival (offsets may be negative: arrival in an earlier tile).
  reqT: Float32Array;
  reqDur: Float32Array;
  reqQueue: Float32Array;
  reqLane: Uint16Array;
  reqStack: Uint8Array;
  reqFlags: Uint8Array;
  reqNo: Float64Array;
  reqArrive: Float64Array;
  /** Request id in the trace (format 1.2), -1 without one. */
  reqId: Float64Array;
  reqSrc: Int16Array;
  reqAddr: Int32Array;
  /** Commands of request j: reqCmds[reqCmdStart[j] .. reqCmdStart[j + 1]) (trace indices). */
  reqCmdStart: Uint32Array;
  reqCmds: Float64Array;
  // Bank-state spans clipped to the tile, by (bank, start).
  spanS: Float32Array;
  spanE: Float32Array;
  spanLane: Uint16Array;
  spanKind: Uint8Array;
  spanCause: Float64Array;
  spanTrueS: Float64Array;
  spanTrueE: Float64Array;
  bus: IntervalPieces | null;
  bg: IntervalPieces | null;
  faw: IntervalPieces | null;
}

/** What every view needs about a tile without its detail: counts, extents and zoomed-out blocks. */
export interface TileSummary {
  k: number;
  span: number;
  cmdFirst: number;
  nCmd: number;
  reads: number;
  writes: number;
  others: number;
  /** Earliest request arrival and latest end of anything drawn in the tile (absolute). */
  minStart: number;
  maxEnd: number;
  nReq: number;
  nBurst: number;
  nFan: number;
  // Per sub-bin: commands by class, command-bus cycles, data-bus cycles (all buses).
  subReads: Uint16Array;
  subWrites: Uint16Array;
  subOthers: Uint16Array;
  subCmdCycles: Float32Array;
  subDataCycles: Float32Array;
  // Per (lane, sub-bin): command instances, most frequent command, data-bus cycles.
  laneCnt: Uint16Array;
  laneTop: Uint8Array;
  laneBusy: Uint16Array;
  /** Per (lane, sub-bin): requests whose bar overlaps the sub-bin (requests completed in this record). */
  laneReqs: Uint16Array;
  /** Per (bank, sub-bin, kind): share of time in span kind 0..5, scaled to 255. */
  bankKinds: Uint8Array | null;
  /** Per (bank, category): bank-ticks in BANK_TIME categories. */
  bankTime: Float32Array | null;
  /** Per domain: ticks covered by bus-busy / tFAW intervals. */
  busCov: Float32Array | null;
  fawCov: Float32Array | null;
  /** Per (domain, sub-bin): the same, for zoomed-out summary lanes. */
  busCovSub: Float32Array | null;
  fawCovSub: Float32Array | null;
}

/** The span or interval that began in tile fromK and was cut at the end of every record before tile toK ended at `end`. */
export interface CrossEnd {
  fromK: number;
  toK: number;
  /** 0: bank span (index = bank); 1 bus, 2 bg, 3 faw (index = domain). */
  set: number;
  index: number;
  end: number;
}
