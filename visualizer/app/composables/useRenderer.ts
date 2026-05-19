import createREGL from 'regl';
import type { TraceHeader, TraceSpec, TraceArrays } from '~/composables/useTrace';
import type { RowLayout, BusType } from '~/stores/ui';
import {
  pathFromAddrForTree,
  nodeId,
  rankLevelIndex,
  structuralDepth as specStructuralDepth,
  bankGroupLevelIndex,
} from '~/utils/traceTree';
import { CMD_PALETTE, defaultCmdColor, hex2rgb, isWriteCommand } from '~/utils/commandColors';
import type { RequestTypeStoredColors } from '~/utils/commandColorStorage';

import StreamingWorker from '~/workers/streaming.worker.ts?worker';

// ── Constants ───────────────────────────────────────────────────────

const ROW_HEIGHT = 16;
/** Extra pixels between vertically stacked request rows (canvas + sidebar must agree). */
const STACK_ROW_GAP = 4;
/** Tight time zoom when jumping request → command (cycles visible in viewport). */
const NAV_ZOOM_CMD_CYCLES = 72;
/** Fallback command-bus bar extent for older traces without per-command cycles. */
const CMD_VIEW_DURATION = 1;
/** When horizontal scale is at least this many pixels per cycle, draw command names on the command bus. */
const CMD_LABEL_MIN_PX_PER_CYCLE = 28;
/** Minimum bar width in pixels before drawing a label on the data bus. */
const LABEL_MIN_BAR_WIDTH_PX = 28;
/** Avoid drawing too many text calls in one frame when the window is very dense. */
const CMD_LABEL_MAX_VISIBLE = 600;
/** Minimum time window when jumping command/data → request (floor; actual zoom also scales with request length). */
const NAV_ZOOM_REQ_CYCLES = 400;
/** Extra cycles beyond ~2.5× (queue+active) when framing a request after navigation. */
const NAV_ZOOM_REQ_PAD = 160;
const BG_COLOR: [number, number, number, number] = [24 / 255, 24 / 255, 27 / 255, 1];

/** Vertical gridline color for major ticks (subtle, semi-transparent). */
const VGRID_COLOR = 'rgba(255, 255, 255, 0.06)';
/** Fill color for the selected range highlight on the main canvas. */
const RANGE_FILL = 'rgba(255, 255, 255, 0.045)';
/** Edge color for the selected range boundary lines. */
const RANGE_EDGE = 'rgba(255, 255, 255, 0.18)';
/** Cursor marker color, shared by the main overlay and minimap. */
const CURSOR_COLOR = 'rgba(251, 191, 36, 0.95)';
const CURSOR_FILL = 'rgba(251, 191, 36, 0.12)';
/** Keep throughput point count bounded at wide zoom levels to avoid frame drops. */
const TP_MAX_DRAW_POINTS = 480;
/** Marker radius for throughput data points. */
const TP_POINT_RADIUS = 2.3;
/** Hover snap radius around throughput markers. */
const TP_HOVER_RADIUS = 8;
/** Minimum horizontal spacing between visible throughput markers (px). */
const TP_MARKER_MIN_GAP_PX = 9;

/** Pick a "nice" tick step for a given time duration and pixel width (matches Timeline.vue). */
function niceStep(duration: number, width: number): number {
  const target = duration * (90 / width);
  const pow = Math.floor(Math.log10(target));
  const base = Math.pow(10, pow);
  for (const m of [1, 2, 5, 10]) {
    if (base * m >= target) return base * m;
  }
  return base * 10;
}

// ── Shaders ─────────────────────────────────────────────────────────

const TRACE_VERT = `
precision highp float;

attribute vec2 a_quad;
attribute float a_relClk;
attribute float a_cmdId;
attribute float a_laneIdx;

uniform sampler2D u_cmdLookup;
uniform sampler2D u_lanes;
uniform vec2 u_viewRange;
uniform float u_viewBase;
uniform vec2 u_resolution;
uniform float u_rowHeight;
uniform float u_laneCount;

varying vec3 v_color;

void main() {
  vec2 cmdUv = vec2((a_cmdId + 0.5) / 256.0, 0.5);
  vec4 cmdProps = texture2D(u_cmdLookup, cmdUv);
  v_color = cmdProps.rgb;
  float duration = cmdProps.a;

  float viewWidth = u_viewRange.y - u_viewRange.x;
  float localRel = a_relClk - u_viewBase;
  float worldX = localRel + a_quad.x * duration;
  float ndcX = ((worldX - u_viewRange.x) / viewWidth) * 2.0 - 1.0;

  float laneUv = (a_laneIdx + 0.5) / u_laneCount;
  float yCenter = texture2D(u_lanes, vec2(laneUv, 0.5)).r;
  float yScreen = yCenter + (a_quad.y - 0.5) * u_rowHeight;
  float ndcY = 1.0 - (yScreen / u_resolution.y) * 2.0;

  gl_Position = vec4(ndcX, ndcY, 0.0, 1.0);
}
`;

const TRACE_FRAG = `
precision highp float;
varying vec3 v_color;
void main() {
  gl_FragColor = vec4(v_color, 1.0);
}
`;

/** Same geometry as TRACE_VERT; passes v_quad + v_duration for REQ_HOVER_FRAG. */
const TRACE_HOVER_VERT = `
precision highp float;

attribute vec2 a_quad;
attribute float a_relClk;
attribute float a_cmdId;
attribute float a_laneIdx;

uniform sampler2D u_cmdLookup;
uniform sampler2D u_lanes;
uniform vec2 u_viewRange;
uniform float u_viewBase;
uniform vec2 u_resolution;
uniform float u_rowHeight;
uniform float u_laneCount;

varying vec2 v_quad;
varying float v_duration;

void main() {
  vec2 cmdUv = vec2((a_cmdId + 0.5) / 256.0, 0.5);
  vec4 cmdProps = texture2D(u_cmdLookup, cmdUv);
  float duration = cmdProps.a;
  v_quad = a_quad;
  v_duration = duration;

  float viewWidth = u_viewRange.y - u_viewRange.x;
  float localRel = a_relClk - u_viewBase;
  float worldX = localRel + a_quad.x * duration;
  float ndcX = ((worldX - u_viewRange.x) / viewWidth) * 2.0 - 1.0;

  float laneUv = (a_laneIdx + 0.5) / u_laneCount;
  float yCenter = texture2D(u_lanes, vec2(laneUv, 0.5)).r;
  float yScreen = yCenter + (a_quad.y - 0.5) * u_rowHeight;
  float ndcY = 1.0 - (yScreen / u_resolution.y) * 2.0;

  gl_Position = vec4(ndcX, ndcY, 0.0, 1.0);
}
`;

const GRID_VERT = `
precision highp float;

attribute vec2 a_quad;
attribute float a_yScreen;

uniform float u_resolutionY;

void main() {
  float ndcX = a_quad.x * 2.0 - 1.0;
  float yPixel = a_yScreen + a_quad.y;
  float ndcY = 1.0 - (yPixel / u_resolutionY) * 2.0;
  gl_Position = vec4(ndcX, ndcY, 0.0, 1.0);
}
`;

const GRID_FRAG = `
precision highp float;
void main() {
  gl_FragColor = vec4(0.25, 0.25, 0.28, 1.0);
}
`;

const GLOW_VERT = `
precision highp float;

attribute vec2 a_quad;

uniform vec2 u_viewRange;
uniform float u_viewBase;
uniform vec2 u_resolution;
uniform float u_rowHeight;
uniform float u_targetStart;
uniform float u_targetDuration;
uniform float u_targetY;

varying vec2 v_uv;

void main() {
  float viewWidth = u_viewRange.y - u_viewRange.x;
  float pad = u_targetDuration * 0.5 + 8.0 * viewWidth / u_resolution.x;

  float localTarget = u_targetStart - u_viewBase;
  float worldLeft  = localTarget - pad;
  float worldRight = localTarget + u_targetDuration + pad;
  float worldX = mix(worldLeft, worldRight, a_quad.x);
  float ndcX = ((worldX - u_viewRange.x) / viewWidth) * 2.0 - 1.0;

  float yPad = u_rowHeight * 1.5;
  float yTop = u_targetY - yPad;
  float yBot = u_targetY + yPad;
  float yScreen = mix(yTop, yBot, a_quad.y);
  float ndcY = 1.0 - (yScreen / u_resolution.y) * 2.0;

  gl_Position = vec4(ndcX, ndcY, 0.0, 1.0);
  v_uv = a_quad;
}
`;

const GLOW_FRAG = `
precision highp float;

varying vec2 v_uv;
uniform float u_opacity;
uniform vec3 u_color;

void main() {
  float dx = abs(v_uv.x - 0.5) * 2.0;
  float dy = abs(v_uv.y - 0.5) * 2.0;
  float d = max(dx, dy);
  float glow = smoothstep(1.0, 0.3, d);
  gl_FragColor = vec4(u_color, glow * u_opacity);
}
`;

const REQ_VERT = `
precision highp float;

attribute vec2 a_quad;
attribute float a_relClk;
attribute float a_cmdId;
attribute float a_laneIdx;
attribute float a_duration;
attribute float a_queueDelay;

uniform sampler2D u_cmdLookup;
uniform sampler2D u_lanes;
uniform vec2 u_viewRange;
uniform float u_viewBase;
uniform vec2 u_resolution;
uniform float u_rowHeight;
uniform float u_laneCount;

varying vec3 v_color;
varying vec2 v_quad;
varying float v_duration;
varying float v_queueDelay;

void main() {
  vec2 cmdUv = vec2((a_cmdId + 0.5) / 256.0, 0.5);
  vec4 cmdProps = texture2D(u_cmdLookup, cmdUv);
  v_color = cmdProps.rgb;
  v_quad = a_quad;
  v_duration = a_duration;
  v_queueDelay = a_queueDelay;

  float viewWidth = u_viewRange.y - u_viewRange.x;
  float localRel = a_relClk - u_viewBase;
  float worldX = localRel + a_quad.x * a_duration;
  float ndcX = ((worldX - u_viewRange.x) / viewWidth) * 2.0 - 1.0;

  float laneUv = (a_laneIdx + 0.5) / u_laneCount;
  float yCenter = texture2D(u_lanes, vec2(laneUv, 0.5)).r;
  float yScreen = yCenter + (a_quad.y - 0.5) * u_rowHeight;
  float ndcY = 1.0 - (yScreen / u_resolution.y) * 2.0;

  gl_Position = vec4(ndcX, ndcY, 0.0, 1.0);
}
`;

const REQ_FRAG = `
precision highp float;

varying vec3 v_color;
varying vec2 v_quad;
varying float v_duration;
varying float v_queueDelay;

void main() {
  float qf = clamp(v_queueDelay / max(v_duration, 1e-6), 0.0, 1.0);
  float alpha = v_quad.x < qf ? 0.8 : 1.0;
  gl_FragColor = vec4(v_color, alpha);
}
`;

const REQ_HOVER_FRAG = `
precision highp float;

varying vec2 v_quad;
varying float v_duration;

uniform vec2 u_viewRange;
uniform vec2 u_resolution;
uniform float u_rowHeight;

void main() {
  float viewWidth = u_viewRange.y - u_viewRange.x;
  float barWpx = (v_duration / max(viewWidth, 1e-6)) * u_resolution.x;
  float edgeX = min(v_quad.x, 1.0 - v_quad.x) * barWpx;
  float edgeY = min(v_quad.y, 1.0 - v_quad.y) * u_rowHeight;
  float edgeDist = min(edgeX, edgeY);
  float border = 1.0 - step(1.5, edgeDist);
  float tint = 0.28 * step(1.5, edgeDist);
  float a = max(border, tint);
  gl_FragColor = vec4(1.0, 1.0, 1.0, a);
}
`;

// ── Types ───────────────────────────────────────────────────────────

export interface ViewState {
  start: number;
  duration: number;
  minDuration: number;
  maxDuration: number;
  minTime: number;
  maxTime: number;
}

export interface Stats {
  fps: number;
  eventCount: number;
  totalEvents: number;
}

export interface HitResult {
  index: number;
  cmdId: number;
  clk: bigint;
  typeId: number;
  sourceId: number;
  addr: number[];
  bus: BusType;
  duration: number;
  busClk?: number;
  queueDelay?: number;
  /** Index into request-view `reqData` (for GPU hover pass). */
  requestGroupIndex?: number;
}

export interface RangeSelection {
  startTime: number;
  endTime: number;
}

export interface RangeStats {
  durationCycles: number;
  totalCommands: number;
  readCommands: number;
  writeCommands: number;
  otherCommands: number;
  accessingCommands: number;
  /** Commands per cycle on the command bus. */
  cmdBusThroughput: number;
}

export interface ThroughputHover {
  series: 'data' | 'cmd';
  binIndex: number;
  value: number;
  binStartTime: number;
  binEndTime: number;
}

export interface ThroughputRangeStats {
  dataMeanUtil: number;
  cmdMeanUtil: number;
  binCount: number;
}

export type CursorJumpDirection = 'prev' | 'next';

// ── Pure helpers ────────────────────────────────────────────────────

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

function buildCmdTextureData(
  spec: TraceSpec,
  userColors: Record<number, string>,
  uniformDuration: number,
  commandDurations?: Uint8Array | null,
): { data: Float32Array; durations: Float32Array } {
  const MAX = 256;
  const data = new Float32Array(MAX * 4);
  const durations = new Float32Array(MAX);

  const [fr, fg, fb] = hex2rgb(CMD_PALETTE.fallback);
  for (let i = 0; i < MAX; i++) {
    data[i * 4 + 0] = fr;
    data[i * 4 + 1] = fg;
    data[i * 4 + 2] = fb;
    data[i * 4 + 3] = uniformDuration;
    durations[i] = uniformDuration;
  }

  for (let id = 0; id < spec.commandNames.length; id++) {
    const meta = spec.commandMeta[id]!;
    const duration = commandDurations?.[id] ?? uniformDuration;
    durations[id] = duration;
    data[id * 4 + 3] = duration;

    const hex = userColors[id] ?? defaultCmdColor(spec.commandNames[id]!, meta);
    const [r, g, b] = hex2rgb(hex);
    data[id * 4 + 0] = r;
    data[id * 4 + 1] = g;
    data[id * 4 + 2] = b;
  }

  return { data, durations };
}

function getCommandDuration(spec: TraceSpec | null | undefined, cmdId: number): number {
  return spec?.commandCycles?.[cmdId] ?? CMD_VIEW_DURATION;
}

function getMaxCommandDuration(spec: TraceSpec | null | undefined): number {
  if (!spec || spec.commandCycles.length === 0) return CMD_VIEW_DURATION;
  let maxDuration = CMD_VIEW_DURATION;
  for (let i = 0; i < spec.commandCycles.length; i++)
    maxDuration = Math.max(maxDuration, spec.commandCycles[i] ?? CMD_VIEW_DURATION);
  return maxDuration;
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
  maxDuration: number;
  count: number;
}

interface CpuPipelineCache {
  dataVersion: number;
  sessionGeneration: number;
  referenceTime: number;
  N: number;
  cmdRelClk: Float32Array;
  cmdLaneIdx: Uint16Array;
  cmdStrides: number[];
  cmdTotalLanes: number;
  dataBus: DataBusArrays;
  dataStrides: number[];
  dataTotalLanes: number;
  dataIdxByOrigIdx: Int32Array;
  dataNBL: number;
  rangeNBL: number;
  prefixRead: Uint32Array;
  prefixWrite: Uint32Array;
  tpBinSize: number;
  tpBinCount: number;
  tpDataUtil: Float32Array;
  tpCmdUtil: Float32Array;
  tpDataMax: number;
  tpCmdMax: number;
  reqData: RequestArrays;
  reqMaxStackPerLane: Uint16Array;
  reqSlotBase: Uint32Array;
  reqSlotCount: number;
}

let cpuPipelineCache: CpuPipelineCache | null = null;

function buildRequestArrays(
  arrays: TraceArrays,
  strides: number[],
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

function buildReqLookupTexture(colors: RequestTypeStoredColors): Float32Array {
  const MAX = 256;
  const data = new Float32Array(MAX * 4);
  const [mr, mg, mb] = hex2rgb(colors.maintenance);
  for (let i = 0; i < MAX; i++) {
    data[i * 4] = mr; data[i * 4 + 1] = mg; data[i * 4 + 2] = mb; data[i * 4 + 3] = 1;
  }
  const [rr, rg, rb] = hex2rgb(colors.read);
  data[0] = rr; data[1] = rg; data[2] = rb; data[3] = 1;
  const [wr, wg, wb] = hex2rgb(colors.write);
  data[4] = wr; data[5] = wg; data[6] = wb; data[7] = 1;
  return data;
}

function buildSwimlaneLookup(
  rowLayout: RowLayout[],
  strides: number[],
  totalLanes: number,
  canvasTop: number,
  busType: BusType,
): Float32Array {
  const size = Math.max(totalLanes, 1);
  const data = new Float32Array(size * 4);

  for (const row of rowLayout) {
    if (row.busType !== busType) continue;
    if (row.depth === 0 || row.depth > strides.length) continue;
    const yCenter = row.top + row.height / 2 - canvasTop;

    let base = 0;
    for (let k = 0; k < row.depth; k++) base += row.path[k]! * strides[k]!;
    const range = strides[row.depth - 1]!;

    for (let j = 0; j < range; j++) data[(base + j) * 4] = yCenter;
  }

  return data;
}

/** Greedy stack assignment for overlapping [start, end) intervals per lane. */
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

function buildRequestSwimlaneLookup(
  rowLayout: RowLayout[],
  strides: number[],
  canvasTop: number,
  maxStackPerLane: Uint16Array,
  slotBase: Uint32Array,
  reqSlotCount: number,
): Float32Array {
  const data = new Float32Array(Math.max(reqSlotCount, 1) * 4);

  for (const row of rowLayout) {
    if (row.busType !== 'request') continue;
    if (row.depth === 0 || row.depth > strides.length) continue;

    let base = 0;
    for (let k = 0; k < row.depth; k++) base += row.path[k]! * strides[k]!;
    const range = strides[row.depth - 1]!;

    for (let j = 0; j < range; j++) {
      const laneFlat = base + j;
      const stacks = maxStackPerLane[laneFlat] ?? 1;
      const sb = slotBase[laneFlat]!;
      const rowTop = row.top - canvasTop;
      const pitch = ROW_HEIGHT + STACK_ROW_GAP;
      for (let s = 0; s < stacks; s++) {
        const yCenter = rowTop + s * pitch + ROW_HEIGHT * 0.5;
        data[(sb + s) * 4] = yCenter;
      }
    }
  }

  return data;
}

function bisectLeft(arr: Float32Array, target: number, count: number): number {
  let lo = 0;
  let hi = count;
  while (lo < hi) {
    const mid = (lo + hi) >>> 1;
    if (arr[mid]! < target) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}

function bisectRight(arr: Float32Array, target: number, count: number): number {
  let lo = 0;
  let hi = count;
  while (lo < hi) {
    const mid = (lo + hi) >>> 1;
    if (arr[mid]! <= target) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}

// ── Composable ──────────────────────────────────────────────────────

export function useRenderer(
  canvas: Ref<HTMLCanvasElement | null>,
  labelCanvas?: Ref<HTMLCanvasElement | null>,
  followLive?: Ref<boolean>,
) {
  const sessionStore = useSessionStore();
  const uiStore = useUIStore();
  const traceLoadStatus = useTraceLoadStatus();
  const followLiveRef = followLive ?? ref(false);
  /** For viewport: first non-empty trace after a session reset uses auto-fit; later live updates preserve or follow. */
  let lastPipelineN = 0;
  let seenSessionGeneration = -1;
  let pipelineBusy = false;
  let pipelinePending = false;

  async function bumpLoadStatus(msg: string) {
    traceLoadStatus.value = msg;
    await nextTick();
    await new Promise<void>((r) => requestAnimationFrame(() => r()));
  }

  let regl: createREGL.Regl | null = null;
  let resizeObs: ResizeObserver | null = null;
  let abortCtrl: AbortController | null = null;

  const hoveredEvent = ref<HitResult | null>(null);
  const throughputHover = ref<ThroughputHover | null>(null);
  const throughputRangeStats = ref<ThroughputRangeStats | null>(null);
  const rangeSelection = ref<RangeSelection | null>(null);
  const rangeStats = ref<RangeStats | null>(null);
  const cursorMode = ref(false);
  const cursorTime = ref<number | null>(null);
  const mouseX = ref(0);
  const mouseY = ref(0);

  const viewState: ViewState = reactive({
    start: 0,
    duration: 100,
    minDuration: 10,
    maxDuration: 10_000,
    minTime: 0,
    maxTime: 1000,
  });

  const stats: Stats = reactive({
    fps: 0,
    eventCount: 0,
    totalEvents: 0,
  });

  let referenceTime = 0;
  let N = 0;

  // Command bus state (all events, rank-level lanes, per-command duration)
  let cmdRelClk: Float32Array | null = null;
  let cmdLaneIdx: Uint16Array | null = null;
  let cmdStrides: number[] = [];
  let cmdTotalLanes = 1;
  let cmdSwimlaneTex: createREGL.Texture2D | null = null;
  let cmdSwimlaneData: Float32Array | null = null;
  let cmdLookupTex: createREGL.Texture2D | null = null;

  // Data bus state (accessing events only, bank-level lanes, duration = nBL)
  let dataBus: DataBusArrays | null = null;
  let dataStrides: number[] = [];
  let dataTotalLanes = 1;
  let dataSwimlaneTex: createREGL.Texture2D | null = null;
  let dataSwimlaneData: Float32Array | null = null;
  let dataLookupTex: createREGL.Texture2D | null = null;
  let dataNBL = 4;
  /** For trace row index i, data bus instance j, or -1 if no data-phase bar (non-accessing). */
  let dataIdxByOrigIdx: Int32Array | null = null;

  // Request view state (grouped requests, structural-depth lanes, per-instance duration)
  let reqData: RequestArrays | null = null;
  let reqMaxStackPerLane: Uint16Array | null = null;
  let reqSlotBase: Uint32Array | null = null;
  let reqSlotCount = 0;
  let reqSwimlaneTex: createREGL.Texture2D | null = null;
  let reqSwimlaneData: Float32Array | null = null;
  let reqLookupTex: createREGL.Texture2D | null = null;

  // Prefix sums for O(1) range-stat queries (length N+1; index 0 = 0)
  let prefixRead: Uint32Array | null = null;
  let prefixWrite: Uint32Array | null = null;
  let rangeNBL = 4;

  // Throughput view: binned utilisation arrays
  let tpBinSize = 1;
  let tpBinCount = 0;
  /** Data bus utilisation per bin (0-1): accessingCmds * nBL / binSize */
  let tpDataUtil: Float32Array | null = null;
  /** Command bus utilisation per bin (0-1): totalCmds / binSize, capped at 1 */
  let tpCmdUtil: Float32Array | null = null;
  let tpDataMax = 0;
  let tpCmdMax = 0;

  let cmdBusVisible = false;
  let dataBusVisible = false;
  let reqBusVisible = false;
  let lastLayoutVersion = -1;

  let _drawCmd: createREGL.DrawCommand | null = null;
  let _drawData: createREGL.DrawCommand | null = null;
  let _drawCmdHover: createREGL.DrawCommand | null = null;
  let _drawDataHover: createREGL.DrawCommand | null = null;
  let _drawReq: createREGL.DrawCommand | null = null;
  let _drawReqHover: createREGL.DrawCommand | null = null;
  let _drawGrid: createREGL.DrawCommand | null = null;
  let _drawGlow: createREGL.DrawCommand | null = null;
  let gridBuf: createREGL.Buffer | null = null;
  let gridLineCount = 0;

  const GLOW_FADE_MS = 1500;
  let glowTarget: {
    relClk: number;
    duration: number;
    laneIdx: number;
    startedAt: number;
    color: [number, number, number];
  } | null = null;

  // ── GPU buffer handles (lifted to composable scope for streaming updates) ─
  // These are created once per session in runPipeline() and updated in-place
  // by updateStreamingData() — never replaced, so draw-command closures remain valid.
  let cmdRelClkBuf: createREGL.Buffer | null = null;
  let cmdCmdIdBuf: createREGL.Buffer | null = null;
  let cmdLaneIdxBuf: createREGL.Buffer | null = null;
  let dataRelClkBuf: createREGL.Buffer | null = null;
  let dataCmdIdBuf: createREGL.Buffer | null = null;
  let dataLaneIdxBuf: createREGL.Buffer | null = null;
  let reqRelClkBuf: createREGL.Buffer | null = null;
  let reqDurationBuf: createREGL.Buffer | null = null;
  let reqQueueDelayBuf: createREGL.Buffer | null = null;
  let reqTypeIdBuf: createREGL.Buffer | null = null;
  let reqLaneIdxBuf: createREGL.Buffer | null = null;

  // ── Layout-dependent rebuilds ─────────────────────────────────

  function refreshSwimlanes() {
    const el = canvas.value;
    if (!el) return;
    const canvasTop = el.getBoundingClientRect().top;
    const rows = uiStore.rowLayout;

    cmdBusVisible = rows.some(r => r.busType === 'command');
    dataBusVisible = rows.some(r => r.busType === 'data');
    reqBusVisible = rows.some(r => r.busType === 'request');

    if (cmdSwimlaneTex) {
      cmdSwimlaneData = buildSwimlaneLookup(
        rows, cmdStrides, cmdTotalLanes, canvasTop, 'command',
      );
      (cmdSwimlaneTex as any)({
        width: Math.max(cmdTotalLanes, 1), height: 1,
        data: cmdSwimlaneData, format: 'rgba', type: 'float',
        min: 'nearest', mag: 'nearest',
      });
    }

    if (dataSwimlaneTex) {
      dataSwimlaneData = buildSwimlaneLookup(
        rows, dataStrides, dataTotalLanes, canvasTop, 'data',
      );
      (dataSwimlaneTex as any)({
        width: Math.max(dataTotalLanes, 1), height: 1,
        data: dataSwimlaneData, format: 'rgba', type: 'float',
        min: 'nearest', mag: 'nearest',
      });
    }

    if (reqSwimlaneTex && reqMaxStackPerLane && reqSlotBase) {
      reqSwimlaneData = buildRequestSwimlaneLookup(
        rows, dataStrides, canvasTop, reqMaxStackPerLane, reqSlotBase, reqSlotCount,
      );
      (reqSwimlaneTex as any)({
        width: Math.max(reqSlotCount, 1), height: 1,
        data: reqSwimlaneData, format: 'rgba', type: 'float',
        min: 'nearest', mag: 'nearest',
      });
    }
  }

  function refreshGrid() {
    const el = canvas.value;
    if (!el || !gridBuf) return;
    const rows = uiStore.rowLayout;
    if (rows.length === 0) {
      gridLineCount = 0;
      return;
    }
    const canvasTop = el.getBoundingClientRect().top;
    const lines = new Float32Array(rows.length);
    for (let i = 0; i < rows.length; i++)
      lines[i] = rows[i]!.top + rows[i]!.height - canvasTop;
    gridBuf.subdata(lines, 0);
    gridLineCount = rows.length;
  }

  // ── Hit testing ───────────────────────────────────────────────

  function hitTestBus(
    clientX: number, clientY: number,
    busType: BusType,
    relClk: Float32Array, laneIdxArr: Uint16Array,
    swimlaneData: Float32Array | null,
    maxDuration: number, count: number,
    resolveIndex: (j: number) => number,
    getDuration: (j: number, origIdx: number) => number,
  ): HitResult | null {
    const el = canvas.value;
    if (!el || !swimlaneData || count === 0) return null;

    const rect = el.getBoundingClientRect();
    const cx = clientX - rect.left;
    const cy = clientY - rect.top;

    const relStart = viewState.start - referenceTime;
    const relTime = relStart + (cx / el.width) * viewState.duration;

    const lo = Math.max(0, bisectLeft(relClk, relTime - maxDuration, count));
    const hi = Math.min(count, bisectRight(relClk, relTime, count));

    const arrays = sessionStore.arrays!;
    const spec = sessionStore.spec!;

    for (let j = lo; j < hi; j++) {
      const t = relClk[j]!;
      const origIdx = resolveIndex(j);
      const dur = getDuration(j, origIdx);
      if (relTime < t || relTime > t + dur) continue;

      const yCenter = swimlaneData[laneIdxArr[j]! * 4]!;
      if (Math.abs(cy - yCenter) > ROW_HEIGHT / 2) continue;

      const cid = arrays.cmdId[origIdx]!;

      const addr: number[] = [];
      for (let k = 0; k < arrays.addr.length; k++) addr.push(arrays.addr[k]![origIdx]!);

      const result: HitResult = {
        index: origIdx,
        cmdId: cid,
        clk: arrays.clk[origIdx]!,
        typeId: arrays.typeId[origIdx]!,
        sourceId: arrays.sourceId[origIdx]!,
        addr,
        bus: busType,
        duration: dur,
      };

      if (busType === 'data') {
        const offset = isWriteCommand(spec.commandNames[cid]!)
          ? getTimingValue(spec, 'nCWL', 12)
          : getTimingValue(spec, 'nCL', 16);
        result.busClk = Number(arrays.clk[origIdx]!) + offset;
      }

      return result;
    }

    return null;
  }

  function hitTestRequests(clientX: number, clientY: number): HitResult | null {
    const el = canvas.value;
    if (!el || !reqSwimlaneData || !reqData || reqData.count === 0) return null;

    const rect = el.getBoundingClientRect();
    const cx = clientX - rect.left;
    const cy = clientY - rect.top;

    const relStart = viewState.start - referenceTime;
    const relTime = relStart + (cx / el.width) * viewState.duration;

    const lo = Math.max(0, bisectLeft(reqData.relClk, relTime - reqData.maxDuration, reqData.count));
    const hi = Math.min(reqData.count, bisectRight(reqData.relClk, relTime, reqData.count));

    const arrays = sessionStore.arrays!;

    for (let j = lo; j < hi; j++) {
      const t = reqData.relClk[j]!;
      const dur = reqData.duration[j]!;
      if (relTime < t || relTime > t + dur) continue;

      const yCenter = reqSwimlaneData[reqData.slotId[j]! * 4]!;
      if (Math.abs(cy - yCenter) > ROW_HEIGHT / 2) continue;

      const origIdx = reqData.representativeIdx[j]!;
      const addr: number[] = [];
      for (let k = 0; k < arrays.addr.length; k++) addr.push(arrays.addr[k]![origIdx]!);

      const qd = reqData.queueDelay[j]!;
      return {
        index: origIdx,
        cmdId: reqData.typeId[j]!,
        clk: arrays.arrive[origIdx]!,
        typeId: arrays.typeId[origIdx]!,
        sourceId: arrays.sourceId[origIdx]!,
        addr,
        bus: 'request' as BusType,
        duration: Math.max(0, dur - qd),
        queueDelay: qd,
        requestGroupIndex: j,
      };
    }

    return null;
  }

  function throughputSampleRange(relStart: number, relEnd: number, widthPx: number): {
    startBin: number;
    endBin: number;
    stride: number;
  } | null {
    if (tpBinCount <= 0 || tpBinSize <= 0) return null;
    const startBin = Math.max(0, Math.floor(relStart / tpBinSize));
    const right = Math.max(relStart, relEnd - 1e-9);
    const endBin = Math.min(tpBinCount - 1, Math.floor(right / tpBinSize));
    if (endBin < startBin) return null;
    const visibleBins = endBin - startBin + 1;
    const maxPoints = Math.max(120, Math.min(TP_MAX_DRAW_POINTS, Math.floor(widthPx * 0.45)));
    const stride = Math.max(1, Math.ceil(visibleBins / maxPoints));
    return { startBin, endBin, stride };
  }

  function buildThroughputPoints(
    bins: Float32Array,
    yMax: number,
    areaTop: number,
    areaH: number,
    relStart: number,
    relEnd: number,
    vw: number,
    widthPx: number,
  ): Array<{ x: number; y: number; bin: number; value: number; binStartTime: number; binEndTime: number }> {
    const pad = 8;
    const chartTop = areaTop + pad;
    const chartH = areaH - pad * 2;
    if (chartH <= 0) return [];

    const sampled = throughputSampleRange(relStart, relEnd, widthPx);
    if (!sampled) return [];

    const effectiveMax = Math.max(yMax, 0.01);
    const points: Array<{ x: number; y: number; bin: number; value: number; binStartTime: number; binEndTime: number }> = [];
    for (let b = sampled.startBin; b <= sampled.endBin; b += sampled.stride) {
      const x = (((b + 0.5) * tpBinSize - relStart) / vw) * widthPx;
      const value = bins[b]!;
      const y = chartTop + chartH - (value / effectiveMax) * chartH;
      points.push({
        x,
        y,
        bin: b,
        value,
        binStartTime: referenceTime + b * tpBinSize,
        binEndTime: referenceTime + (b + 1) * tpBinSize,
      });
    }
    if (points.length === 0 || points[points.length - 1]!.bin !== sampled.endBin) {
      const b = sampled.endBin;
      const x = (((b + 0.5) * tpBinSize - relStart) / vw) * widthPx;
      const value = bins[b]!;
      const y = chartTop + chartH - (value / effectiveMax) * chartH;
      points.push({
        x,
        y,
        bin: b,
        value,
        binStartTime: referenceTime + b * tpBinSize,
        binEndTime: referenceTime + (b + 1) * tpBinSize,
      });
    }
    return points;
  }

  function hitTestThroughput(clientX: number, clientY: number): ThroughputHover | null {
    const el = canvas.value;
    if (!el || uiStore.viewMode !== 'throughput' || !tpDataUtil || !tpCmdUtil || tpBinCount === 0) return null;

    const rect = el.getBoundingClientRect();
    if (
      clientX < rect.left || clientX > rect.right
      || clientY < rect.top || clientY > rect.bottom
    ) {
      return null;
    }

    const rows = uiStore.rowLayout;
    const dataRow = rows.find(r => r.busType === 'tp_data');
    const cmdRow = rows.find(r => r.busType === 'tp_cmd');
    const inData = !!dataRow && clientY >= dataRow.top && clientY <= dataRow.top + dataRow.height;
    const inCmd = !!cmdRow && clientY >= cmdRow.top && clientY <= cmdRow.top + cmdRow.height;
    if (!inData && !inCmd) return null;
    const targetSeries = inData ? 'data' as const : 'cmd' as const;
    const targetBins = targetSeries === 'data' ? tpDataUtil : tpCmdUtil;
    const targetRow = targetSeries === 'data' ? dataRow : cmdRow;
    const targetMax = targetSeries === 'data' ? tpDataMax : tpCmdMax;
    if (!targetRow) return null;

    const tpRelStart = viewState.start - referenceTime;
    const tpRelEnd = tpRelStart + viewState.duration;
    const points = buildThroughputPoints(
      targetBins,
      targetMax,
      targetRow.top - rect.top,
      targetRow.height,
      tpRelStart,
      tpRelEnd,
      viewState.duration,
      el.width,
    );
    if (points.length === 0) return null;

    const cx = clientX - rect.left;
    const cy = clientY - rect.top;
    let best = -1;
    let bestD2 = TP_HOVER_RADIUS * TP_HOVER_RADIUS;
    for (let i = 0; i < points.length; i++) {
      const p = points[i]!;
      const dx = p.x - cx;
      const dy = p.y - cy;
      const d2 = dx * dx + dy * dy;
      if (d2 <= bestD2) {
        bestD2 = d2;
        best = i;
      }
    }
    if (best < 0) return null;
    const point = points[best]!;

    return {
      series: targetSeries,
      binIndex: point.bin,
      value: point.value,
      binStartTime: point.binStartTime,
      binEndTime: point.binEndTime,
    };
  }

  function recomputeThroughputRangeStats(sel: RangeSelection | null) {
    if (!sel || !tpDataUtil || !tpCmdUtil || tpBinCount === 0 || tpBinSize <= 0) {
      throughputRangeStats.value = null;
      return;
    }
    const relStart = sel.startTime - referenceTime;
    const relEnd = sel.endTime - referenceTime;
    if (relEnd <= relStart) {
      throughputRangeStats.value = null;
      return;
    }
    const startBin = Math.max(0, Math.floor(relStart / tpBinSize));
    const right = Math.max(relStart, relEnd - 1e-9);
    const endBin = Math.min(tpBinCount - 1, Math.floor(right / tpBinSize));
    if (endBin < startBin) {
      throughputRangeStats.value = null;
      return;
    }
    let sumData = 0;
    let sumCmd = 0;
    const binCount = endBin - startBin + 1;
    for (let b = startBin; b <= endBin; b++) {
      sumData += tpDataUtil[b]!;
      sumCmd += tpCmdUtil[b]!;
    }
    throughputRangeStats.value = {
      dataMeanUtil: sumData / binCount,
      cmdMeanUtil: sumCmd / binCount,
      binCount,
    };
  }

  function recomputeRangeStats() {
    const sel = rangeSelection.value;
    recomputeThroughputRangeStats(sel);
    if (!sel || !cmdRelClk || !prefixRead || !prefixWrite || N === 0) {
      rangeStats.value = null;
      return;
    }

    const duration = sel.endTime - sel.startTime;
    if (duration <= 0) { rangeStats.value = null; return; }

    const relStart = sel.startTime - referenceTime;
    const relEnd = sel.endTime - referenceTime;
    const lo = bisectLeft(cmdRelClk, relStart, N);
    const hi = bisectRight(cmdRelClk, relEnd, N);

    const readCount = prefixRead[hi]! - prefixRead[lo]!;
    const writeCount = prefixWrite[hi]! - prefixWrite[lo]!;
    const accessingCount = readCount + writeCount;
    const totalCommands = hi - lo;

    rangeStats.value = {
      durationCycles: duration,
      totalCommands,
      readCommands: readCount,
      writeCommands: writeCount,
      otherCommands: totalCommands - accessingCount,
      accessingCommands: accessingCount,
      cmdBusThroughput: totalCommands / duration,
    };
  }

  function clampAbsoluteTime(t: number): number {
    return Math.max(viewState.minTime, Math.min(viewState.maxTime, t));
  }

  function activeCursorStarts(): { relClk: Float32Array; count: number } | null {
    if (uiStore.viewMode === 'command' && cmdRelClk && N > 0) {
      return { relClk: cmdRelClk, count: N };
    }
    if (uiStore.viewMode === 'request' && reqData && reqData.count > 0) {
      return { relClk: reqData.relClk, count: reqData.count };
    }
    return null;
  }

  function keepCursorInView(time: number) {
    if (time >= viewState.start && time <= viewState.start + viewState.duration) return;
    const maxStart = viewState.maxTime - viewState.duration;
    viewState.start = Math.max(
      viewState.minTime,
      Math.min(time - viewState.duration / 2, maxStart),
    );
  }

  function placeCursorAtClientX(clientX: number) {
    const el = canvas.value;
    if (!el) return;
    const rect = el.getBoundingClientRect();
    const frac = (clientX - rect.left) / rect.width;
    cursorTime.value = clampAbsoluteTime(viewState.start + frac * viewState.duration);
  }

  function jumpCursor(direction: CursorJumpDirection): boolean {
    const current = cursorTime.value;
    const starts = activeCursorStarts();
    if (current == null || !starts) return false;

    const relCurrent = current - referenceTime;
    const idx = direction === 'next'
      ? bisectRight(starts.relClk, relCurrent, starts.count)
      : bisectLeft(starts.relClk, relCurrent, starts.count) - 1;

    if (idx < 0 || idx >= starts.count) return false;

    const nextTime = clampAbsoluteTime(starts.relClk[idx]! + referenceTime);
    cursorTime.value = nextTime;
    keepCursorInView(nextTime);
    hoveredEvent.value = null;
    throughputHover.value = null;
    return true;
  }

  function clearCursor() {
    cursorTime.value = null;
  }

  function setCursorMode(enabled: boolean) {
    cursorMode.value = enabled;
  }

  function drawCursorLine(ctx: CanvasRenderingContext2D, width: number, height: number) {
    const t = cursorTime.value;
    if (t == null) return;
    const x = ((t - viewState.start) / viewState.duration) * width;
    if (x < 0 || x > width) return;

    const sx = Math.round(x) + 0.5;
    ctx.save();
    ctx.fillStyle = CURSOR_FILL;
    ctx.fillRect(Math.max(0, sx - 2.5), 0, 5, height);
    ctx.strokeStyle = CURSOR_COLOR;
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(sx, 0);
    ctx.lineTo(sx, height);
    ctx.stroke();
    ctx.restore();
  }

  function drawOverlayLabels() {
    if (!labelCanvas) return;
    const el = canvas.value;
    const lo = labelCanvas.value;
    if (!el || !lo) return;
    const ctxMaybe = lo.getContext('2d');
    if (!ctxMaybe) return;
    const ctx = ctxMaybe;

    if (lo.width !== el.width || lo.height !== el.height) {
      lo.width = el.width;
      lo.height = el.height;
    }
    ctx.clearRect(0, 0, lo.width, lo.height);

    const spec = sessionStore.spec;
    const arrays = sessionStore.arrays;
    if (!spec || !arrays) return;

    const vw = viewState.duration;
    const pxPerCycle = el.width / vw;
    const relStart = viewState.start - referenceTime;
    const relEnd = relStart + vw;
    const viewBase = Math.floor(relStart);
    const viewFrac = relStart - viewBase;

    // ── Vertical gridlines at major ticks ───────────────────────
    {
      const step = niceStep(vw, el.width);
      const absStart = viewState.start;
      const majorStart = Math.ceil(absStart / step) * step;
      const absEnd = absStart + vw;
      const eps = step / 1000;

      ctx.beginPath();
      ctx.strokeStyle = VGRID_COLOR;
      ctx.lineWidth = 1;
      for (let t = majorStart; t <= absEnd + eps; t += step) {
        if (t < viewState.minTime || t > viewState.maxTime) continue;
        const x = Math.round(((t - absStart) / vw) * el.width) + 0.5;
        ctx.moveTo(x, 0);
        ctx.lineTo(x, el.height);
      }
      ctx.stroke();
    }

    // ── Range selection highlight ──────────────────────────────
    if (rangeSelection.value) {
      const sel = rangeSelection.value;
      const absStart = viewState.start;
      const xLeft = ((sel.startTime - absStart) / vw) * el.width;
      const xRight = ((sel.endTime - absStart) / vw) * el.width;
      const x0 = Math.max(0, Math.min(el.width, xLeft));
      const x1 = Math.max(0, Math.min(el.width, xRight));

      if (x1 > x0) {
        ctx.fillStyle = RANGE_FILL;
        ctx.fillRect(x0, 0, x1 - x0, el.height);

        ctx.beginPath();
        ctx.strokeStyle = RANGE_EDGE;
        ctx.lineWidth = 1;
        if (xLeft >= 0 && xLeft <= el.width) {
          const lx = Math.round(xLeft) + 0.5;
          ctx.moveTo(lx, 0);
          ctx.lineTo(lx, el.height);
        }
        if (xRight >= 0 && xRight <= el.width) {
          const rx = Math.round(xRight) + 0.5;
          ctx.moveTo(rx, 0);
          ctx.lineTo(rx, el.height);
        }
        ctx.stroke();
      }
    }

    // ── Throughput charts ────────────────────────────────────
    if (uiStore.viewMode === 'throughput' && tpDataUtil && tpCmdUtil && tpBinCount > 0) {
      const rows = uiStore.rowLayout;
      const canvasTop = el.getBoundingClientRect().top;
      let dataRow: { top: number; height: number } | null = null;
      let cmdRow: { top: number; height: number } | null = null;
      for (const r of rows) {
        if ((r as any).busType === 'tp_data') dataRow = r;
        if ((r as any).busType === 'tp_cmd') cmdRow = r;
      }

      const drawChart = (
        bins: Float32Array,
        yMax: number,
        areaTop: number,
        areaH: number,
        series: 'data' | 'cmd',
        strokeColor: string,
        fillColor: string,
      ) => {
        const pad = 8;
        const chartTop = areaTop + pad;
        const chartH = areaH - pad * 2;
        if (chartH <= 0) return;
        const effectiveMax = Math.max(yMax, 0.01);

        ctx.save();
        ctx.beginPath();
        ctx.rect(0, areaTop, el.width, areaH);
        ctx.clip();

        if (rangeSelection.value) {
          const sel = rangeSelection.value;
          const x0 = ((sel.startTime - viewState.start) / vw) * el.width;
          const x1 = ((sel.endTime - viewState.start) / vw) * el.width;
          const sx = Math.max(0, Math.min(el.width, Math.min(x0, x1)));
          const ex = Math.max(0, Math.min(el.width, Math.max(x0, x1)));
          if (ex > sx) {
            ctx.fillStyle = 'rgba(255,255,255,0.05)';
            ctx.fillRect(sx, areaTop, ex - sx, areaH);
          }
        }

        // Y-axis labels
        ctx.font = '9px ui-monospace, JetBrains Mono, monospace';
        ctx.textAlign = 'right';
        ctx.textBaseline = 'middle';
        ctx.fillStyle = '#52525b';
        const yTicks = [0, 0.25, 0.5, 0.75, 1.0];
        for (const frac of yTicks) {
          const val = effectiveMax * frac;
          const y = chartTop + chartH - frac * chartH;
          if (y < areaTop || y > areaTop + areaH) continue;
          ctx.fillText((val * 100).toFixed(0) + '%', 30, y);
          // Grid line
          ctx.beginPath();
          ctx.strokeStyle = 'rgba(255,255,255,0.04)';
          ctx.lineWidth = 1;
          ctx.moveTo(34, Math.round(y) + 0.5);
          ctx.lineTo(el.width, Math.round(y) + 0.5);
          ctx.stroke();
        }

        // Line + fill (point-decimated by pixel width for stable FPS when zoomed out)
        const tpRelStart = viewState.start - referenceTime;
        const tpRelEnd = tpRelStart + vw;
        const points = buildThroughputPoints(
          bins, yMax, areaTop, areaH, tpRelStart, tpRelEnd, vw, el.width,
        );
        if (points.length === 0) {
          ctx.restore();
          return;
        }
        const linePoints: Array<{ x: number; y: number }> = [];
        const first = points[0]!;
        const last = points[points.length - 1]!;
        if (first.x > 0) linePoints.push({ x: 0, y: first.y });
        for (const p of points) linePoints.push({ x: p.x, y: p.y });
        if (last.x < el.width) linePoints.push({ x: el.width, y: last.y });

        ctx.beginPath();
        for (let i = 0; i < linePoints.length; i++) {
          const p = linePoints[i]!;
          if (i === 0) ctx.moveTo(p.x, p.y);
          else ctx.lineTo(p.x, p.y);
        }
        ctx.strokeStyle = strokeColor;
        ctx.lineWidth = 1.5;
        ctx.stroke();

        // Fill under line
        ctx.lineTo(linePoints[linePoints.length - 1]!.x, chartTop + chartH);
        ctx.lineTo(linePoints[0]!.x, chartTop + chartH);
        ctx.closePath();
        ctx.fillStyle = fillColor;
        ctx.fill();

        // Circular point markers (adaptively thinned as we zoom out).
        // Keep line detail, but avoid drawing circles that are too close in screen space.
        const markerIndices: number[] = [];
        let lastMarkerX = Number.NEGATIVE_INFINITY;
        for (let i = 0; i < points.length; i++) {
          const p = points[i]!;
          if (i === points.length - 1 || p.x - lastMarkerX >= TP_MARKER_MIN_GAP_PX) {
            markerIndices.push(i);
            lastMarkerX = p.x;
          }
        }

        ctx.beginPath();
        for (const idx of markerIndices) {
          const p = points[idx]!;
          ctx.moveTo(p.x + TP_POINT_RADIUS, p.y);
          ctx.arc(p.x, p.y, TP_POINT_RADIUS, 0, Math.PI * 2);
        }
        ctx.fillStyle = 'rgba(24, 24, 27, 0.96)';
        ctx.fill();
        ctx.strokeStyle = strokeColor;
        ctx.lineWidth = 1;
        ctx.stroke();

        // Emphasize currently hovered marker.
        const hov = throughputHover.value;
        if (hov && hov.series === series) {
          const hp = points.find(p => p.bin === hov.binIndex);
          if (hp) {
            ctx.beginPath();
            ctx.arc(hp.x, hp.y, TP_POINT_RADIUS + 2, 0, Math.PI * 2);
            ctx.fillStyle = 'rgba(255, 255, 255, 0.18)';
            ctx.fill();
            ctx.beginPath();
            ctx.arc(hp.x, hp.y, TP_POINT_RADIUS + 0.5, 0, Math.PI * 2);
            ctx.fillStyle = strokeColor;
            ctx.fill();
          }
        }

        ctx.restore();
      };

      if (dataRow) {
        const top = dataRow.top - canvasTop;
        drawChart(tpDataUtil, tpDataMax, top, dataRow.height, 'data',
          'rgba(59, 130, 246, 0.9)', 'rgba(59, 130, 246, 0.08)');
      }
      if (cmdRow) {
        const top = cmdRow.top - canvasTop;
        drawChart(tpCmdUtil, tpCmdMax, top, cmdRow.height, 'cmd',
          'rgba(251, 191, 36, 0.9)', 'rgba(251, 191, 36, 0.08)');
      }

      drawCursorLine(ctx, el.width, el.height);
      return;
    }

    ctx.font = '10px ui-monospace, JetBrains Mono, monospace';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';

    const drawLabel = (sx: number, y: number, raw: string) => {
      const label = raw.length > 14 ? `${raw.slice(0, 13)}…` : raw;
      ctx.shadowColor = 'rgba(0,0,0,0.85)';
      ctx.shadowBlur = 4;
      ctx.fillStyle = 'rgba(250,250,250,0.92)';
      ctx.fillText(label, sx, y);
      ctx.shadowBlur = 0;
    };

    if (uiStore.viewMode === 'command') {
      if (cmdBusVisible && N > 0 && cmdRelClk && cmdLaneIdx && cmdSwimlaneData
        && pxPerCycle >= CMD_LABEL_MIN_PX_PER_CYCLE) {
        const startIdx = Math.max(0, bisectLeft(cmdRelClk, relStart - getMaxCommandDuration(spec), N));
        const endIdx = Math.min(N, bisectRight(cmdRelClk, relEnd, N));
        if (endIdx > startIdx) {
          const labelEnd = Math.min(endIdx, startIdx + CMD_LABEL_MAX_VISIBLE);
          for (let i = startIdx; i < labelEnd; i++) {
            const localRel = cmdRelClk[i]! - viewBase;
            const cid = arrays.cmdId[i]!;
            const halfW = getCommandDuration(spec, cid) * 0.5;
            const cxWorld = localRel + halfW;
            const sx = ((cxWorld - viewFrac) / vw) * el.width;
            const lane = cmdLaneIdx[i]!;
            const y = cmdSwimlaneData[lane * 4]!;
            drawLabel(sx, y, spec.commandNames[cid] ?? '?');
          }
        }
      }

      if (
        dataBusVisible && dataBus && dataBus.count > 0 && dataSwimlaneData
        && pxPerCycle * dataNBL >= LABEL_MIN_BAR_WIDTH_PX
      ) {
        const M = dataBus.count;
        const startIdx = Math.max(0, bisectLeft(dataBus.relClk, relStart - dataNBL, M));
        const endIdx = Math.min(M, bisectRight(dataBus.relClk, relEnd, M));
        if (endIdx > startIdx) {
          const halfD = dataNBL * 0.5;
          const labelEnd = Math.min(endIdx, startIdx + CMD_LABEL_MAX_VISIBLE);
          for (let j = startIdx; j < labelEnd; j++) {
            const localRel = dataBus.relClk[j]! - viewBase;
            const cxWorld = localRel + halfD;
            const sx = ((cxWorld - viewFrac) / vw) * el.width;
            const lane = dataBus.laneIdx[j]!;
            const y = dataSwimlaneData[lane * 4]!;
            const cid = dataBus.cmdId[j]!;
            drawLabel(sx, y, spec.commandNames[cid] ?? '?');
          }
        }
      }
    }

    drawCursorLine(ctx, el.width, el.height);
  }

  /** Apply immediately (not in nextTick) so the next regl frame sees the new window — deferred updates looked like “constant had no effect”. */
  function applyNavZoomCenter(targetRelClk: number, zoomCycles: number) {
    const span = viewState.maxTime - viewState.minTime;
    viewState.duration = Math.max(
      viewState.minDuration,
      Math.min(zoomCycles, span),
    );
    const targetAbs = targetRelClk + referenceTime;
    let s = targetAbs - viewState.duration / 2;
    s = Math.max(viewState.minTime, Math.min(s, viewState.maxTime - viewState.duration));
    viewState.start = s;
  }

  function navigateToCounterpart(hit: HitResult) {
    const arrays = sessionStore.arrays!;
    const spec = sessionStore.spec;
    if (!spec) return;

    if (hit.bus === 'request') {
      const origIdx = hit.index;
      const targetRelClk = Number(arrays.clk[origIdx]!) - referenceTime;
      const targetDuration = getCommandDuration(spec, arrays.cmdId[origIdx]!);
      const cmdLane = cmdLaneIdx ? cmdLaneIdx[origIdx]! : 0;

      const rl = rankLevelIndex(spec);
      const sd = specStructuralDepth(spec);
      const pathRank = pathFromAddrForTree(spec, arrays.addr, origIdx, rl);
      const pathLane = pathFromAddrForTree(spec, arrays.addr, origIdx, sd - 1);
      const minIds = [
        '_channel',
        '_bus_cmd',
        '_bus_data',
        nodeId('cmd', pathRank),
        nodeId('data', pathRank),
      ];

      uiStore.setViewMode('command');
      uiStore.mergeExpandedForNavigation(minIds);
      uiStore.treeScrollTarget = { busType: 'command', path: pathLane };

      glowTarget = {
        relClk: targetRelClk,
        duration: targetDuration,
        laneIdx: cmdLane,
        startedAt: performance.now(),
        color: [1, 1, 1],
      };
      applyNavZoomCenter(
        targetRelClk + targetDuration / 2,
        NAV_ZOOM_CMD_CYCLES,
      );
    } else {
      const origIdx = hit.index;
      const arrive = Number(arrays.arrive[origIdx]!);
      if (!reqData || arrive < 0) return;

      const bgl = bankGroupLevelIndex(spec);
      const pathBg = pathFromAddrForTree(spec, arrays.addr, origIdx, bgl);
      const minIds = ['_channel', nodeId('req', pathBg)];

      for (let j = 0; j < reqData.count; j++) {
        const repIdx = reqData.representativeIdx[j]!;
        if (Number(arrays.arrive[repIdx]!) !== arrive) continue;

        let matchedLane = true;
        const structDepth = getStructuralDepth(spec);
        for (let k = 0; k < structDepth; k++) {
          if (arrays.addr[k]![repIdx]! !== arrays.addr[k]![origIdx]!) {
            matchedLane = false;
            break;
          }
        }
        if (!matchedLane) continue;

        const targetRelClk = reqData.relClk[j]!;
        const dur = reqData.duration[j]!;
        const qd = reqData.queueDelay[j]!;
        const typeId = reqData.typeId[j]!;
        const glowRgb = hex2rgb(sessionStore.getRequestTypeColor(typeId));

        uiStore.setViewMode('request');
        uiStore.mergeExpandedForNavigation(minIds);
        uiStore.treeScrollTarget = { busType: 'request', path: pathBg };

        glowTarget = {
          relClk: targetRelClk,
          duration: dur,
          laneIdx: reqData.slotId[j]!,
          startedAt: performance.now(),
          color: [glowRgb[0], glowRgb[1], glowRgb[2]],
        };
        // Frame queue + active span with margin; fixed constants alone were often dwarfed by long requests.
        const totalReqClk = dur + qd;
        const zoomReq = Math.max(
          NAV_ZOOM_REQ_CYCLES,
          Math.ceil(totalReqClk * 2.5 + NAV_ZOOM_REQ_PAD),
        );
        applyNavZoomCenter(targetRelClk, zoomReq);
        return;
      }

      uiStore.setViewMode('request');
      uiStore.mergeExpandedForNavigation(minIds);
      uiStore.treeScrollTarget = { busType: 'request', path: pathBg };
      const relArrive = arrive - referenceTime;
      applyNavZoomCenter(relArrive, NAV_ZOOM_REQ_CYCLES);
    }
  }

  function hitTest(clientX: number, clientY: number): HitResult | null {
    if (uiStore.viewMode === 'throughput') return null;
    if (uiStore.viewMode === 'request') {
      return reqBusVisible ? hitTestRequests(clientX, clientY) : null;
    }

    const cmdHit = cmdBusVisible && cmdRelClk
      ? hitTestBus(
          clientX, clientY, 'command',
          cmdRelClk, cmdLaneIdx!, cmdSwimlaneData,
          getMaxCommandDuration(sessionStore.spec), N, (j) => j,
          (_j, origIdx) => getCommandDuration(sessionStore.spec, sessionStore.arrays!.cmdId[origIdx]!),
        )
      : null;

    const dataHit = dataBusVisible && dataBus
      ? hitTestBus(
          clientX, clientY, 'data',
          dataBus.relClk, dataBus.laneIdx, dataSwimlaneData,
          dataNBL, dataBus.count, (j) => dataBus!.originalIndices[j]!,
          () => dataNBL,
        )
      : null;

    return cmdHit ?? dataHit;
  }

  // ── Lifecycle ─────────────────────────────────────────────────

  let refreshCommandLookupTextures: (() => void) | null = null;
  let refreshRequestLookupTextures: (() => void) | null = null;

  /**
   * Fast streaming-refresh path: recomputes all CPU-side derived arrays and
   * re-uploads data to the existing GPU buffers in-place.  The regl context,
   * compiled draw commands, lookup textures, and interaction handlers are
   * untouched — the frame loop keeps running without a single dropped frame.
   *
   * Called instead of the full runPipeline() when sessionGeneration is
   * unchanged (i.e., this is an incremental live-streaming update, not a new
   * session or a full offline trace load).
   */
  let worker: Worker | null = null;

  async function updateStreamingData(
    el: HTMLCanvasElement,
    arrays: TraceArrays,
    header: TraceHeader,
    spec: TraceSpec,
  ) {
    N = header.numEntries;
    stats.totalEvents = N;
    stats.eventCount = N;
    if (N === 0) return;

    if (lastPipelineN === 0) {
      referenceTime = Number(arrays.clk[0]);
    }

    if (!worker) {
      worker = new StreamingWorker();
    }

    const structDepth = getStructuralDepth(spec);

    const rawArrays = toRaw(arrays);
    const rawSpec = toRaw(spec);

    const payload = {
      arrays: {
        clk: rawArrays.clk,
        arrive: rawArrays.arrive,
        cmdId: rawArrays.cmdId,
        typeId: rawArrays.typeId,
        sourceId: rawArrays.sourceId,
        addr: toRaw(rawArrays.addr).map(a => toRaw(a)),
      },
      spec: {
        levelNames: toRaw(rawSpec.levelNames),
        levelSizes: toRaw(rawSpec.levelSizes),
        commandNames: toRaw(rawSpec.commandNames),
        commandMeta: toRaw(rawSpec.commandMeta).map(m => toRaw(m)),
        commandCycles: toRaw(rawSpec.commandCycles),
        timingNames: toRaw(rawSpec.timingNames),
        timingValues: toRaw(rawSpec.timingValues),
      },
      N,
      referenceTime,
      readLatency: header.readLatency,
      cmdStrides,
      dataStrides,
      structDepth,
      dataTotalLanes,
    };

    const msg = await new Promise<any>((resolve, reject) => {
      worker!.onmessage = (e) => resolve(e.data);
      worker!.onerror = (e) => reject(e);
      worker!.postMessage(payload);
    });

    cmdRelClk = msg.cmdRelClk;
    cmdLaneIdx = msg.cmdLaneIdx;
    dataBus = msg.dataBus;
    dataIdxByOrigIdx = msg.dataIdxByOrigIdx;
    prefixRead = msg.prefixRead;
    prefixWrite = msg.prefixWrite;
    tpBinSize = msg.tpBinSize;
    tpBinCount = msg.tpBinCount;
    tpDataUtil = msg.tpDataUtil;
    tpCmdUtil = msg.tpCmdUtil;
    tpDataMax = msg.tpDataMax;
    tpCmdMax = msg.tpCmdMax;
    reqData = msg.reqData;

    dataNBL = getTimingValue(spec, 'nBL', 4);
    rangeNBL = dataNBL;

    recomputeThroughputRangeStats(rangeSelection.value);

    const prevSlotCount = reqSlotCount;
    reqMaxStackPerLane = reqData!.maxStackPerLane;
    reqSlotBase = reqData!.slotBase;
    reqSlotCount = reqData!.reqSlotCount;

    // ── GPU buffer updates (in-place, no context teardown) ───────
    if (cmdRelClkBuf) cmdRelClkBuf(cmdRelClk!);
    if (cmdCmdIdBuf) cmdCmdIdBuf(rawArrays.cmdId);
    if (cmdLaneIdxBuf) cmdLaneIdxBuf(cmdLaneIdx!);

    if (dataRelClkBuf) dataRelClkBuf(dataBus!.relClk);
    if (dataCmdIdBuf) dataCmdIdBuf(dataBus!.cmdId);
    if (dataLaneIdxBuf) dataLaneIdxBuf(dataBus!.laneIdx);

    if (reqRelClkBuf) reqRelClkBuf(reqData!.relClk);
    if (reqDurationBuf) reqDurationBuf(reqData!.duration);
    if (reqQueueDelayBuf) reqQueueDelayBuf(reqData!.queueDelay);
    if (reqTypeIdBuf) reqTypeIdBuf(reqData!.typeId);
    if (reqLaneIdxBuf) reqLaneIdxBuf(reqData!.slotId);

    // If the request swimlane count changed, resize the texture in-place.
    // The draw command references reqSwimlaneTex via a closure returning the
    // same JS object, so resizing it transparently updates the GPU texture.
    if (reqSlotCount !== prevSlotCount && reqSwimlaneTex) {
      const canvasTop = el.getBoundingClientRect().top;
      const newSwimlaneData = buildRequestSwimlaneLookup(
        uiStore.rowLayout, dataStrides, canvasTop, reqMaxStackPerLane!, reqSlotBase!, reqSlotCount,
      );
      (reqSwimlaneTex as any)({
        width: Math.max(reqSlotCount, 1), height: 1,
        data: newSwimlaneData, format: 'rgba', type: 'float',
        min: 'nearest', mag: 'nearest',
      });
    }

    sessionStore.setRequestLaneStackDepth(Array.from(reqData!.maxStackPerLane));

    // ── Viewport bounds update ────────────────────────────────────
    const savedStart = viewState.start;
    const savedDuration = viewState.duration;
    const lastCmdClk = cmdRelClk![N - 1]!;
    const lastDataClk = dataBus!.count > 0 ? dataBus!.relClk[dataBus!.count - 1]! : 0;
    const span = Math.max(lastCmdClk, lastDataClk) + dataNBL + 1;
    viewState.minTime = referenceTime;
    viewState.maxTime = referenceTime + span;
    viewState.maxDuration = span;

    const dur = Math.max(viewState.minDuration, Math.min(savedDuration, span, viewState.maxDuration));
    viewState.duration = dur;
    if (followLiveRef.value) {
      const tailStart = referenceTime + span - dur;
      viewState.start = Math.max(viewState.minTime, Math.min(tailStart, viewState.maxTime - dur));
    } else {
      viewState.start = Math.max(viewState.minTime, Math.min(savedStart, viewState.maxTime - dur));
    }
    lastPipelineN = N;
    cpuPipelineCache = {
      dataVersion: sessionStore.dataVersion,
      sessionGeneration: sessionStore.sessionGeneration,
      referenceTime,
      N,
      cmdRelClk: cmdRelClk!,
      cmdLaneIdx: cmdLaneIdx!,
      cmdStrides,
      cmdTotalLanes,
      dataBus: dataBus!,
      dataStrides,
      dataTotalLanes,
      dataIdxByOrigIdx: dataIdxByOrigIdx!,
      dataNBL,
      rangeNBL,
      prefixRead: prefixRead!,
      prefixWrite: prefixWrite!,
      tpBinSize,
      tpBinCount,
      tpDataUtil: tpDataUtil!,
      tpCmdUtil: tpCmdUtil!,
      tpDataMax,
      tpCmdMax,
      reqData: reqData!,
      reqMaxStackPerLane: reqMaxStackPerLane!,
      reqSlotBase: reqSlotBase!,
      reqSlotCount,
    };
  }

  async function runPipeline() {
    if (pipelineBusy) {
      pipelinePending = true;
      return;
    }
    pipelineBusy = true;
    try {
    const el = canvas.value;
    if (!el) return;

    const { arrays, header, spec } = sessionStore;
    if (!arrays || !header || !spec) return;

    // ── Streaming refresh fast path ──────────────────────────────
    // Same session generation + GPU already initialized: we use the Web Worker
    // to compute the heavy CPU work asynchronously. The main thread is not blocked.
    if (sessionStore.sessionGeneration === seenSessionGeneration && regl) {
      const canvasEl = canvas.value;
      if (!canvasEl) return;
      const { arrays: a, header: h, spec: s } = sessionStore;
      if (!a || !h || !s) return;
      await updateStreamingData(canvasEl, a, h, s);
      return;
    }

    // ── Full (re)initialization ───────────────────────────────────
    if (sessionStore.sessionGeneration !== seenSessionGeneration) {
      if (cpuPipelineCache?.sessionGeneration !== sessionStore.sessionGeneration) {
        cpuPipelineCache = null;
      }
      lastPipelineN = 0;
      clearCursor();
      seenSessionGeneration = sessionStore.sessionGeneration;
    }

    const savedStart = viewState.start;
    const savedDuration = viewState.duration;

    if (regl) {
      await bumpLoadStatus('Cleaning up previous session…');
      abortCtrl?.abort();
      abortCtrl = null;
      resizeObs?.disconnect();
      resizeObs = null;
      regl.destroy();
      regl = null;
      _drawCmd = null;
      _drawData = null;
      _drawCmdHover = null;
      _drawDataHover = null;
      _drawReq = null;
      _drawReqHover = null;
      _drawGrid = null;
      _drawGlow = null;
      gridBuf = null;
      refreshCommandLookupTextures = null;
      refreshRequestLookupTextures = null;
      // Clear lifted buffer refs so updateStreamingData won't touch stale handles
      cmdRelClkBuf = null; cmdCmdIdBuf = null; cmdLaneIdxBuf = null;
      dataRelClkBuf = null; dataCmdIdBuf = null; dataLaneIdxBuf = null;
      reqRelClkBuf = null; reqDurationBuf = null; reqQueueDelayBuf = null;
      reqTypeIdBuf = null; reqLaneIdxBuf = null;
    }

    N = header.numEntries;
    stats.totalEvents = N;
    stats.eventCount = N;

    const cachedCpu = cpuPipelineCache?.dataVersion === sessionStore.dataVersion
      && cpuPipelineCache.sessionGeneration === sessionStore.sessionGeneration
      ? cpuPipelineCache
      : null;
    const skipProgressUi = lastPipelineN > 0 || cachedCpu !== null;

    if (cachedCpu) {
      referenceTime = cachedCpu.referenceTime;
      N = cachedCpu.N;
      stats.totalEvents = N;
      stats.eventCount = N;
      cmdRelClk = cachedCpu.cmdRelClk;
      cmdLaneIdx = cachedCpu.cmdLaneIdx;
      cmdStrides = cachedCpu.cmdStrides;
      cmdTotalLanes = cachedCpu.cmdTotalLanes;
      dataBus = cachedCpu.dataBus;
      dataStrides = cachedCpu.dataStrides;
      dataTotalLanes = cachedCpu.dataTotalLanes;
      dataIdxByOrigIdx = cachedCpu.dataIdxByOrigIdx;
      dataNBL = cachedCpu.dataNBL;
      rangeNBL = cachedCpu.rangeNBL;
      prefixRead = cachedCpu.prefixRead;
      prefixWrite = cachedCpu.prefixWrite;
      tpBinSize = cachedCpu.tpBinSize;
      tpBinCount = cachedCpu.tpBinCount;
      tpDataUtil = cachedCpu.tpDataUtil;
      tpCmdUtil = cachedCpu.tpCmdUtil;
      tpDataMax = cachedCpu.tpDataMax;
      tpCmdMax = cachedCpu.tpCmdMax;
      reqData = cachedCpu.reqData;
      reqMaxStackPerLane = cachedCpu.reqMaxStackPerLane;
      reqSlotBase = cachedCpu.reqSlotBase;
      reqSlotCount = cachedCpu.reqSlotCount;
      sessionStore.setRequestLaneStackDepth(Array.from(reqMaxStackPerLane));
      recomputeThroughputRangeStats(rangeSelection.value);
    } else {
      const structDepth = getStructuralDepth(spec);

      // Command and data buses share the same lane tiling (full hierarchy to Row / structural depth).
      const laneS = computeStrides(spec.levelSizes, structDepth);
      cmdStrides = laneS.strides;
      cmdTotalLanes = laneS.totalLanes;
      dataStrides = laneS.strides;
      dataTotalLanes = laneS.totalLanes;

      referenceTime = N > 0 ? Number(arrays.clk[0]) : 0;

      if (!skipProgressUi) await bumpLoadStatus('Computing command clocks and lanes…');
      // Command bus: all events, unshifted clocks, full-depth lane indices (synced with sidebar/data bus).
      cmdRelClk = buildRelativeClocks(arrays.clk, N, referenceTime);
      cmdLaneIdx = buildLaneIndices(arrays.addr, cmdStrides, structDepth, N);

      if (!skipProgressUi) await bumpLoadStatus('Building data bus…');
      // Data bus arrays: only accessing events, clocks offset by nCL/nCWL
      dataNBL = getTimingValue(spec, 'nBL', 4);
      dataBus = buildDataBusArrays(
        arrays, spec, dataStrides, structDepth, N, referenceTime,
      );

      dataIdxByOrigIdx = new Int32Array(N);
      dataIdxByOrigIdx.fill(-1);
      if (dataBus) {
        for (let j = 0; j < dataBus.count; j++) {
          const o = dataBus.originalIndices[j]!;
          dataIdxByOrigIdx[o] = j;
        }
      }

      rangeNBL = dataNBL;
      prefixRead = new Uint32Array(N + 1);
      prefixWrite = new Uint32Array(N + 1);
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

      // Throughput-view bins: bin size = nBL * 32
      tpBinSize = dataNBL * 32;
      if (N > 0 && cmdRelClk) {
        const span = cmdRelClk[N - 1]! + 1;
        tpBinCount = Math.ceil(span / tpBinSize);
        const accessBin = new Uint32Array(tpBinCount);
        const totalBin = new Uint32Array(tpBinCount);
        for (let i = 0; i < N; i++) {
          const b = Math.min(tpBinCount - 1, Math.floor(cmdRelClk[i]! / tpBinSize));
          totalBin[b]!++;
          const cid = arrays.cmdId[i]!;
          if (spec.commandMeta[cid]?.isAccessing) accessBin[b]!++;
        }
        tpDataUtil = new Float32Array(tpBinCount);
        tpCmdUtil = new Float32Array(tpBinCount);
        tpDataMax = 0;
        tpCmdMax = 0;
        for (let b = 0; b < tpBinCount; b++) {
          const d = Math.min(1, (accessBin[b]! * dataNBL) / tpBinSize);
          const c = Math.min(1, totalBin[b]! / tpBinSize);
          tpDataUtil[b] = d;
          tpCmdUtil[b] = c;
          if (d > tpDataMax) tpDataMax = d;
          if (c > tpCmdMax) tpCmdMax = c;
        }
      } else {
        tpBinCount = 0;
        tpDataUtil = new Float32Array(0);
        tpCmdUtil = new Float32Array(0);
        tpDataMax = 0;
        tpCmdMax = 0;
      }
      recomputeThroughputRangeStats(rangeSelection.value);

      if (!skipProgressUi) await bumpLoadStatus('Grouping requests…');
      // Request view arrays: group commands by (arrive, structural lane)
      reqData = buildRequestArrays(
        arrays, dataStrides, structDepth, N, referenceTime, header.readLatency, dataTotalLanes,
      );
      reqMaxStackPerLane = reqData.maxStackPerLane;
      reqSlotBase = reqData.slotBase;
      reqSlotCount = reqData.reqSlotCount;
      sessionStore.setRequestLaneStackDepth(Array.from(reqData.maxStackPerLane));

      const defaultColors: Record<number, string> = {};
      for (let id = 0; id < spec.commandNames.length; id++) {
        if (sessionStore.commandColors[id] === undefined)
          defaultColors[id] = defaultCmdColor(spec.commandNames[id]!, spec.commandMeta[id]!);
      }
      if (Object.keys(defaultColors).length > 0)
        sessionStore.mergeDefaultCommandColors(defaultColors);

      cpuPipelineCache = {
        dataVersion: sessionStore.dataVersion,
        sessionGeneration: sessionStore.sessionGeneration,
        referenceTime,
        N,
        cmdRelClk: cmdRelClk!,
        cmdLaneIdx: cmdLaneIdx!,
        cmdStrides,
        cmdTotalLanes,
        dataBus: dataBus!,
        dataStrides,
        dataTotalLanes,
        dataIdxByOrigIdx: dataIdxByOrigIdx!,
        dataNBL,
        rangeNBL,
        prefixRead: prefixRead!,
        prefixWrite: prefixWrite!,
        tpBinSize,
        tpBinCount,
        tpDataUtil: tpDataUtil!,
        tpCmdUtil: tpCmdUtil!,
        tpDataMax,
        tpCmdMax,
        reqData: reqData!,
        reqMaxStackPerLane: reqMaxStackPerLane!,
        reqSlotBase: reqSlotBase!,
        reqSlotCount,
      };
    }

    if (!skipProgressUi) await bumpLoadStatus('Preparing textures…');
    const { data: cmdTexData } = buildCmdTextureData(spec, sessionStore.commandColors, 1, spec.commandCycles);
    const { data: dataTexData } = buildCmdTextureData(spec, sessionStore.commandColors, dataNBL);

    if (N > 0) {
      const lastCmdClk = cmdRelClk[N - 1]!;
      const lastDataClk = dataBus.count > 0 ? dataBus.relClk[dataBus.count - 1]! : 0;
      const span = Math.max(lastCmdClk + getMaxCommandDuration(spec), lastDataClk + dataNBL) + 1;
      viewState.minTime = referenceTime;
      viewState.maxTime = referenceTime + span;
      viewState.maxDuration = span;
      if (lastPipelineN === 0) {
        viewState.start = referenceTime;
        viewState.duration = Math.min(span, 500);
      } else if (followLiveRef.value) {
        const dur = Math.max(
          viewState.minDuration,
          Math.min(savedDuration, span, viewState.maxDuration),
        );
        viewState.duration = dur;
        const tailStart = referenceTime + span - dur;
        viewState.start = Math.max(
          viewState.minTime,
          Math.min(tailStart, viewState.maxTime - dur),
        );
      } else {
        const dur = Math.max(
          viewState.minDuration,
          Math.min(savedDuration, span, viewState.maxDuration),
        );
        viewState.duration = dur;
        viewState.start = Math.max(
          viewState.minTime,
          Math.min(savedStart, viewState.maxTime - dur),
        );
      }
      lastPipelineN = N;
    } else {
      viewState.minTime = referenceTime;
      viewState.maxTime = referenceTime + 1000;
      viewState.start = referenceTime;
      viewState.duration = 100;
      viewState.maxDuration = 1000;
    }

    const rect = el.getBoundingClientRect();
    el.width = rect.width;
    el.height = rect.height;
    const lcInit = labelCanvas?.value;
    if (lcInit) {
      lcInit.width = rect.width;
      lcInit.height = rect.height;
    }

    if (!skipProgressUi) await bumpLoadStatus('Initializing WebGL…');
    try {
      regl = createREGL({
        canvas: el,
        attributes: { antialias: false, alpha: false },
        extensions: ['angle_instanced_arrays', 'oes_texture_float'],
      });
    } catch (e) {
      console.error('Failed to initialize regl:', e);
      traceLoadStatus.value = null;
      return;
    }

    if (!skipProgressUi) await bumpLoadStatus('Creating GPU buffers…');
    // ── GPU resources ───────────────────────────────────────────

    const quad: [number, number][] = [
      [0, 0], [1, 0], [0, 1],
      [0, 1], [1, 0], [1, 1],
    ];

    // Command bus GPU
    cmdRelClkBuf = regl.buffer({ data: cmdRelClk, type: 'float' });
    cmdCmdIdBuf = regl.buffer({ data: arrays.cmdId, type: 'uint8' });
    cmdLaneIdxBuf = regl.buffer({ data: cmdLaneIdx, type: 'uint16' });

    cmdLookupTex = regl.texture({
      width: 256, height: 1, data: cmdTexData,
      format: 'rgba', type: 'float', min: 'nearest', mag: 'nearest',
    });

    const cmdLaneTexSize = Math.max(cmdTotalLanes, 1);
    cmdSwimlaneTex = regl.texture({
      width: cmdLaneTexSize, height: 1,
      data: new Float32Array(cmdLaneTexSize * 4),
      format: 'rgba', type: 'float', min: 'nearest', mag: 'nearest',
    });

    // Data bus GPU
    dataRelClkBuf = regl.buffer({ data: dataBus.relClk, type: 'float' });
    dataCmdIdBuf = regl.buffer({ data: dataBus.cmdId, type: 'uint8' });
    dataLaneIdxBuf = regl.buffer({ data: dataBus.laneIdx, type: 'uint16' });

    dataLookupTex = regl.texture({
      width: 256, height: 1, data: dataTexData,
      format: 'rgba', type: 'float', min: 'nearest', mag: 'nearest',
    });

    refreshCommandLookupTextures = () => {
      const s = sessionStore.spec;
      if (!s || !cmdLookupTex || !dataLookupTex) return;
      const { data: cmdD } = buildCmdTextureData(s, sessionStore.commandColors, 1, s.commandCycles);
      const { data: dataD } = buildCmdTextureData(s, sessionStore.commandColors, dataNBL);
      (cmdLookupTex as any)({
        width: 256, height: 1, data: cmdD,
        format: 'rgba', type: 'float', min: 'nearest', mag: 'nearest',
      });
      (dataLookupTex as any)({
        width: 256, height: 1, data: dataD,
        format: 'rgba', type: 'float', min: 'nearest', mag: 'nearest',
      });
    };

    const dataLaneTexSize = Math.max(dataTotalLanes, 1);
    dataSwimlaneTex = regl.texture({
      width: dataLaneTexSize, height: 1,
      data: new Float32Array(dataLaneTexSize * 4),
      format: 'rgba', type: 'float', min: 'nearest', mag: 'nearest',
    });

    // Request view GPU
    reqRelClkBuf = regl.buffer({ data: reqData.relClk, type: 'float' });
    reqDurationBuf = regl.buffer({ data: reqData.duration, type: 'float' });
    reqQueueDelayBuf = regl.buffer({ data: reqData.queueDelay, type: 'float' });
    reqTypeIdBuf = regl.buffer({ data: reqData.typeId, type: 'uint8' });
    reqLaneIdxBuf = regl.buffer({ data: reqData.slotId, type: 'uint16' });

    reqLookupTex = regl.texture({
      width: 256, height: 1, data: buildReqLookupTexture(sessionStore.requestTypeColors),
      format: 'rgba', type: 'float', min: 'nearest', mag: 'nearest',
    });

    refreshRequestLookupTextures = () => {
      if (!reqLookupTex) return;
      const data = buildReqLookupTexture(sessionStore.requestTypeColors);
      (reqLookupTex as any)({
        width: 256, height: 1, data,
        format: 'rgba', type: 'float', min: 'nearest', mag: 'nearest',
      });
    };

    const reqLaneTexSize = Math.max(reqData.reqSlotCount, 1);
    reqSwimlaneTex = regl.texture({
      width: reqLaneTexSize, height: 1,
      data: new Float32Array(reqLaneTexSize * 4),
      format: 'rgba', type: 'float', min: 'nearest', mag: 'nearest',
    });

    gridBuf = regl.buffer({
      length: 2048 * 4,
      type: 'float',
      usage: 'dynamic',
    });

    // ── Draw commands ───────────────────────────────────────────

    interface TraceProps {
      viewRange: [number, number];
      viewBase: number;
      offset: number;
      instances: number;
    }

    _drawCmd = regl({
      vert: TRACE_VERT,
      frag: TRACE_FRAG,
      attributes: {
        a_quad: quad,
        a_relClk: {
          buffer: cmdRelClkBuf, divisor: 1,
          offset: ((_: any, p: TraceProps) => p.offset * 4) as any,
        },
        a_cmdId: {
          buffer: cmdCmdIdBuf, divisor: 1, normalized: false,
          offset: ((_: any, p: TraceProps) => p.offset) as any,
        },
        a_laneIdx: {
          buffer: cmdLaneIdxBuf, divisor: 1, normalized: false,
          offset: ((_: any, p: TraceProps) => p.offset * 2) as any,
        },
      },
      uniforms: {
        u_cmdLookup: () => cmdLookupTex!,
        u_lanes: () => cmdSwimlaneTex!,
        u_viewRange: regl.prop<TraceProps, 'viewRange'>('viewRange'),
        u_viewBase: regl.prop<TraceProps, 'viewBase'>('viewBase'),
        u_resolution: (ctx: any) => [ctx.viewportWidth, ctx.viewportHeight],
        u_rowHeight: ROW_HEIGHT,
        u_laneCount: cmdLaneTexSize,
      },
      depth: { enable: false },
      instances: ((_: any, p: TraceProps) => p.instances) as any,
      count: 6,
    });

    _drawData = regl({
      vert: TRACE_VERT,
      frag: TRACE_FRAG,
      attributes: {
        a_quad: quad,
        a_relClk: {
          buffer: dataRelClkBuf, divisor: 1,
          offset: ((_: any, p: TraceProps) => p.offset * 4) as any,
        },
        a_cmdId: {
          buffer: dataCmdIdBuf, divisor: 1, normalized: false,
          offset: ((_: any, p: TraceProps) => p.offset) as any,
        },
        a_laneIdx: {
          buffer: dataLaneIdxBuf, divisor: 1, normalized: false,
          offset: ((_: any, p: TraceProps) => p.offset * 2) as any,
        },
      },
      uniforms: {
        u_cmdLookup: () => dataLookupTex!,
        u_lanes: () => dataSwimlaneTex!,
        u_viewRange: regl.prop<TraceProps, 'viewRange'>('viewRange'),
        u_viewBase: regl.prop<TraceProps, 'viewBase'>('viewBase'),
        u_resolution: (ctx: any) => [ctx.viewportWidth, ctx.viewportHeight],
        u_rowHeight: ROW_HEIGHT,
        u_laneCount: dataLaneTexSize,
      },
      depth: { enable: false },
      instances: ((_: any, p: TraceProps) => p.instances) as any,
      count: 6,
    });

    const traceHoverBlend = {
      enable: true,
      func: {
        srcRGB: 'src alpha',
        srcAlpha: 'one',
        dstRGB: 'one minus src alpha',
        dstAlpha: 'one minus src alpha',
      },
    } as const;

    _drawCmdHover = regl({
      vert: TRACE_HOVER_VERT,
      frag: REQ_HOVER_FRAG,
      attributes: {
        a_quad: quad,
        a_relClk: {
          buffer: cmdRelClkBuf, divisor: 1,
          offset: ((_: any, p: TraceProps) => p.offset * 4) as any,
        },
        a_cmdId: {
          buffer: cmdCmdIdBuf, divisor: 1, normalized: false,
          offset: ((_: any, p: TraceProps) => p.offset) as any,
        },
        a_laneIdx: {
          buffer: cmdLaneIdxBuf, divisor: 1, normalized: false,
          offset: ((_: any, p: TraceProps) => p.offset * 2) as any,
        },
      },
      uniforms: {
        u_cmdLookup: () => cmdLookupTex!,
        u_lanes: () => cmdSwimlaneTex!,
        u_viewRange: regl.prop<TraceProps, 'viewRange'>('viewRange'),
        u_viewBase: regl.prop<TraceProps, 'viewBase'>('viewBase'),
        u_resolution: (ctx: any) => [ctx.viewportWidth, ctx.viewportHeight],
        u_rowHeight: ROW_HEIGHT,
        u_laneCount: cmdLaneTexSize,
      },
      blend: traceHoverBlend,
      depth: { enable: false },
      instances: ((_: any, p: TraceProps) => p.instances) as any,
      count: 6,
    });

    _drawDataHover = regl({
      vert: TRACE_HOVER_VERT,
      frag: REQ_HOVER_FRAG,
      attributes: {
        a_quad: quad,
        a_relClk: {
          buffer: dataRelClkBuf, divisor: 1,
          offset: ((_: any, p: TraceProps) => p.offset * 4) as any,
        },
        a_cmdId: {
          buffer: dataCmdIdBuf, divisor: 1, normalized: false,
          offset: ((_: any, p: TraceProps) => p.offset) as any,
        },
        a_laneIdx: {
          buffer: dataLaneIdxBuf, divisor: 1, normalized: false,
          offset: ((_: any, p: TraceProps) => p.offset * 2) as any,
        },
      },
      uniforms: {
        u_cmdLookup: () => dataLookupTex!,
        u_lanes: () => dataSwimlaneTex!,
        u_viewRange: regl.prop<TraceProps, 'viewRange'>('viewRange'),
        u_viewBase: regl.prop<TraceProps, 'viewBase'>('viewBase'),
        u_resolution: (ctx: any) => [ctx.viewportWidth, ctx.viewportHeight],
        u_rowHeight: ROW_HEIGHT,
        u_laneCount: dataLaneTexSize,
      },
      blend: traceHoverBlend,
      depth: { enable: false },
      instances: ((_: any, p: TraceProps) => p.instances) as any,
      count: 6,
    });

    _drawReq = regl({
      vert: REQ_VERT,
      frag: REQ_FRAG,
      attributes: {
        a_quad: quad,
        a_relClk: {
          buffer: reqRelClkBuf, divisor: 1,
          offset: ((_: any, p: TraceProps) => p.offset * 4) as any,
        },
        a_cmdId: {
          buffer: reqTypeIdBuf, divisor: 1, normalized: false,
          offset: ((_: any, p: TraceProps) => p.offset) as any,
        },
        a_laneIdx: {
          buffer: reqLaneIdxBuf, divisor: 1, normalized: false,
          offset: ((_: any, p: TraceProps) => p.offset * 2) as any,
        },
        a_duration: {
          buffer: reqDurationBuf, divisor: 1,
          offset: ((_: any, p: TraceProps) => p.offset * 4) as any,
        },
        a_queueDelay: {
          buffer: reqQueueDelayBuf, divisor: 1,
          offset: ((_: any, p: TraceProps) => p.offset * 4) as any,
        },
      },
      uniforms: {
        u_cmdLookup: () => reqLookupTex!,
        u_lanes: () => reqSwimlaneTex!,
        u_viewRange: regl.prop<TraceProps, 'viewRange'>('viewRange'),
        u_viewBase: regl.prop<TraceProps, 'viewBase'>('viewBase'),
        u_resolution: (ctx: any) => [ctx.viewportWidth, ctx.viewportHeight],
        u_rowHeight: ROW_HEIGHT,
        u_laneCount: () => Math.max(reqSlotCount, 1),
      },
      blend: {
        enable: true,
        func: {
          srcRGB: 'src alpha',
          srcAlpha: 'one',
          dstRGB: 'one minus src alpha',
          dstAlpha: 'one minus src alpha',
        },
      },
      depth: { enable: false },
      instances: ((_: any, p: TraceProps) => p.instances) as any,
      count: 6,
    });

    _drawReqHover = regl({
      vert: REQ_VERT,
      frag: REQ_HOVER_FRAG,
      attributes: {
        a_quad: quad,
        a_relClk: {
          buffer: reqRelClkBuf, divisor: 1,
          offset: ((_: any, p: TraceProps) => p.offset * 4) as any,
        },
        a_cmdId: {
          buffer: reqTypeIdBuf, divisor: 1, normalized: false,
          offset: ((_: any, p: TraceProps) => p.offset) as any,
        },
        a_laneIdx: {
          buffer: reqLaneIdxBuf, divisor: 1, normalized: false,
          offset: ((_: any, p: TraceProps) => p.offset * 2) as any,
        },
        a_duration: {
          buffer: reqDurationBuf, divisor: 1,
          offset: ((_: any, p: TraceProps) => p.offset * 4) as any,
        },
        a_queueDelay: {
          buffer: reqQueueDelayBuf, divisor: 1,
          offset: ((_: any, p: TraceProps) => p.offset * 4) as any,
        },
      },
      uniforms: {
        u_cmdLookup: () => reqLookupTex!,
        u_lanes: () => reqSwimlaneTex!,
        u_viewRange: regl.prop<TraceProps, 'viewRange'>('viewRange'),
        u_viewBase: regl.prop<TraceProps, 'viewBase'>('viewBase'),
        u_resolution: (ctx: any) => [ctx.viewportWidth, ctx.viewportHeight],
        u_rowHeight: ROW_HEIGHT,
        u_laneCount: () => Math.max(reqSlotCount, 1),
      },
      blend: {
        enable: true,
        func: {
          srcRGB: 'src alpha',
          srcAlpha: 'one',
          dstRGB: 'one minus src alpha',
          dstAlpha: 'one minus src alpha',
        },
      },
      depth: { enable: false },
      instances: ((_: any, p: TraceProps) => p.instances) as any,
      count: 6,
    });

    interface GridProps { count: number }

    _drawGrid = regl({
      vert: GRID_VERT,
      frag: GRID_FRAG,
      attributes: {
        a_quad: quad,
        a_yScreen: { buffer: gridBuf, divisor: 1 },
      },
      uniforms: {
        u_resolutionY: (ctx: any) => ctx.viewportHeight,
      },
      depth: { enable: false },
      instances: ((_: any, p: GridProps) => p.count) as any,
      count: 6,
    });

    interface GlowProps {
      viewRange: [number, number];
      viewBase: number;
      targetStart: number;
      targetDuration: number;
      targetY: number;
      opacity: number;
      color: [number, number, number];
    }

    _drawGlow = regl({
      vert: GLOW_VERT,
      frag: GLOW_FRAG,
      attributes: { a_quad: quad },
      uniforms: {
        u_viewRange: ((_: any, p: GlowProps) => p.viewRange) as any,
        u_viewBase: ((_: any, p: GlowProps) => p.viewBase) as any,
        u_resolution: (ctx: any) => [ctx.viewportWidth, ctx.viewportHeight],
        u_rowHeight: ROW_HEIGHT,
        u_targetStart: ((_: any, p: GlowProps) => p.targetStart) as any,
        u_targetDuration: ((_: any, p: GlowProps) => p.targetDuration) as any,
        u_targetY: ((_: any, p: GlowProps) => p.targetY) as any,
        u_opacity: ((_: any, p: GlowProps) => p.opacity) as any,
        u_color: ((_: any, p: GlowProps) => p.color) as any,
      },
      blend: {
        enable: true,
        func: { srcRGB: 'src alpha', dstRGB: 'one', srcAlpha: 'one', dstAlpha: 'one' },
      },
      depth: { enable: false },
      count: 6,
    });

    // ── Frame loop ──────────────────────────────────────────────

    let frameCount = 0;
    let lastFpsTime = performance.now();

    traceLoadStatus.value = null;
    regl.frame(() => {
      regl!.clear({ color: BG_COLOR, depth: 1 });

      if (uiStore.layoutVersion !== lastLayoutVersion) {
        lastLayoutVersion = uiStore.layoutVersion;
        refreshSwimlanes();
        refreshGrid();
      }

      const relStart = viewState.start - referenceTime;
      const relEnd = relStart + viewState.duration;
      const viewBase = Math.floor(relStart);
      const vr: [number, number] = [relStart - viewBase, relEnd - viewBase];

      if (uiStore.viewMode === 'command') {
        // Command bus pass
        if (cmdBusVisible && N > 0 && cmdRelClk && _drawCmd) {
          const startIdx = Math.max(0, bisectLeft(cmdRelClk, relStart - 1, N));
          const endIdx = Math.min(N, bisectRight(cmdRelClk, relEnd, N));
          const count = endIdx - startIdx;
          if (count > 0) {
            _drawCmd({ viewRange: vr, viewBase, offset: startIdx, instances: count });
          }
        }

        // Data bus pass
        if (dataBusVisible && dataBus && dataBus.count > 0 && _drawData) {
          const M = dataBus.count;
          const startIdx = Math.max(0, bisectLeft(dataBus.relClk, relStart - dataNBL, M));
          const endIdx = Math.min(M, bisectRight(dataBus.relClk, relEnd, M));
          const count = endIdx - startIdx;
          if (count > 0) {
            _drawData({ viewRange: vr, viewBase, offset: startIdx, instances: count });
          }
        }
      } else if (uiStore.viewMode === 'request' && reqBusVisible && reqData && reqData.count > 0 && _drawReq) {
        // Request view pass
        const R = reqData.count;
        const startIdx = Math.max(0, bisectLeft(reqData.relClk, relStart - reqData.maxDuration, R));
        const endIdx = Math.min(R, bisectRight(reqData.relClk, relEnd, R));
        const count = endIdx - startIdx;
        if (count > 0) {
          _drawReq({ viewRange: vr, viewBase, offset: startIdx, instances: count });
        }
      }

      if (gridLineCount > 0 && _drawGrid) {
        _drawGrid({ count: gridLineCount });
      }

      if (uiStore.viewMode === 'command') {
        const hov = hoveredEvent.value;
        if (
          dataIdxByOrigIdx && hov && (hov.bus === 'command' || hov.bus === 'data')
        ) {
          const i = Number(hov.index);
          if (i >= 0 && i < N) {
            if (cmdBusVisible && _drawCmdHover) {
              _drawCmdHover({ viewRange: vr, viewBase, offset: i, instances: 1 });
            }
            const dj = dataIdxByOrigIdx[i]!;
            if (dj >= 0 && dataBusVisible && dataBus && _drawDataHover) {
              _drawDataHover({ viewRange: vr, viewBase, offset: dj, instances: 1 });
            }
          }
        }
      }

      if (
        uiStore.viewMode === 'request' && reqBusVisible && reqData && reqData.count > 0
        && _drawReqHover
      ) {
        const R = reqData.count;
        const hov = hoveredEvent.value;
        if (hov?.bus === 'request' && hov.requestGroupIndex != null) {
          const j = hov.requestGroupIndex;
          if (j >= 0 && j < R) {
            _drawReqHover({ viewRange: vr, viewBase, offset: j, instances: 1 });
          }
        }
      }

      if (glowTarget && _drawGlow) {
        const elapsed = performance.now() - glowTarget.startedAt;
        if (elapsed < GLOW_FADE_MS) {
          const t = elapsed / GLOW_FADE_MS;
          const opacity = (1 - t * t) * 0.7;
          const swimData = uiStore.viewMode === 'request' ? reqSwimlaneData : cmdSwimlaneData;
          if (swimData) {
            const yCenter = swimData[glowTarget.laneIdx * 4];
            if (yCenter !== undefined) {
              _drawGlow({
                viewRange: vr,
                viewBase,
                targetStart: glowTarget.relClk,
                targetDuration: glowTarget.duration,
                targetY: yCenter,
                opacity,
                color: glowTarget.color,
              });
            }
          }
        } else {
          glowTarget = null;
        }
      }

      drawOverlayLabels();

      frameCount++;
      const now = performance.now();
      if (now - lastFpsTime >= 1000) {
        stats.fps = Math.round((frameCount * 1000) / (now - lastFpsTime));
        frameCount = 0;
        lastFpsTime = now;
      }
    });

    // ── Interaction ─────────────────────────────────────────────

    abortCtrl = new AbortController();
    const sig = abortCtrl.signal;

    let dragging = false;
    let dragMoved = false;
    let dragStartX = 0;
    let dragStartTime = 0;

    let rangeSelecting = false;
    let rangeAnchorTime = 0;

    el.addEventListener(
      'wheel',
      (e) => {
        e.preventDefault();
        const r = el.getBoundingClientRect();
        const frac = (e.clientX - r.left) / r.width;
        const anchor = viewState.start + frac * viewState.duration;

        const factor = e.deltaY > 0 ? 1.15 : 1 / 1.15;
        let dur = viewState.duration * factor;
        dur = Math.max(viewState.minDuration, Math.min(dur, viewState.maxDuration));

        let s = anchor - frac * dur;
        s = Math.max(viewState.minTime, Math.min(s, viewState.maxTime - dur));

        viewState.start = s;
        viewState.duration = dur;
      },
      { passive: false, signal: sig },
    );

    el.addEventListener(
      'mousedown',
      (e) => {
        if (e.button !== 0) return;

        if (cursorMode.value && !e.shiftKey && !e.ctrlKey) {
          dragMoved = false;
          dragStartX = e.clientX;
          return;
        }

        if (e.shiftKey || e.ctrlKey) {
          rangeSelecting = true;
          dragMoved = false;
          dragStartX = e.clientX;
          const r = el.getBoundingClientRect();
          const frac = (e.clientX - r.left) / r.width;
          rangeAnchorTime = Math.max(
            viewState.minTime,
            Math.min(viewState.maxTime, viewState.start + frac * viewState.duration),
          );
          rangeSelection.value = { startTime: rangeAnchorTime, endTime: rangeAnchorTime };
          rangeStats.value = null;
          throughputRangeStats.value = null;
          return;
        }

        rangeSelection.value = null;
        rangeStats.value = null;
        throughputRangeStats.value = null;
        dragging = true;
        dragMoved = false;
        dragStartX = e.clientX;
        dragStartTime = viewState.start;
      },
      { signal: sig },
    );

    window.addEventListener(
      'mousemove',
      (e) => {
        if (rangeSelecting) {
          const r = el.getBoundingClientRect();
          const frac = (e.clientX - r.left) / r.width;
          const t = Math.max(
            viewState.minTime,
            Math.min(viewState.maxTime, viewState.start + frac * viewState.duration),
          );
          rangeSelection.value = {
            startTime: Math.min(rangeAnchorTime, t),
            endTime: Math.max(rangeAnchorTime, t),
          };
          if (Math.abs(e.clientX - dragStartX) > 3) dragMoved = true;
          recomputeRangeStats();
          return;
        }
        if (!dragging) return;
        if (Math.abs(e.clientX - dragStartX) > 3) dragMoved = true;
        const r = el.getBoundingClientRect();
        const dx = e.clientX - dragStartX;
        let s = dragStartTime - (dx / r.width) * viewState.duration;
        s = Math.max(
          viewState.minTime,
          Math.min(s, viewState.maxTime - viewState.duration),
        );
        viewState.start = s;
      },
      { signal: sig },
    );

    window.addEventListener(
      'mouseup',
      (e: MouseEvent) => {
        if (rangeSelecting) {
          rangeSelecting = false;
          const sel = rangeSelection.value;
          if (!sel || sel.endTime - sel.startTime < 1) {
            rangeSelection.value = null;
            rangeStats.value = null;
            throughputRangeStats.value = null;
          } else {
            recomputeRangeStats();
            if (e.ctrlKey) {
              const dur = Math.max(viewState.minDuration, Math.min(sel.endTime - sel.startTime, viewState.maxDuration));
              viewState.duration = dur;
              viewState.start = Math.max(viewState.minTime, Math.min(sel.startTime, viewState.maxTime - dur));
            }
          }
          return;
        }
        dragging = false;
      },
      { signal: sig },
    );

    el.addEventListener(
      'mousemove',
      (e) => {
        mouseX.value = e.clientX;
        mouseY.value = e.clientY;
        if (!rangeSelecting) {
          hoveredEvent.value = hitTest(e.clientX, e.clientY);
          throughputHover.value = uiStore.viewMode === 'throughput'
            ? hitTestThroughput(e.clientX, e.clientY)
            : null;
        }
      },
      { signal: sig },
    );

    el.addEventListener(
      'mouseleave',
      () => {
        hoveredEvent.value = null;
        throughputHover.value = null;
      },
      { signal: sig },
    );

    el.addEventListener(
      'click',
      (e) => {
        if (e.button !== 0 || dragMoved) return;
        if (e.shiftKey || e.ctrlKey) return;

        if (cursorMode.value) {
          placeCursorAtClientX(e.clientX);
          hoveredEvent.value = null;
          throughputHover.value = null;
          return;
        }

        const hit = hitTest(e.clientX, e.clientY);
        if (hit) {
          navigateToCounterpart(hit);
          hoveredEvent.value = null;
        }
      },
      { signal: sig },
    );

    // ── Resize ──────────────────────────────────────────────────

    resizeObs = new ResizeObserver(() => {
      const r = el.getBoundingClientRect();
      el.width = r.width;
      el.height = r.height;
      const lc = labelCanvas?.value;
      if (lc) {
        lc.width = r.width;
        lc.height = r.height;
      }
      regl?.poll();
    });
    resizeObs.observe(el);
    } finally {
      pipelineBusy = false;
      if (pipelinePending) {
        pipelinePending = false;
        void runPipeline();
      }
    }
  }

  onMounted(() => {
    void runPipeline();
  });

  watch(
    () => sessionStore.dataVersion,
    () => { void runPipeline(); },
  );

  onUnmounted(() => {
    traceLoadStatus.value = null;
    abortCtrl?.abort();
    resizeObs?.disconnect();
    regl?.destroy();
    regl = null;
    lastPipelineN = 0;
    seenSessionGeneration = -1;
    worker?.terminate();
    worker = null;
  });

  watch(
    () => sessionStore.commandColors,
    () => {
      refreshCommandLookupTextures?.();
    },
    { deep: true },
  );

  watch(
    () => sessionStore.requestTypeColors,
    () => {
      refreshRequestLookupTextures?.();
    },
    { deep: true },
  );

  watch(
    () => uiStore.viewMode,
    () => {
      hoveredEvent.value = null;
      throughputHover.value = null;
      throughputRangeStats.value = null;
    },
  );

  // Instant follow-live snap: when the user enables "follow", immediately
  // jump the viewport to the tail without waiting for the next data batch.
  if (followLive) {
    watch(followLive, (newFollow) => {
      if (!newFollow || N === 0) return;
      const span = viewState.maxTime - viewState.minTime;
      if (span <= 0) return;
      const dur = Math.max(
        viewState.minDuration,
        Math.min(viewState.duration, span, viewState.maxDuration),
      );
      viewState.duration = dur;
      const tailStart = viewState.maxTime - dur;
      viewState.start = Math.max(viewState.minTime, Math.min(tailStart, viewState.maxTime - dur));
    });
  }

  return {
    viewState,
    stats,
    hoveredEvent,
    throughputHover,
    mouseX,
    mouseY,
    rangeSelection,
    rangeStats,
    throughputRangeStats,
    cursorMode,
    cursorTime,
    setCursorMode,
    clearCursor,
    jumpCursor,
  };
}
