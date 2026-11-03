// Messages between the page and the engine worker.
import type { TraceSpec } from '../composables/useTrace';
import type { CrossEnd, TileDetail, TileSummary } from './types';

/** A trace to open; a server trace may name where the server keeps its index (see indexFile.ts). */
export type EngineSource = { kind: 'blob'; blob: Blob } | { kind: 'url'; url: string; index?: string } | { kind: 'live' };

export type EngineRequest =
  | { type: 'open'; source: EngineSource; spec: TraceSpec; readLatency: number; dramType: string }
  /** Detail of these records (by first tile index). */
  | { type: 'detail'; keys: number[] }
  /** A batch of live events, as LiveTraceStreamer encodes them (JSON text). */
  | { type: 'live'; columns: string }
  | { type: 'live-end' }
  | { type: 'close' };

export type EngineResponse =
  | { type: 'summaries'; gen: number; tiles: TileSummary[]; crossEnds: CrossEnd[]; maxRows: Uint16Array; entries: number; firstClk: number; lastClk: number }
  | { type: 'detail'; gen: number; tiles: TileDetail[] }
  /** Live: the record still being filled, as of the last event. */
  | { type: 'tail'; gen: number; summary: TileSummary; detail: TileDetail }
  | { type: 'progress'; gen: number; entries: number; total: number }
  /** A file's first and last command clocks, known before the file is read. */
  | { type: 'extent'; gen: number; firstClk: number; lastClk: number }
  | { type: 'done'; gen: number; entries: number; lastClk: number }
  | { type: 'error'; gen: number; message: string }
  /** Live: the view stopped taking events, out of memory (no file system to spill to) or storage. */
  | { type: 'live-full'; gen: number; reason: 'memory' | 'storage'; entries: number }
  /** Answer to close: the worker released its storage and can be ended. */
  | { type: 'closed'; gen: number };
