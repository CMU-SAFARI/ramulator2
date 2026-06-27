import type { TraceArrays, TraceSpec } from '~/composables/useTrace';
import { isWriteCommand } from '~/utils/commandColors';

function getStructuralDepth(spec: TraceSpec): number {
  const rowIdx = spec.levelNames.indexOf('Row');
  return rowIdx >= 0 ? rowIdx : Math.max(spec.levelNames.length - 2, 1);
}

function getTimingValue(spec: TraceSpec, name: string, fallback: number): number {
  const idx = spec.timingNames.indexOf(name);
  return idx >= 0 ? spec.timingValues[idx]! : fallback;
}

function computeStrides(
  levelSizes: Uint32Array,
  depth: number,
): { strides: number[]; totalLanes: number } {
  if (depth === 0) return { strides: [], totalLanes: 1 };
  const strides = new Array<number>(depth);
  strides[depth - 1] = 1;
  for (let k = depth - 2; k >= 0; k--)
    strides[k] = strides[k + 1]! * levelSizes[k + 1]!;
  return { strides, totalLanes: levelSizes[0]! * strides[0]! };
}

function buildRelativeClocks(
  clk: BigInt64Array,
  N: number,
  refTime: number,
): Float32Array {
  const out = new Float32Array(N);
  for (let i = 0; i < N; i++) out[i] = Number(clk[i]) - refTime;
  return out;
}

function buildLaneIndices(
  addr: Int32Array[],
  strides: number[],
  depth: number,
  N: number,
): Uint16Array {
  const out = new Uint16Array(N);
  for (let i = 0; i < N; i++) {
    let flat = 0;
    for (let k = 0; k < depth; k++) {
      const a = addr[k]![i]!;
      if (a >= 0) flat += a * strides[k]!;
    }
    out[i] = flat;
  }
  return out;
}

interface DataBusArrays {
  relClk: Float32Array;
  laneIdx: Uint16Array;
  cmdId: Uint8Array;
  originalIndices: Uint32Array;
  count: number;
}

function buildDataBusArrays(
  arrays: TraceArrays,
  spec: TraceSpec,
  strides: number[],
  structDepth: number,
  N: number,
  referenceTime: number,
): DataBusArrays {
  const nCL = getTimingValue(spec, 'nCL', 16);
  const nCWL = getTimingValue(spec, 'nCWL', 12);

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

    let flat = 0;
    for (let k = 0; k < structDepth; k++) {
      const a = arrays.addr[k]![i]!;
      if (a >= 0) flat += a * strides[k]!;
    }
    laneIdx[j] = flat;
  }

  return { relClk, laneIdx, cmdId, originalIndices, count: M };
}

interface RequestArrays {
  relClk: Float32Array;
  duration: Float32Array;
  queueDelay: Float32Array;
  typeId: Uint8Array;
  laneIdx: Uint16Array;
  slotId: Uint16Array;
  maxStackPerLane: Uint16Array;
  slotBase: Uint32Array;
  reqSlotCount: number;
  representativeIdx: Uint32Array;
  maxDuration: number;
  count: number;
}

function assignRequestStacks(
  R: number,
  relClk: Float32Array,
  duration: Float32Array,
  laneIdx: Uint16Array,
  totalLanes: number,
): {
  slotId: Uint16Array;
  maxStackPerLane: Uint16Array;
  slotBase: Uint32Array;
  reqSlotCount: number;
} {
  const maxStackPerLane = new Uint16Array(totalLanes);
  maxStackPerLane.fill(1);

  if (R === 0 || totalLanes === 0) {
    const slotBase = new Uint32Array(totalLanes);
    let sum = 0;
    for (let l = 0; l < totalLanes; l++) {
      slotBase[l] = sum;
      sum += 1;
    }
    return {
      slotId: new Uint16Array(0),
      maxStackPerLane,
      slotBase,
      reqSlotCount: Math.max(sum, 1),
    };
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
      const ea = sa + duration[a]!;
      const eb = sb + duration[b]!;
      return eb - ea;
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
      const need = placed + 1;
      if (need > maxStackPerLane[L]!) maxStackPerLane[L] = need;
    }
  }

  const slotBase = new Uint32Array(totalLanes);
  let sum = 0;
  for (let l = 0; l < totalLanes; l++) {
    slotBase[l] = sum;
    sum += maxStackPerLane[l]!;
  }

  const slotId = new Uint16Array(R);
  for (let j = 0; j < R; j++) {
    const L = laneIdx[j]!;
    slotId[j] = slotBase[L]! + stackIdx[j]!;
  }

  return { slotId, maxStackPerLane, slotBase, reqSlotCount: Math.max(sum, 1) };
}

function buildRequestArrays(
  arrays: TraceArrays,
  strides: number[],
  structDepth: number,
  N: number,
  referenceTime: number,
  readLatency: number,
  totalLanes: number,
): RequestArrays {
  const groups = new Map<number, {
    arrive: number; minClk: number; maxClk: number;
    typeId: number; laneIdx: number; repIdx: number;
  }>();

  for (let i = 0; i < N; i++) {
    const arriveBI = arrays.arrive[i]!;
    if (arriveBI < 0n) continue;
    const arrive = Number(arriveBI);
    const clk = Number(arrays.clk[i]!);

    let flat = 0;
    for (let k = 0; k < structDepth; k++) {
      const a = arrays.addr[k]![i]!;
      if (a >= 0) flat += a * strides[k]!;
    }

    const key = arrive * totalLanes + flat;
    const existing = groups.get(key);
    if (existing) {
      if (clk > existing.maxClk) existing.maxClk = clk;
      if (clk < existing.minClk) existing.minClk = clk;
    } else {
      groups.set(key, {
        arrive, minClk: clk, maxClk: clk,
        typeId: Math.max(0, arrays.typeId[i]!),
        laneIdx: flat, repIdx: i,
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
  }

  const { slotId, maxStackPerLane, slotBase, reqSlotCount } = assignRequestStacks(
    R, relClk, duration, laneIdx, totalLanes,
  );

  return {
    relClk,
    duration,
    queueDelay,
    typeId,
    laneIdx,
    slotId,
    maxStackPerLane,
    slotBase,
    reqSlotCount,
    representativeIdx,
    maxDuration,
    count: R,
  };
}

self.onmessage = (event) => {
  const {
    arrays,
    spec,
    N,
    referenceTime,
    readLatency,
    cmdStrides,
    dataStrides,
    structDepth,
    dataTotalLanes,
  } = event.data;

  const cmdRelClk = buildRelativeClocks(arrays.clk, N, referenceTime);
  const cmdLaneIdx = buildLaneIndices(arrays.addr, cmdStrides, structDepth, N);

  const dataNBL = getTimingValue(spec, 'nBL', 4);
  const dataBus = buildDataBusArrays(arrays, spec, dataStrides, structDepth, N, referenceTime);

  const dataIdxByOrigIdx = new Int32Array(N);
  dataIdxByOrigIdx.fill(-1);
  for (let j = 0; j < dataBus.count; j++) {
    const o = dataBus.originalIndices[j]!;
    dataIdxByOrigIdx[o] = j;
  }

  const prefixRead = new Uint32Array(N + 1);
  const prefixWrite = new Uint32Array(N + 1);
  for (let i = 0; i < N; i++) {
    prefixRead[i + 1] = prefixRead[i]!;
    prefixWrite[i + 1] = prefixWrite[i]!;
    const cid = arrays.cmdId[i]!;
    const meta = spec.commandMeta[cid];
    if (meta?.isAccessing) {
      if (isWriteCommand(spec.commandNames[cid]!)) prefixWrite[i + 1]!++;
      else prefixRead[i + 1]!++;
    }
  }

  const tpBinSize = dataNBL * 32;
  const tpSpan = cmdRelClk[N - 1]! + 1;
  const tpBinCount = Math.ceil(tpSpan / tpBinSize);
  const accessBin = new Uint32Array(tpBinCount);
  const totalBin = new Uint32Array(tpBinCount);
  for (let i = 0; i < N; i++) {
    const b = Math.min(tpBinCount - 1, Math.floor(cmdRelClk[i]! / tpBinSize));
    totalBin[b]!++;
    const cid = arrays.cmdId[i]!;
    if (spec.commandMeta[cid]?.isAccessing) accessBin[b]!++;
  }
  const tpDataUtil = new Float32Array(tpBinCount);
  const tpCmdUtil = new Float32Array(tpBinCount);
  let tpDataMax = 0;
  let tpCmdMax = 0;
  for (let b = 0; b < tpBinCount; b++) {
    const d = Math.min(1, (accessBin[b]! * dataNBL) / tpBinSize);
    const c = Math.min(1, totalBin[b]! / tpBinSize);
    tpDataUtil[b] = d;
    tpCmdUtil[b] = c;
    if (d > tpDataMax) tpDataMax = d;
    if (c > tpCmdMax) tpCmdMax = c;
  }

  const reqData = buildRequestArrays(
    arrays, dataStrides, structDepth, N, referenceTime, readLatency, dataTotalLanes,
  );

  const transferables = [
    cmdRelClk.buffer,
    cmdLaneIdx.buffer,
    dataBus.relClk.buffer,
    dataBus.laneIdx.buffer,
    dataBus.cmdId.buffer,
    dataBus.originalIndices.buffer,
    dataIdxByOrigIdx.buffer,
    prefixRead.buffer,
    prefixWrite.buffer,
    tpDataUtil.buffer,
    tpCmdUtil.buffer,
    reqData.relClk.buffer,
    reqData.duration.buffer,
    reqData.queueDelay.buffer,
    reqData.typeId.buffer,
    reqData.laneIdx.buffer,
    reqData.slotId.buffer,
    reqData.maxStackPerLane.buffer,
    reqData.slotBase.buffer,
    reqData.representativeIdx.buffer,
  ];

  self.postMessage({
    cmdRelClk,
    cmdLaneIdx,
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
    reqData,
  }, transferables);
};
