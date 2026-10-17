// Derived per-trace arrays shared by every panel: lane indices, data-bus bursts, request groups,
// throughput bins, prefix sums and (optionally) the bank-state reconstruction.
// Pure (no Vue), so it runs on the main thread and in workers/streaming.worker.ts.
import type { TraceArrays, TraceSpec } from '~/composables/useTrace';
import { isWriteCommand } from '~/utils/commandColors';
import { bankBuildState, bankTransferables, type BankStateData } from '~/utils/bankState';

/** Bar length (cycles) of a command without a commandCycles entry. */
export const CMD_VIEW_DURATION = 1;

export interface DataBusArrays {
  relClk: Float32Array;
  laneIdx: Uint16Array;
  cmdId: Uint8Array;
  originalIndices: Uint32Array;
  count: number;
}

export interface RequestArrays {
  /** Relative clock of request arrival (bar start = arrive) */
  relClk: Float32Array;
  /** Total span: depart - arrive (bar width) */
  duration: Float32Array;
  /** Queue delay: minClk - arrive (left segment length) */
  queueDelay: Float32Array;
  typeId: Uint8Array;
  /** Structural flat lane index (bank path) */
  laneIdx: Uint16Array;
  /** Global swimlane slot (stacked sub-row within lane) */
  slotId: Uint16Array;
  maxStackPerLane: Uint16Array;
  slotBase: Uint32Array;
  reqSlotCount: number;
  representativeIdx: Uint32Array;
  /** Request of each trace entry, -1 for commands without one (refresh, controller-issued PRE…). */
  cmdReq: Int32Array;
  /** Commands of request j: cmds[cmdStart[j] .. cmdStart[j + 1]), in trace order. */
  cmdStart: Uint32Array;
  cmds: Uint32Array;
  maxDuration: number;
  count: number;
}

export interface TraceDerived {
  referenceTime: number;
  N: number;
  /** Command ids of the N entries (the trace's own column). */
  cmdId: Uint8Array;
  /** Lane tiling shared by commands, data bursts and requests (hierarchy down to Row). */
  strides: number[];
  totalLanes: number;
  cmdRelClk: Float32Array;
  cmdLaneIdx: Uint16Array;
  nBL: number;
  dataBus: DataBusArrays;
  /** For trace row i, its data-bus instance, or -1 (non-accessing). */
  dataIdxByOrigIdx: Int32Array;
  /** Prefix sums for O(1) window statistics (length N+1). */
  prefixRead: Uint32Array;
  prefixWrite: Uint32Array;
  tpBinSize: number;
  tpBinCount: number;
  /** Data-bus utilisation per bin (0-1): accessing commands * nBL / binSize. */
  tpDataUtil: Float32Array;
  /** Command-bus utilisation per bin (0-1): commands / binSize. */
  tpCmdUtil: Float32Array;
  tpDataMax: number;
  tpCmdMax: number;
  /** Accessing commands per (bin, lane), at [bin * totalLanes + lane]: per-node data-bus share. */
  tpLaneAccess: Uint16Array;
  reqData: RequestArrays;
  bankState: BankStateData | null;
}

export interface TraceDerivedInput {
  spec: TraceSpec;
  arrays: TraceArrays;
  N: number;
  readLatency: number;
  dramType: string;
  wantBankState: boolean;
}

export function specTiming(spec: TraceSpec, name: string, fallback: number): number {
  const idx = spec.timingNames.indexOf(name);
  return idx >= 0 ? spec.timingValues[idx]! : fallback;
}

export function commandDuration(spec: TraceSpec | null | undefined, cmdId: number): number {
  return spec?.commandCycles?.[cmdId] ?? CMD_VIEW_DURATION;
}

export function maxCommandDuration(spec: TraceSpec | null | undefined): number {
  if (!spec || spec.commandCycles.length === 0) return CMD_VIEW_DURATION;
  let maxDuration = CMD_VIEW_DURATION;
  for (let i = 0; i < spec.commandCycles.length; i++)
    maxDuration = Math.max(maxDuration, spec.commandCycles[i] ?? CMD_VIEW_DURATION);
  return maxDuration;
}

export function bisectLeft(arr: Float32Array, target: number, count: number): number {
  let lo = 0;
  let hi = count;
  while (lo < hi) {
    const mid = (lo + hi) >>> 1;
    if (arr[mid]! < target) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}

export function bisectRight(arr: Float32Array, target: number, count: number): number {
  let lo = 0;
  let hi = count;
  while (lo < hi) {
    const mid = (lo + hi) >>> 1;
    if (arr[mid]! <= target) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}

function structuralDepthOf(spec: TraceSpec): number {
  const rowIdx = spec.levelNames.indexOf('Row');
  return rowIdx >= 0 ? rowIdx : Math.max(spec.levelNames.length - 2, 1);
}

function laneFrom(spec: TraceSpec): number {
  return spec.levelNames[0] === 'Channel' ? 1 : 0;
}

function computeStrides(levelSizes: Uint32Array, depth: number): { strides: number[]; totalLanes: number } {
  if (depth === 0) return { strides: [], totalLanes: 1 };
  const strides = new Array<number>(depth);
  strides[depth - 1] = 1;
  for (let k = depth - 2; k >= 0; k--)
    strides[k] = strides[k + 1]! * levelSizes[k + 1]!;
  return { strides, totalLanes: levelSizes[0]! * strides[0]! };
}

/**
 * Flat lane of entry i over levels [from, depth). Level 0 is skipped when it is Channel: it holds the
 * global channel id while the trace covers one channel (levelSizes[0] = 1).
 */
function flatLane(addr: Int32Array[], strides: number[], from: number, depth: number, i: number): number {
  let flat = 0;
  for (let k = from; k < depth; k++) {
    const a = addr[k]![i]!;
    if (a >= 0) flat += a * strides[k]!;
  }
  return flat;
}

function buildDataBusArrays(
  arrays: TraceArrays,
  spec: TraceSpec,
  strides: number[],
  structDepth: number,
  N: number,
  referenceTime: number,
): DataBusArrays {
  const nCL = specTiming(spec, 'nCL', 16);
  const nCWL = specTiming(spec, 'nCWL', 12);

  const indices: number[] = [];
  for (let i = 0; i < N; i++) {
    if (spec.commandMeta[arrays.cmdId[i]!]?.isAccessing) indices.push(i);
  }

  const M = indices.length;
  const relClk = new Float32Array(M);
  const laneIdx = new Uint16Array(M);
  const cmdId = new Uint8Array(M);
  const originalIndices = new Uint32Array(M);

  for (let j = 0; j < M; j++) {
    const i = indices[j]!;
    originalIndices[j] = i;
    cmdId[j] = arrays.cmdId[i]!;
    const offset = isWriteCommand(spec.commandNames[cmdId[j]!]!) ? nCWL : nCL;
    relClk[j] = Number(arrays.clk[i]) - referenceTime + offset;
    laneIdx[j] = flatLane(arrays.addr, strides, laneFrom(spec), structDepth, i);
  }

  return { relClk, laneIdx, cmdId, originalIndices, count: M };
}

/** Greedy stack assignment for overlapping [start, end) intervals per lane. */
function assignRequestStacks(
  R: number,
  relClk: Float32Array,
  duration: Float32Array,
  laneIdx: Uint16Array,
  totalLanes: number,
): { slotId: Uint16Array; maxStackPerLane: Uint16Array; slotBase: Uint32Array; reqSlotCount: number } {
  const maxStackPerLane = new Uint16Array(totalLanes);
  maxStackPerLane.fill(1);

  if (R === 0 || totalLanes === 0) {
    const slotBase = new Uint32Array(totalLanes);
    let sum = 0;
    for (let l = 0; l < totalLanes; l++) {
      slotBase[l] = sum;
      sum += 1;
    }
    return { slotId: new Uint16Array(0), maxStackPerLane, slotBase, reqSlotCount: Math.max(sum, 1) };
  }

  const byLane = new Map<number, number[]>();
  for (let j = 0; j < R; j++) {
    const L = laneIdx[j]!;
    let arr = byLane.get(L);
    if (!arr) {
      arr = [];
      byLane.set(L, arr);
    }
    arr.push(j);
  }

  const stackIdx = new Uint16Array(R);

  for (const [L, indices] of byLane) {
    indices.sort((a, b) => {
      const sa = relClk[a]!;
      const sb = relClk[b]!;
      if (sa !== sb) return sa - sb;
      return (sb + duration[b]!) - (sa + duration[a]!);
    });
    const ends: number[] = [];
    for (const j of indices) {
      const start = relClk[j]!;
      const end = start + duration[j]!;
      let placed = -1;
      for (let s = 0; s < ends.length; s++) {
        if (ends[s]! <= start) {
          placed = s;
          break;
        }
      }
      if (placed < 0) {
        placed = ends.length;
        ends.push(end);
      }
      else {
        ends[placed] = end;
      }
      stackIdx[j] = placed;
      if (placed + 1 > maxStackPerLane[L]!) maxStackPerLane[L] = placed + 1;
    }
  }

  const slotBase = new Uint32Array(totalLanes);
  let sum = 0;
  for (let l = 0; l < totalLanes; l++) {
    slotBase[l] = sum;
    sum += maxStackPerLane[l]!;
  }

  const slotId = new Uint16Array(R);
  for (let j = 0; j < R; j++) slotId[j] = slotBase[laneIdx[j]!]! + stackIdx[j]!;

  return { slotId, maxStackPerLane, slotBase, reqSlotCount: Math.max(sum, 1) };
}

/** One request bar per (arrive, structural lane): arrival → last command (+ read latency). */
function buildRequestArrays(
  arrays: TraceArrays,
  strides: number[],
  from: number,
  structDepth: number,
  N: number,
  referenceTime: number,
  readLatency: number,
  totalLanes: number,
): RequestArrays {
  // Number key: arrive * totalLanes + flat.  Collision-free because flat ∈ [0, totalLanes),
  // so the encoding is injective.  Safe in float64 for arrive < 2^37 (≈137B cycles)
  // and totalLanes < 2^16, which covers all practical DRAM simulations.
  const groups = new Map<number, {
    arrive: number; minClk: number; maxClk: number;
    typeId: number; laneIdx: number; repIdx: number; index: number;
  }>();
  const cmdKey = new Float64Array(N).fill(-1);

  for (let i = 0; i < N; i++) {
    const arriveBI = arrays.arrive[i]!;
    if (arriveBI < 0n) continue;
    const arrive = Number(arriveBI);
    const clk = Number(arrays.clk[i]!);
    const flat = flatLane(arrays.addr, strides, from, structDepth, i);

    const key = arrive * totalLanes + flat;
    cmdKey[i] = key;
    const existing = groups.get(key);
    if (existing) {
      if (clk > existing.maxClk) existing.maxClk = clk;
      if (clk < existing.minClk) existing.minClk = clk;
    } else {
      groups.set(key, {
        arrive, minClk: clk, maxClk: clk,
        typeId: Math.max(0, arrays.typeId[i]!),
        laneIdx: flat, repIdx: i, index: 0,
      });
    }
  }

  const entries = Array.from(groups.values());
  entries.sort((a, b) => {
    if (a.arrive !== b.arrive) return a.arrive - b.arrive;
    if (a.laneIdx !== b.laneIdx) return a.laneIdx - b.laneIdx;
    return a.minClk - b.minClk;
  });

  const R = entries.length;
  const relClk = new Float32Array(R);
  const duration = new Float32Array(R);
  const queueDelay = new Float32Array(R);
  const typeId = new Uint8Array(R);
  const laneIdx = new Uint16Array(R);
  const representativeIdx = new Uint32Array(R);
  let maxDuration = 0;

  for (let j = 0; j < R; j++) {
    const e = entries[j]!;
    const depart = e.maxClk + (e.typeId === 0 ? readLatency : 1);
    const total = Math.max(depart - e.arrive, 1e-6);
    relClk[j] = e.arrive - referenceTime;
    duration[j] = total;
    queueDelay[j] = e.minClk - e.arrive;
    if (total > maxDuration) maxDuration = total;
    typeId[j] = e.typeId;
    laneIdx[j] = e.laneIdx;
    representativeIdx[j] = e.repIdx;
    e.index = j;
  }

  // Command ↔ request links (CSR, commands in trace order).
  const cmdReq = new Int32Array(N).fill(-1);
  const cmdStart = new Uint32Array(R + 1);
  for (let i = 0; i < N; i++) {
    if (cmdKey[i]! < 0) continue;
    const j = groups.get(cmdKey[i]!)!.index;
    cmdReq[i] = j;
    cmdStart[j + 1]!++;
  }
  for (let j = 0; j < R; j++) cmdStart[j + 1]! += cmdStart[j]!;
  const cmds = new Uint32Array(cmdStart[R]!);
  const fill = cmdStart.slice(0, R);
  for (let i = 0; i < N; i++) if (cmdReq[i]! >= 0) cmds[fill[cmdReq[i]!]!++] = i;

  const stacks = assignRequestStacks(R, relClk, duration, laneIdx, totalLanes);
  return { relClk, duration, queueDelay, typeId, laneIdx, ...stacks, representativeIdx, cmdReq, cmdStart, cmds, maxDuration, count: R };
}

/**
 * Build every derived array for the first N entries. `progress` (main thread only) is awaited
 * between stages so a status message can paint.
 */
export async function buildTraceDerived(
  input: TraceDerivedInput,
  progress?: (msg: string) => Promise<void> | void,
): Promise<TraceDerived> {
  const { spec, arrays, N, readLatency } = input;
  const structDepth = structuralDepthOf(spec);
  const { strides, totalLanes } = computeStrides(spec.levelSizes, structDepth);
  const referenceTime = N > 0 ? Number(arrays.clk[0]) : 0;

  await progress?.('Computing command clocks and lanes…');
  const cmdRelClk = new Float32Array(N);
  const cmdLaneIdx = new Uint16Array(N);
  for (let i = 0; i < N; i++) {
    cmdRelClk[i] = Number(arrays.clk[i]) - referenceTime;
    cmdLaneIdx[i] = flatLane(arrays.addr, strides, laneFrom(spec), structDepth, i);
  }

  await progress?.('Building data bus…');
  const nBL = specTiming(spec, 'nBL', 4);
  const dataBus = buildDataBusArrays(arrays, spec, strides, structDepth, N, referenceTime);
  const dataIdxByOrigIdx = new Int32Array(N).fill(-1);
  for (let j = 0; j < dataBus.count; j++) dataIdxByOrigIdx[dataBus.originalIndices[j]!] = j;

  const prefixRead = new Uint32Array(N + 1);
  const prefixWrite = new Uint32Array(N + 1);
  for (let i = 0; i < N; i++) {
    prefixRead[i + 1] = prefixRead[i]!;
    prefixWrite[i + 1] = prefixWrite[i]!;
    const cid = arrays.cmdId[i]!;
    if (spec.commandMeta[cid]?.isAccessing) {
      if (isWriteCommand(spec.commandNames[cid]!)) prefixWrite[i + 1]!++;
      else prefixRead[i + 1]!++;
    }
  }

  // Throughput bins: nBL * 32 cycles each.
  const tpBinSize = nBL * 32;
  const tpBinCount = N > 0 ? Math.ceil((cmdRelClk[N - 1]! + 1) / tpBinSize) : 0;
  const tpDataUtil = new Float32Array(tpBinCount);
  const tpCmdUtil = new Float32Array(tpBinCount);
  let tpDataMax = 0;
  let tpCmdMax = 0;
  const tpLaneAccess = new Uint16Array(tpBinCount * totalLanes);
  if (tpBinCount > 0) {
    const accessBin = new Uint32Array(tpBinCount);
    const totalBin = new Uint32Array(tpBinCount);
    for (let i = 0; i < N; i++) {
      const b = Math.min(tpBinCount - 1, Math.floor(cmdRelClk[i]! / tpBinSize));
      totalBin[b]!++;
      if (spec.commandMeta[arrays.cmdId[i]!]?.isAccessing) {
        accessBin[b]!++;
        const k = b * totalLanes + cmdLaneIdx[i]!;
        if (tpLaneAccess[k]! < 0xffff) tpLaneAccess[k]!++;
      }
    }
    for (let b = 0; b < tpBinCount; b++) {
      const d = Math.min(1, (accessBin[b]! * nBL) / tpBinSize);
      const c = Math.min(1, totalBin[b]! / tpBinSize);
      tpDataUtil[b] = d;
      tpCmdUtil[b] = c;
      if (d > tpDataMax) tpDataMax = d;
      if (c > tpCmdMax) tpCmdMax = c;
    }
  }

  await progress?.('Grouping requests…');
  const reqData = buildRequestArrays(arrays, strides, laneFrom(spec), structDepth, N, referenceTime, readLatency, totalLanes);

  let bankState: BankStateData | null = null;
  if (input.wantBankState) {
    await progress?.('Reconstructing bank states…');
    bankState = bankBuildState(spec, arrays, N, input.dramType);
  }

  return {
    referenceTime,
    N,
    cmdId: arrays.cmdId.subarray(0, N),
    strides,
    totalLanes,
    cmdRelClk,
    cmdLaneIdx,
    nBL,
    dataBus,
    dataIdxByOrigIdx,
    prefixRead,
    prefixWrite,
    tpBinSize,
    tpBinCount,
    tpDataUtil,
    tpCmdUtil,
    tpDataMax,
    tpCmdMax,
    tpLaneAccess,
    reqData,
    bankState,
  };
}

/** Buffers the worker can transfer (everything it allocated; not the trace's own cmdId). */
export function traceDerivedTransferables(d: TraceDerived): ArrayBuffer[] {
  const views: ArrayBufferView[] = [
    d.cmdRelClk, d.cmdLaneIdx,
    d.dataBus.relClk, d.dataBus.laneIdx, d.dataBus.cmdId, d.dataBus.originalIndices,
    d.dataIdxByOrigIdx, d.prefixRead, d.prefixWrite, d.tpDataUtil, d.tpCmdUtil, d.tpLaneAccess,
    d.reqData.relClk, d.reqData.duration, d.reqData.queueDelay, d.reqData.typeId, d.reqData.laneIdx,
    d.reqData.slotId, d.reqData.maxStackPerLane, d.reqData.slotBase, d.reqData.representativeIdx,
    d.reqData.cmdReq, d.reqData.cmdStart, d.reqData.cmds,
  ];
  return [...(d.bankState ? bankTransferables(d.bankState) : []), ...views.map(v => v.buffer as ArrayBuffer)];
}
