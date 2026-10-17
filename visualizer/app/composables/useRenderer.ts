// One WebGL renderer per docked panel. Each instance draws a single view; the time axis,
// cursor, range and derived data come from the page's shared timeline (useTimeline).
import createREGL from 'regl';
import type { TraceSpec } from '~/composables/useTrace';
import type { RowLayout, BusType, ViewMode } from '~/stores/ui';
import { nodeId, computeDefaultExpanded, structuralDepth as specStructuralDepth } from '~/utils/traceTree';
import { CMD_PALETTE, defaultCmdColor, hex2rgb, isWriteCommand } from '~/utils/commandColors';
import type { RequestTypeStoredColors } from '~/utils/commandColorStorage';
import {
  BANK_COLORS,
  bankAccumulate,
  bankCoordsName,
  bankCoordsRange,
  bankCoverage,
  bankDomOf,
  bankEmptyStats,
  bankIntervalOrGap,
  bankKindColor,
  bankKindName,
  bankLaneLabel,
  bankLanePath,
  bankStateAt,
  type BankStateData,
  type BankWindowStats,
} from '~/utils/bankState';
import { bankGeometry, bankTitle, createBankLayer, type BankLayer } from '~/composables/bankLayer';
import {
  bisectLeft,
  bisectRight,
  commandDuration,
  maxCommandDuration,
  specTiming,
  type DataBusArrays,
  type RequestArrays,
} from '~/utils/tracePipeline';
import type {
  BankHover,
  BankShares,
  HitResult,
  LinkAnchor,
  ThroughputHover,
  TraceModel,
} from '~/composables/useTimeline';
import { PAPER } from '~/utils/theme';

// ── Constants ───────────────────────────────────────────────────────

const ROW_HEIGHT = 16;
/** Request bars: height, gap between stacked slots, slots shown per bank (canvas + sidebar must agree). */
const REQ_BAR_HEIGHT = 10;
const REQ_STACK_GAP = 2;
const REQ_MAX_STACKS = 8;
/** Space above the first request slot in a bank row. */
const REQ_ROW_PAD = 3;
/** When horizontal scale is at least this many pixels per cycle, draw command names on the command bus. */
const CMD_LABEL_MIN_PX_PER_CYCLE = 28;
/** Minimum bar width in pixels before drawing a label on the data bus. */
const LABEL_MIN_BAR_WIDTH_PX = 28;
/** Avoid drawing too many text calls in one frame when the window is very dense. */
const CMD_LABEL_MAX_VISIBLE = 600;
const BG_COLOR = PAPER.bg;
/** Trailing throttle for the bank-view window statistics. */
const BANK_STATS_THROTTLE_MS = 100;
/** Row separators the grid buffer can hold. */
const GRID_MAX_LINES = 2048;

/** Vertical gridline color for major ticks (subtle, semi-transparent). */
const VGRID_COLOR = PAPER.vgrid;
/** Fill color for the selected range highlight on the main canvas. */
const RANGE_FILL = PAPER.rangeFill;
/** Edge color for the selected range boundary lines. */
const RANGE_EDGE = PAPER.rangeEdge;
/** Cursor marker color, shared by the main overlay and minimap. */
const CURSOR_COLOR = PAPER.cursor;
const CURSOR_FILL = PAPER.cursorFill;
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
/** >= 0: draw every instance on this row (CMD BUS summary lane) instead of its own lane. */
uniform float u_forceY;

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
  float yCenter = u_forceY >= 0.0 ? u_forceY : texture2D(u_lanes, vec2(laneUv, 0.5)).r;
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
  gl_FragColor = vec4(0.922, 0.929, 0.937, 1.0);
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
  float a = max(border, tint * 0.45);
  gl_FragColor = vec4(0.122, 0.137, 0.157, a);
}
`;


// ── Pure helpers ────────────────────────────────────────────────────

function bankShares(acc: BankWindowStats): BankShares {
  const tot = acc.banks * (acc.t1 - acc.t0);
  const p = (x: number) => (tot > 0 ? (x / tot) * 100 : 0);
  return {
    starved: p(acc.starved),
    bus: p(acc.bus),
    bg: p(acc.bg),
    act: p(acc.act),
    pre: p(acc.pre),
    idle: p(acc.idle),
    wrrec: p(acc.wrrec),
    ref: p(acc.ref),
  };
}

function buildCmdTextureData(
  spec: TraceSpec,
  userColors: Record<number, string>,
  uniformDuration: number,
  commandDurations?: Uint8Array | null,
): Float32Array {
  const MAX = 256;
  const data = new Float32Array(MAX * 4);

  const [fr, fg, fb] = hex2rgb(CMD_PALETTE.fallback);
  for (let i = 0; i < MAX; i++) {
    data[i * 4 + 0] = fr;
    data[i * 4 + 1] = fg;
    data[i * 4 + 2] = fb;
    data[i * 4 + 3] = uniformDuration;
  }

  for (let id = 0; id < spec.commandNames.length; id++) {
    const meta = spec.commandMeta[id]!;
    data[id * 4 + 3] = commandDurations?.[id] ?? uniformDuration;
    const hex = userColors[id] ?? defaultCmdColor(spec.commandNames[id]!, meta);
    const [r, g, b] = hex2rgb(hex);
    data[id * 4 + 0] = r;
    data[id * 4 + 1] = g;
    data[id * 4 + 2] = b;
  }

  return data;
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

    // A collapsed group row overlays all of its lanes' requests within its own height.
    const leaf = row.depth === strides.length;
    for (let j = 0; j < range; j++) {
      const laneFlat = base + j;
      const stacks = maxStackPerLane[laneFlat] ?? 1;
      const sb = slotBase[laneFlat]!;
      const rowTop = row.top - canvasTop;
      const pitch = REQ_BAR_HEIGHT + REQ_STACK_GAP;
      for (let s = 0; s < stacks; s++) {
        // Overlap deeper than REQ_MAX_STACKS shares the last slot.
        const slot = Math.min(s, REQ_MAX_STACKS - 1);
        const yCenter = leaf ? rowTop + REQ_ROW_PAD + slot * pitch + REQ_BAR_HEIGHT * 0.5 : rowTop + row.height * 0.5;
        data[(sb + s) * 4] = yCenter;
      }
    }
  }

  return data;
}

function setLookupTexture(tex: createREGL.Texture2D, width: number, data: Float32Array) {
  (tex as any)({ width: Math.max(width, 1), height: 1, data, format: 'rgba', type: 'float', min: 'nearest', mag: 'nearest' });
}

// ── Composable ──────────────────────────────────────────────────────

/**
 * Draw `view` on `canvas` (WebGL) and `labelCanvas` (2D overlay). Lanes follow the panel's
 * sidebar rows (`uiStore.panels[view].rowLayout`); zoom, pan, range and cursor are shared.
 */
export function useRenderer(
  canvas: Ref<HTMLCanvasElement | null>,
  labelCanvas: Ref<HTMLCanvasElement | null>,
  view: ViewMode,
) {
  const sessionStore = useSessionStore();
  const uiStore = useUIStore();
  const tl = useTimeline();
  const { viewState, rangeSelection, cursorMode, cursorTime } = tl;
  const panel = uiStore.panels[view];

  /** Bar hovered in this panel (Commands / Requests); the other views keep theirs in the timeline. */
  const hoveredEvent = view === 'request'
    ? tl.hoveredRequest
    : view === 'command' ? tl.hoveredCommand : shallowRef<HitResult | null>(null);

  let regl: createREGL.Regl | null = null;
  let resizeObs: ResizeObserver | null = null;
  let abortCtrl: AbortController | null = null;

  // Canvases are sized in device pixels for crisp HiDPI output; all drawing and hit-test math
  // stays in CSS pixels (cssW / cssH), and shaders receive the CSS resolution.
  let dpr = 1;
  let cssW = 1;
  let cssH = 1;
  function sizeCanvases(el: HTMLCanvasElement) {
    const r = el.getBoundingClientRect();
    dpr = window.devicePixelRatio || 1;
    cssW = Math.max(1, r.width);
    cssH = Math.max(1, r.height);
    el.width = Math.max(1, Math.round(cssW * dpr));
    el.height = Math.max(1, Math.round(cssH * dpr));
    const lc = labelCanvas.value;
    if (lc) {
      lc.width = el.width;
      lc.height = el.height;
    }
  }

  // ── Shared derived data (adopted from tl.model) ────────────────
  let model: TraceModel | null = null;
  let gpuGeneration = -1;
  let referenceTime = 0;
  let N = 0;
  let cmdRelClk: Float32Array | null = null;
  let cmdLaneIdx: Uint16Array | null = null;
  let strides: number[] = [];
  let totalLanes = 1;
  let dataBus: DataBusArrays | null = null;
  let dataNBL = 4;
  let dataIdxByOrigIdx: Int32Array | null = null;
  let reqData: RequestArrays | null = null;
  let reqSlotCount = 0;
  let tpBinSize = 1;
  let tpBinCount = 0;
  let tpDataUtil: Float32Array | null = null;
  let tpCmdUtil: Float32Array | null = null;
  let tpDataMax = 0;
  let tpCmdMax = 0;
  let tpLaneAccess: Uint16Array | null = null;

  function adoptModel(m: TraceModel) {
    model = m;
    referenceTime = m.referenceTime;
    N = m.N;
    cmdRelClk = m.cmdRelClk;
    cmdLaneIdx = m.cmdLaneIdx;
    strides = m.strides;
    totalLanes = m.totalLanes;
    dataBus = m.dataBus;
    dataNBL = m.nBL;
    dataIdxByOrigIdx = m.dataIdxByOrigIdx;
    reqData = m.reqData;
    reqSlotCount = m.reqData.reqSlotCount;
    tpBinSize = m.tpBinSize;
    tpBinCount = m.tpBinCount;
    tpDataUtil = m.tpDataUtil;
    tpCmdUtil = m.tpCmdUtil;
    tpDataMax = m.tpDataMax;
    tpCmdMax = m.tpCmdMax;
    tpLaneAccess = m.tpLaneAccess;
  }

  // ── Lane lookups (rebuilt when the panel's tree layout changes) ─
  let cmdBusVisible = false;
  /** Canvas y of the CMD BUS summary row (Commands view), null when not shown. */
  let cmdSumY: number | null = null;
  let dataBusVisible = false;
  let reqBusVisible = false;
  let lastLayoutVersion = -1;
  let cmdSwimlaneData: Float32Array | null = null;
  let dataSwimlaneData: Float32Array | null = null;
  let reqSwimlaneData: Float32Array | null = null;

  // ── GPU resources (only those of this panel's view) ────────────
  let cmdSwimlaneTex: createREGL.Texture2D | null = null;
  let cmdLookupTex: createREGL.Texture2D | null = null;
  let dataSwimlaneTex: createREGL.Texture2D | null = null;
  let dataLookupTex: createREGL.Texture2D | null = null;
  let reqSwimlaneTex: createREGL.Texture2D | null = null;
  let reqLookupTex: createREGL.Texture2D | null = null;
  // Updated in place on live refreshes, never replaced, so draw-command closures stay valid.
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
  let gridBuf: createREGL.Buffer | null = null;
  let gridLineCount = 0;
  let _drawCmd: createREGL.DrawCommand | null = null;
  let _drawData: createREGL.DrawCommand | null = null;
  let _drawCmdHover: createREGL.DrawCommand | null = null;
  let _drawDataHover: createREGL.DrawCommand | null = null;
  let _drawReq: createREGL.DrawCommand | null = null;
  let _drawReqHover: createREGL.DrawCommand | null = null;
  let _drawGrid: createREGL.DrawCommand | null = null;

  // ── Bank State view ────────────────────────────────────────────
  let bankState: BankStateData | null = null;
  let bankLayer: BankLayer | null = null;
  /** Panel titles per sidebar header row (coords.join(',')), refreshed by recomputeBankStats. */
  const bankHeaderTitles = new Map<string, string>();
  let bankStatsTimer: ReturnType<typeof setTimeout> | null = null;

  // ── Layout-dependent rebuilds ─────────────────────────────────

  function refreshSwimlanes() {
    const el = canvas.value;
    if (!el) return;
    const canvasTop = el.getBoundingClientRect().top;
    const rows = panel.rowLayout;

    if (view === 'command') {
      cmdBusVisible = rows.some(r => r.busType === 'command' || r.busType === 'cmd_sum');
      const sumRow = rows.find(r => r.busType === 'cmd_sum');
      cmdSumY = sumRow ? sumRow.top + sumRow.height / 2 - canvasTop : null;
      dataBusVisible = rows.some(r => r.busType === 'data');
      if (cmdSwimlaneTex) {
        cmdSwimlaneData = buildSwimlaneLookup(rows, strides, totalLanes, canvasTop, 'command');
        setLookupTexture(cmdSwimlaneTex, totalLanes, cmdSwimlaneData);
      }
      if (dataSwimlaneTex) {
        dataSwimlaneData = buildSwimlaneLookup(rows, strides, totalLanes, canvasTop, 'data');
        setLookupTexture(dataSwimlaneTex, totalLanes, dataSwimlaneData);
      }
    } else if (view === 'request') {
      reqBusVisible = rows.some(r => r.busType === 'request');
      if (reqSwimlaneTex && reqData) {
        reqSwimlaneData = buildRequestSwimlaneLookup(rows, strides, canvasTop, reqData.maxStackPerLane, reqData.slotBase, reqSlotCount);
        setLookupTexture(reqSwimlaneTex, reqSlotCount, reqSwimlaneData);
      }
    } else if (view === 'bank') {
      bankLayer?.setLayout(rows, canvasTop);
    }
  }

  function refreshGrid() {
    const el = canvas.value;
    if (!el || !gridBuf) return;
    const rows = panel.rowLayout;
    const n = Math.min(rows.length, GRID_MAX_LINES);
    gridLineCount = n;
    if (n === 0) return;
    const canvasTop = el.getBoundingClientRect().top;
    const lines = new Float32Array(n);
    for (let i = 0; i < n; i++) lines[i] = rows[i]!.top + rows[i]!.height - canvasTop;
    gridBuf.subdata(lines, 0);
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
    forcedY?: number,
  ): HitResult | null {
    const el = canvas.value;
    if (!el || !swimlaneData || count === 0) return null;

    const rect = el.getBoundingClientRect();
    const cx = clientX - rect.left;
    const cy = clientY - rect.top;

    const relStart = viewState.start - referenceTime;
    const relTime = relStart + (cx / rect.width) * viewState.duration;

    const lo = Math.max(0, bisectLeft(relClk, relTime - maxDuration, count));
    const hi = Math.min(count, bisectRight(relClk, relTime, count));

    const arrays = sessionStore.arrays!;
    const spec = sessionStore.spec!;

    for (let j = lo; j < hi; j++) {
      const t = relClk[j]!;
      const origIdx = resolveIndex(j);
      const dur = getDuration(j, origIdx);
      if (relTime < t || relTime > t + dur) continue;

      const yCenter = forcedY ?? swimlaneData[laneIdxArr[j]! * 4]!;
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
          ? specTiming(spec, 'nCWL', 12)
          : specTiming(spec, 'nCL', 16);
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
    const relTime = relStart + (cx / rect.width) * viewState.duration;

    const lo = Math.max(0, bisectLeft(reqData.relClk, relTime - reqData.maxDuration, reqData.count));
    const hi = Math.min(reqData.count, bisectRight(reqData.relClk, relTime, reqData.count));

    const arrays = sessionStore.arrays!;

    for (let j = lo; j < hi; j++) {
      const t = reqData.relClk[j]!;
      const dur = reqData.duration[j]!;
      if (relTime < t || relTime > t + dur) continue;

      const yCenter = reqSwimlaneData[reqData.slotId[j]! * 4]!;
      if (Math.abs(cy - yCenter) > REQ_BAR_HEIGHT / 2 + 1) continue;

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
        bus: 'request',
        duration: Math.max(0, dur - qd),
        queueDelay: qd,
        requestGroupIndex: j,
      };
    }

    return null;
  }

  function hitTest(clientX: number, clientY: number): HitResult | null {
    if (view === 'request') return reqBusVisible ? hitTestRequests(clientX, clientY) : null;
    if (view !== 'command' || !cmdRelClk || !cmdLaneIdx) return null;

    const spec = sessionStore.spec;
    const arrays = sessionStore.arrays!;
    const cmdDuration = (_j: number, origIdx: number) => commandDuration(spec, arrays.cmdId[origIdx]!);
    const maxDur = maxCommandDuration(spec);

    if (cmdSumY !== null) {
      const sumHit = hitTestBus(clientX, clientY, 'command', cmdRelClk, cmdLaneIdx, cmdSwimlaneData, maxDur, N, j => j, cmdDuration, cmdSumY);
      if (sumHit) return sumHit;
    }
    const cmdHit = cmdBusVisible
      ? hitTestBus(clientX, clientY, 'command', cmdRelClk, cmdLaneIdx, cmdSwimlaneData, maxDur, N, j => j, cmdDuration)
      : null;
    if (cmdHit) return cmdHit;
    const bus = dataBus;
    return dataBusVisible && bus
      ? hitTestBus(clientX, clientY, 'data', bus.relClk, bus.laneIdx, dataSwimlaneData, dataNBL, bus.count, j => bus.originalIndices[j]!, () => dataNBL)
      : null;
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
    const point = (b: number) => {
      const value = bins[b]!;
      return {
        x: (((b + 0.5) * tpBinSize - relStart) / vw) * widthPx,
        y: chartTop + chartH - (value / effectiveMax) * chartH,
        bin: b,
        value,
        binStartTime: referenceTime + b * tpBinSize,
        binEndTime: referenceTime + (b + 1) * tpBinSize,
      };
    };
    const points: ReturnType<typeof point>[] = [];
    for (let b = sampled.startBin; b <= sampled.endBin; b += sampled.stride) points.push(point(b));
    if (points.length === 0 || points[points.length - 1]!.bin !== sampled.endBin) points.push(point(sampled.endBin));
    return points;
  }

  // ── Throughput per organization node ──────────────────────────

  /** Lanes [first, first + count) under a tree path (same tiling as buildSwimlaneLookup). */
  function laneRange(path: number[]): [number, number] {
    let first = 0;
    for (let k = 0; k < path.length; k++) first += path[k]! * strides[k]!;
    return [first, strides[path.length - 1] ?? totalLanes];
  }

  /** Data-bus share in bin b of the bursts to lanes [first, first + count). */
  function nodeShare(b: number, first: number, count: number): number {
    const acc = tpLaneAccess!;
    const o = b * totalLanes + first;
    let n = 0;
    for (let l = 0; l < count; l++) n += acc[o + l]!;
    return Math.min(1, (n * dataNBL) / tpBinSize);
  }

  /** Highest share of any node at a tree depth over the whole trace: rows at one depth share a y-scale. */
  const nodeMaxByDepth = new Map<number, number>();
  let nodeMaxModel: TraceModel | null = null;
  function nodeDepthMax(depth: number): number {
    if (nodeMaxModel !== model) {
      nodeMaxByDepth.clear();
      nodeMaxModel = model;
    }
    let v = nodeMaxByDepth.get(depth);
    if (v === undefined) {
      const count = strides[depth - 1] ?? totalLanes;
      let max = 0;
      for (let b = 0; b < tpBinCount; b++) {
        for (let first = 0; first < totalLanes; first += count) max = Math.max(max, nodeShare(b, first, count));
      }
      v = Math.max(max, 0.01);
      nodeMaxByDepth.set(depth, v);
    }
    return v;
  }

  /** "PseudoChannel 0 · Sid 0 · BankGroup 1 · Bank 3" (the channel level omitted). */
  function nodeLabel(path: number[]): string {
    const names = sessionStore.spec?.levelNames ?? [];
    const from = names[0] === 'Channel' ? 1 : 0;
    return path.slice(from).map((v, i) => `${names[from + i]} ${v}`).join(' · ');
  }

  function drawNodeRows(ctx: CanvasRenderingContext2D, W: number, H: number, canvasTop: number) {
    const rows = panel.rowLayout.filter(r => r.busType === 'tp_node');
    const relStart = viewState.start - referenceTime;
    const vw = viewState.duration;
    const sampled = throughputSampleRange(relStart, relStart + vw, W);
    if (rows.length === 0 || !tpLaneAccess || !sampled) return;
    const hov = tl.throughputHover.value;
    const xOf = (b: number) => (((b + 0.5) * tpBinSize - relStart) / vw) * W;
    const bins: number[] = [];
    for (let b = sampled.startBin; b <= sampled.endBin; b += sampled.stride) bins.push(b);
    if (bins[bins.length - 1] !== sampled.endBin) bins.push(sampled.endBin);

    ctx.save();
    ctx.font = '9px ui-monospace, IBM Plex Mono, monospace';
    ctx.textAlign = 'right';
    ctx.textBaseline = 'top';
    for (const r of rows) {
      const top = r.top - canvasTop;
      if (top > H || top + r.height < 0) continue;
      const [first, count] = laneRange(r.path);
      const yMax = nodeDepthMax(r.path.length);
      const pad = 3;
      const base = top + r.height - pad;
      const h = r.height - 2 * pad;
      const yOf = (b: number) => base - (nodeShare(b, first, count) / yMax) * h;
      // Held flat from the canvas edges to the first / last sample, like the channel charts.
      const pts = bins.map(b => [xOf(b), yOf(b)] as const);
      const line = [[Math.min(0, pts[0]![0]), pts[0]![1]] as const, ...pts, [Math.max(W, pts[pts.length - 1]![0]), pts[pts.length - 1]![1]] as const];

      ctx.beginPath();
      line.forEach(([x, y], i) => (i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y)));
      ctx.strokeStyle = PAPER.tp.data;
      ctx.lineWidth = 1;
      ctx.stroke();
      ctx.lineTo(line[line.length - 1]![0], base);
      ctx.lineTo(line[0]![0], base);
      ctx.closePath();
      ctx.fillStyle = PAPER.tp.dataFill;
      ctx.fill();

      axisLabel(ctx, `${Math.round(yMax * 100)}%`, 30, top + 2);

      if (hov?.series === 'node' && hov.path?.length === r.path.length && hov.path.every((v, k) => v === r.path[k])) {
        ctx.beginPath();
        ctx.arc(xOf(hov.binIndex), yOf(hov.binIndex), TP_POINT_RADIUS + 0.5, 0, Math.PI * 2);
        ctx.fillStyle = PAPER.tp.data;
        ctx.fill();
      }
    }
    ctx.restore();
  }

  function hitTestThroughput(clientX: number, clientY: number): ThroughputHover | null {
    const el = canvas.value;
    if (!el || view !== 'throughput' || !tpDataUtil || !tpCmdUtil || tpBinCount === 0) return null;

    const rect = el.getBoundingClientRect();
    if (clientX < rect.left || clientX > rect.right || clientY < rect.top || clientY > rect.bottom) return null;

    const rows = panel.rowLayout;
    const node = rows.find(r => r.busType === 'tp_node' && clientY >= r.top && clientY < r.top + r.height);
    if (node && tpLaneAccess) {
      const rel = viewState.start - referenceTime + ((clientX - rect.left) / rect.width) * viewState.duration;
      const b = Math.max(0, Math.min(tpBinCount - 1, Math.floor(rel / tpBinSize)));
      const [first, count] = laneRange(node.path);
      return {
        series: 'node',
        binIndex: b,
        value: nodeShare(b, first, count),
        binStartTime: referenceTime + b * tpBinSize,
        binEndTime: referenceTime + (b + 1) * tpBinSize,
        path: node.path,
        label: nodeLabel(node.path),
      };
    }
    const dataRow = rows.find(r => r.busType === 'tp_data');
    const cmdRow = rows.find(r => r.busType === 'tp_cmd');
    const inData = !!dataRow && clientY >= dataRow.top && clientY <= dataRow.top + dataRow.height;
    const inCmd = !!cmdRow && clientY >= cmdRow.top && clientY <= cmdRow.top + cmdRow.height;
    if (!inData && !inCmd) return null;
    const series = inData ? 'data' as const : 'cmd' as const;
    const row = series === 'data' ? dataRow : cmdRow;
    if (!row) return null;

    const relStart = viewState.start - referenceTime;
    const points = buildThroughputPoints(
      series === 'data' ? tpDataUtil : tpCmdUtil,
      series === 'data' ? tpDataMax : tpCmdMax,
      row.top - rect.top,
      row.height,
      relStart,
      relStart + viewState.duration,
      viewState.duration,
      rect.width,
    );

    const cx = clientX - rect.left;
    const cy = clientY - rect.top;
    let best = -1;
    let bestD2 = TP_HOVER_RADIUS * TP_HOVER_RADIUS;
    for (let i = 0; i < points.length; i++) {
      const dx = points[i]!.x - cx;
      const dy = points[i]!.y - cy;
      const d2 = dx * dx + dy * dy;
      if (d2 <= bestD2) {
        bestD2 = d2;
        best = i;
      }
    }
    if (best < 0) return null;
    const p = points[best]!;
    return { series, binIndex: p.bin, value: p.value, binStartTime: p.binStartTime, binEndTime: p.binEndTime };
  }

  function bankHitTest(clientX: number, clientY: number): BankHover | null {
    const el = canvas.value;
    const bs = bankState;
    const arrays = sessionStore.arrays;
    const spec = sessionStore.spec;
    if (!el || !bs || !arrays || !spec || view !== 'bank') return null;
    const rect = el.getBoundingClientRect();
    if (clientX < rect.left || clientX > rect.right || clientY < rect.top || clientY > rect.bottom) return null;
    const row = panel.rowLayout.find(r => clientY >= r.top && clientY < r.top + r.height
      && (r.busType === 'bank' || r.busType === 'bk_bus' || r.busType === 'bk_faw'));
    if (!row) return null;
    const t = viewState.start + ((clientX - rect.left) / rect.width) * viewState.duration;
    const L = bs.layout;
    const causeInfo = (i: number) => (i >= 0 && i < N
      ? { causeName: spec.commandNames[arrays.cmdId[i]!] ?? '?', causeClk: Number(arrays.clk[i]!) }
      : {});

    if (row.busType === 'bank') {
      const lane = row.path[0]!;
      const h = bankStateAt(bs, lane, t);
      if (!h) return null;
      const out: BankHover = {
        row: 'bank',
        title: bankLaneLabel(L, lane),
        path: bankLanePath(L, lane),
        state: bankKindName(h.kind),
        color: bankKindColor(h.kind),
        start: h.start,
        end: h.end,
        cause: h.cause,
        ...causeInfo(h.cause),
      };
      if (h.sub) {
        out.sub = h.sub === 'bus' ? 'data bus busy' : h.sub === 'bg' ? 'bank group blocked (tCCD_L)' : 'not blocked (open-but-starved)';
        out.subColor = h.sub === 'bus' ? BANK_COLORS.bus : h.sub === 'bg' ? BANK_COLORS.bg : BANK_COLORS.open;
        out.subStart = h.subStart;
        out.subEnd = h.subEnd;
      }
      return out;
    }

    const isBus = row.busType === 'bk_bus';
    const set = isBus ? bs.bus : bs.faw;
    if (!set) return null;
    const dom = bankCoordsRange(L, row.path).dom;
    const g = bankIntervalOrGap(set, dom, t, viewState.minTime, viewState.maxTime);
    return {
      row: isBus ? 'bus' : 'faw',
      title: isBus ? 'DATA BUS' : 'ACT / tFAW',
      path: bankCoordsName(L, row.path, `Channel ${sessionStore.header?.channelId ?? 0}`),
      state: isBus
        ? (g.inside ? 'burst on the bus (nBL)' : 'bus idle')
        : (g.inside ? 'four ACTs in the trailing tFAW: no ACT allowed' : 'ACT allowed'),
      color: g.inside ? (isBus ? BANK_COLORS.busLane : BANK_COLORS.fawLane) : '#ffffff',
      start: g.start,
      end: g.end,
      cause: g.cause,
      ...causeInfo(g.cause),
    };
  }

  // ── Bank statistics ───────────────────────────────────────────

  function recomputeBankStats() {
    if (bankStatsTimer) {
      clearTimeout(bankStatsTimer);
      bankStatsTimer = null;
    }
    const bs = bankState;
    const el = canvas.value;
    if (!bs || !el) {
      tl.bankStats.value = null;
      bankHeaderTitles.clear();
      return;
    }
    const sel = rangeSelection.value;
    const scope: 'view' | 'range' = sel && sel.endTime > sel.startTime ? 'range' : 'view';
    const t0 = scope === 'range' ? sel!.startTime : viewState.start;
    const t1 = scope === 'range' ? sel!.endTime : viewState.start + viewState.duration;
    if (!(t1 > t0)) return;
    const L = bs.layout;
    const geom = bankGeometry(bs, panel.rowLayout, el.getBoundingClientRect().top);
    const opts = {
      hasBg: !!bs.bg,
      hasWrrec: bs.hasWrrecSpans,
      hasRef: bs.hasRefSpans,
    };

    // Per header row: the panel title over every bank of that domain.
    bankHeaderTitles.clear();
    for (const h of geom.headers) {
      const { level, dom, first, count } = bankCoordsRange(L, h.coords);
      const lanes = Array.from({ length: count }, (_, i) => first + i);
      const acc = bankAccumulate(bs, lanes, t0, t1, bankEmptyStats(t0, t1));
      const busPct = level === L.busLevel ? bankCoverage(bs.bus, dom, t0, t1) * 100 : null;
      const fawPct = L.fawLevel !== null && level === L.fawLevel ? bankCoverage(bs.faw, dom, t0, t1) * 100 : null;
      bankHeaderTitles.set(h.coords.join(','), bankTitle({ busPct, fawPct }, bankShares(acc), opts));
    }

    // Inspector summary: the banks currently visible in the sidebar.
    const lanes = geom.visibleLanes.length > 0 ? geom.visibleLanes : Array.from({ length: L.numBanks }, (_, i) => i);
    const acc = bankAccumulate(bs, lanes, t0, t1, bankEmptyStats(t0, t1));
    const mean = (set: typeof bs.bus | null, level: number | null) => {
      if (!set || level === null) return null;
      const doms = new Set(lanes.map(l => bankDomOf(L, l, level)));
      let sum = 0;
      for (const d of doms) sum += bankCoverage(set, d, t0, t1);
      return doms.size ? (sum / doms.size) * 100 : null;
    };
    tl.bankStats.value = {
      scope,
      t0,
      t1,
      banks: acc.banks,
      busPct: mean(bs.bus, L.busLevel),
      fawPct: mean(bs.faw, L.fawLevel),
      shares: bankShares(acc),
      ...opts,
    };
  }

  function scheduleBankStats() {
    if (bankStatsTimer || view !== 'bank') return;
    bankStatsTimer = setTimeout(recomputeBankStats, BANK_STATS_THROTTLE_MS);
  }

  function applyBankState(bs: BankStateData | null) {
    bankState = bs;
    if (bankLayer && bs) {
      bankLayer.setData(bs);
      lastLayoutVersion = -1; // re-run setLayout against the new data on the next frame
    }
    scheduleBankStats();
  }

  // ── 2D overlay ────────────────────────────────────────────────

  function placeCursorAtClientX(clientX: number) {
    const el = canvas.value;
    if (!el) return;
    const rect = el.getBoundingClientRect();
    tl.placeCursor(viewState.start + ((clientX - rect.left) / rect.width) * viewState.duration, view);
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

  /** Right-aligned axis label on a light backing, so plotted lines never run through it. */
  function axisLabel(ctx: CanvasRenderingContext2D, text: string, x: number, y: number) {
    const w = ctx.measureText(text).width;
    const top = ctx.textBaseline === 'top' ? y - 1 : y - 6;
    ctx.fillStyle = 'rgba(255, 255, 255, 0.85)';
    ctx.fillRect(x - w - 2, top, w + 4, 11);
    ctx.fillStyle = PAPER.tp.axis;
    ctx.fillText(text, x, y);
  }

  function drawThroughputChart(
    ctx: CanvasRenderingContext2D,
    W: number,
    bins: Float32Array,
    yMax: number,
    areaTop: number,
    areaH: number,
    series: 'data' | 'cmd',
    strokeColor: string,
    fillColor: string,
  ) {
    const pad = 8;
    const chartTop = areaTop + pad;
    const chartH = areaH - pad * 2;
    if (chartH <= 0) return;
    const vw = viewState.duration;
    const effectiveMax = Math.max(yMax, 0.01);

    ctx.save();
    ctx.beginPath();
    ctx.rect(0, areaTop, W, areaH);
    ctx.clip();

    if (rangeSelection.value) {
      const sel = rangeSelection.value;
      const x0 = ((sel.startTime - viewState.start) / vw) * W;
      const x1 = ((sel.endTime - viewState.start) / vw) * W;
      const sx = Math.max(0, Math.min(W, Math.min(x0, x1)));
      const ex = Math.max(0, Math.min(W, Math.max(x0, x1)));
      if (ex > sx) {
        ctx.fillStyle = PAPER.tp.rangeFill;
        ctx.fillRect(sx, areaTop, ex - sx, areaH);
      }
    }

    // Y-axis labels and gridlines
    ctx.font = '9px ui-monospace, IBM Plex Mono, monospace';
    ctx.textAlign = 'right';
    ctx.textBaseline = 'middle';
    ctx.fillStyle = PAPER.tp.axis;
    for (const frac of [0, 0.25, 0.5, 0.75, 1.0]) {
      const y = chartTop + chartH - frac * chartH;
      if (y < areaTop || y > areaTop + areaH) continue;
      axisLabel(ctx, `${(effectiveMax * frac * 100).toFixed(0)}%`, 30, y);
      ctx.beginPath();
      ctx.strokeStyle = PAPER.tp.grid;
      ctx.lineWidth = 1;
      ctx.moveTo(34, Math.round(y) + 0.5);
      ctx.lineTo(W, Math.round(y) + 0.5);
      ctx.stroke();
    }

    // Line + fill (point-decimated by pixel width for stable FPS when zoomed out)
    const relStart = viewState.start - referenceTime;
    const points = buildThroughputPoints(bins, yMax, areaTop, areaH, relStart, relStart + vw, vw, W);
    if (points.length === 0) {
      ctx.restore();
      return;
    }
    const line: Array<{ x: number; y: number }> = [];
    const first = points[0]!;
    const last = points[points.length - 1]!;
    if (first.x > 0) line.push({ x: 0, y: first.y });
    for (const p of points) line.push({ x: p.x, y: p.y });
    if (last.x < W) line.push({ x: W, y: last.y });

    ctx.beginPath();
    line.forEach((p, i) => (i === 0 ? ctx.moveTo(p.x, p.y) : ctx.lineTo(p.x, p.y)));
    ctx.strokeStyle = strokeColor;
    ctx.lineWidth = 1.5;
    ctx.stroke();

    ctx.lineTo(line[line.length - 1]!.x, chartTop + chartH);
    ctx.lineTo(line[0]!.x, chartTop + chartH);
    ctx.closePath();
    ctx.fillStyle = fillColor;
    ctx.fill();

    // Point markers, thinned so they never crowd in screen space.
    ctx.beginPath();
    let lastMarkerX = Number.NEGATIVE_INFINITY;
    for (let i = 0; i < points.length; i++) {
      const p = points[i]!;
      if (i !== points.length - 1 && p.x - lastMarkerX < TP_MARKER_MIN_GAP_PX) continue;
      lastMarkerX = p.x;
      ctx.moveTo(p.x + TP_POINT_RADIUS, p.y);
      ctx.arc(p.x, p.y, TP_POINT_RADIUS, 0, Math.PI * 2);
    }
    ctx.fillStyle = PAPER.tp.markerFill;
    ctx.fill();
    ctx.strokeStyle = strokeColor;
    ctx.lineWidth = 1;
    ctx.stroke();

    const hov = tl.throughputHover.value;
    if (hov && hov.series === series) {
      const hp = points.find(p => p.bin === hov.binIndex);
      if (hp) {
        ctx.beginPath();
        ctx.arc(hp.x, hp.y, TP_POINT_RADIUS + 2, 0, Math.PI * 2);
        ctx.fillStyle = PAPER.tp.markerHalo;
        ctx.fill();
        ctx.beginPath();
        ctx.arc(hp.x, hp.y, TP_POINT_RADIUS + 0.5, 0, Math.PI * 2);
        ctx.fillStyle = strokeColor;
        ctx.fill();
      }
    }

    ctx.restore();
  }

  // ── Linked selection ──────────────────────────────────────────

  /** Canvas rectangle of a command bar (in its lane) or request bar, or null when it is not on screen. */
  function linkedBar(kind: 'cmd' | 'req', idx: number): { x: number; y: number; w: number; h: number } | null {
    if (!model) return null;
    const pxPerCycle = cssW / viewState.duration;
    const relStart = viewState.start - referenceTime;
    if (kind === 'cmd') {
      if (!cmdSwimlaneData || !cmdBusVisible || idx >= N) return null;
      const y = cmdSwimlaneData[cmdLaneIdx![idx]! * 4]!;
      if (!(y > 0)) return null;
      const x = (cmdRelClk![idx]! - relStart) * pxPerCycle;
      return { x, y: y - ROW_HEIGHT / 2, w: Math.max(2, commandDuration(sessionStore.spec, model.cmdId[idx]!) * pxPerCycle), h: ROW_HEIGHT };
    }
    if (!reqSwimlaneData || !reqData || !reqBusVisible || idx >= reqData.count) return null;
    const y = reqSwimlaneData[reqData.slotId[idx]! * 4]!;
    if (!(y > 0)) return null;
    const x = (reqData.relClk[idx]! - relStart) * pxPerCycle;
    return { x, y: y - REQ_BAR_HEIGHT / 2, w: Math.max(2, reqData.duration[idx]! * pxPerCycle), h: REQ_BAR_HEIGHT };
  }

  function outlineBar(ctx: CanvasRenderingContext2D, r: { x: number; y: number; w: number; h: number }, color: string, width: number) {
    const g = 2;
    ctx.lineWidth = width + 2;
    ctx.strokeStyle = '#ffffff';
    ctx.strokeRect(r.x - g, r.y - g, r.w + 2 * g, r.h + 2 * g);
    ctx.lineWidth = width;
    ctx.strokeStyle = color;
    ctx.strokeRect(r.x - g, r.y - g, r.w + 2 * g, r.h + 2 * g);
  }

  /** Outline this panel's part of the selected group (accent) and of the hovered item's group (muted). */
  function drawLinked(ctx: CanvasRenderingContext2D) {
    if (view !== 'command' && view !== 'request') return;
    const sel = tl.selection.value;
    const pv = tl.linkPreview.value;
    const draw = (g: { req: number; cmds: number[] }, color: string, width: number) => {
      if (view === 'request') {
        const r = g.req >= 0 ? linkedBar('req', g.req) : null;
        if (r) outlineBar(ctx, r, color, width);
      } else {
        for (const i of g.cmds) {
          const r = linkedBar('cmd', i);
          if (r) outlineBar(ctx, r, color, width);
        }
      }
    };
    ctx.save();
    if (pv && !(sel && sel.req === pv.req && sel.cmds[0] === pv.cmds[0])) draw(pv, PAPER.muted, 1);
    if (sel) draw(sel, PAPER.accent, 1.5);
    ctx.restore();
  }

  /** Client-space anchor of a linked bar, for the dock's leader lines. */
  function linkAnchor(kind: 'cmd' | 'req', idx: number): LinkAnchor | null {
    const el = canvas.value;
    const r = linkedBar(kind, idx);
    if (!el || !r) return null;
    const rect = el.getBoundingClientRect();
    return { x: rect.left + r.x + r.w / 2, top: rect.top + r.y, bottom: rect.top + r.y + r.h, clipTop: rect.top, clipBottom: rect.bottom };
  }

  function drawOverlayLabels() {
    const el = canvas.value;
    const lo = labelCanvas.value;
    if (!el || !lo) return;
    const ctx = lo.getContext('2d');
    if (!ctx) return;

    if (lo.width !== el.width || lo.height !== el.height) {
      lo.width = el.width;
      lo.height = el.height;
    }
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, cssW, cssH);
    const W = cssW;
    const H = cssH;

    const spec = sessionStore.spec;
    const arrays = sessionStore.arrays;
    if (!spec || !arrays || !model) return;

    const vw = viewState.duration;
    const pxPerCycle = W / vw;
    const relStart = viewState.start - referenceTime;
    const relEnd = relStart + vw;
    const viewBase = Math.floor(relStart);
    const viewFrac = relStart - viewBase;
    const canvasTop = el.getBoundingClientRect().top;

    // ── Group bands (Commands / Requests): expanded hierarchy rows, as in the Bank state view ─
    if (view === 'command' || view === 'request') {
      const isCmd = view === 'command';
      const prefix = isCmd ? 'cmd' : 'req';
      const expanded = new Set(panel.expandedState.length > 0 ? panel.expandedState : computeDefaultExpanded(view, spec));
      const leafDepth = specStructuralDepth(spec);
      ctx.fillStyle = PAPER.groupBand;
      for (const r of panel.rowLayout) {
        if (r.busType !== view || r.depth >= leafDepth) continue;
        if (!expanded.has(nodeId(prefix, r.path))) continue;
        ctx.fillRect(0, r.top - canvasTop, W, r.height);
      }
      const dataSum = isCmd ? panel.rowLayout.find(r => r.busType === 'data') : undefined;
      if (dataSum) {
        ctx.fillStyle = PAPER.summaryRule;
        ctx.fillRect(0, Math.round(dataSum.top + dataSum.height - canvasTop) - 1, W, 1.5);
      }
    }

    // ── Vertical gridlines at major ticks ───────────────────────
    if (view !== 'bank') {
      const step = niceStep(vw, W);
      const absStart = viewState.start;
      const absEnd = absStart + vw;
      const eps = step / 1000;
      ctx.beginPath();
      ctx.strokeStyle = VGRID_COLOR;
      ctx.lineWidth = 1;
      for (let t = Math.ceil(absStart / step) * step; t <= absEnd + eps; t += step) {
        if (t < viewState.minTime || t > viewState.maxTime) continue;
        const x = Math.round(((t - absStart) / vw) * W) + 0.5;
        ctx.moveTo(x, 0);
        ctx.lineTo(x, H);
      }
      ctx.stroke();
    }

    // ── Range selection highlight ──────────────────────────────
    const sel = rangeSelection.value;
    if (sel) {
      const xLeft = ((sel.startTime - viewState.start) / vw) * W;
      const xRight = ((sel.endTime - viewState.start) / vw) * W;
      const x0 = Math.max(0, Math.min(W, xLeft));
      const x1 = Math.max(0, Math.min(W, xRight));
      if (x1 > x0) {
        ctx.fillStyle = RANGE_FILL;
        ctx.fillRect(x0, 0, x1 - x0, H);
        ctx.beginPath();
        ctx.strokeStyle = RANGE_EDGE;
        ctx.lineWidth = 1;
        for (const x of [xLeft, xRight]) {
          if (x < 0 || x > W) continue;
          const sx = Math.round(x) + 0.5;
          ctx.moveTo(sx, 0);
          ctx.lineTo(sx, H);
        }
        ctx.stroke();
      }
    }

    // ── Bank State view: separators, panel titles, command markers ─
    if (view === 'bank') {
      if (bankLayer && bankState && cmdRelClk && N > 0) {
        const rawArrays = toRaw(arrays);
        const L = bankState.layout;
        const relClk = cmdRelClk;
        bankLayer.drawOverlay(ctx, { start: viewState.start, duration: vw }, W, H, {
          clk: rawArrays.clk,
          cmdId: rawArrays.cmdId,
          addr: tl.rawAddr(),
          commandRange: (a, b) => [
            bisectLeft(relClk, a - referenceTime, N),
            bisectRight(relClk, b - referenceTime, N),
          ],
          headerTitles: bankHeaderTitles,
          headerName: c => (c.length === 0
            ? `Channel ${sessionStore.header?.channelId ?? 0}`
            : `${L.levelNames[L.firstLevel + c.length - 1]} ${c[c.length - 1]}`),
        });
      }
      drawCursorLine(ctx, W, H);
      return;
    }

    // ── Throughput charts ────────────────────────────────────
    if (view === 'throughput') {
      if (tpDataUtil && tpCmdUtil && tpBinCount > 0) {
        const dataRow = panel.rowLayout.find(r => r.busType === 'tp_data');
        const cmdRow = panel.rowLayout.find(r => r.busType === 'tp_cmd');
        if (dataRow) drawThroughputChart(ctx, W, tpDataUtil, tpDataMax, dataRow.top - canvasTop, dataRow.height, 'data', PAPER.tp.data, PAPER.tp.dataFill);
        if (cmdRow) drawThroughputChart(ctx, W, tpCmdUtil, tpCmdMax, cmdRow.top - canvasTop, cmdRow.height, 'cmd', PAPER.tp.cmd, PAPER.tp.cmdFill);
        drawNodeRows(ctx, W, H, canvasTop);
      }
      drawCursorLine(ctx, W, H);
      return;
    }

    // ── Command names on wide-enough bars ──────────────────────
    if (view === 'command') {
      ctx.font = '10px ui-monospace, IBM Plex Mono, monospace';
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      const drawLabel = (sx: number, y: number, raw: string) => {
        const label = raw.length > 14 ? `${raw.slice(0, 13)}…` : raw;
        ctx.shadowColor = PAPER.barLabelShadow;
        ctx.shadowBlur = 4;
        ctx.fillStyle = PAPER.barLabel;
        ctx.fillText(label, sx, y);
        ctx.shadowBlur = 0;
      };

      if (cmdBusVisible && N > 0 && cmdRelClk && cmdLaneIdx && cmdSwimlaneData
        && pxPerCycle >= CMD_LABEL_MIN_PX_PER_CYCLE) {
        const startIdx = Math.max(0, bisectLeft(cmdRelClk, relStart - maxCommandDuration(spec), N));
        const endIdx = Math.min(N, bisectRight(cmdRelClk, relEnd, N), startIdx + CMD_LABEL_MAX_VISIBLE);
        for (let i = startIdx; i < endIdx; i++) {
          const cid = arrays.cmdId[i]!;
          const cxWorld = cmdRelClk[i]! - viewBase + commandDuration(spec, cid) * 0.5;
          drawLabel(((cxWorld - viewFrac) / vw) * W, cmdSwimlaneData[cmdLaneIdx[i]! * 4]!, spec.commandNames[cid] ?? '?');
        }
      }

      if (dataBusVisible && dataBus && dataBus.count > 0 && dataSwimlaneData
        && pxPerCycle * dataNBL >= LABEL_MIN_BAR_WIDTH_PX) {
        const M = dataBus.count;
        const startIdx = Math.max(0, bisectLeft(dataBus.relClk, relStart - dataNBL, M));
        const endIdx = Math.min(M, bisectRight(dataBus.relClk, relEnd, M), startIdx + CMD_LABEL_MAX_VISIBLE);
        for (let j = startIdx; j < endIdx; j++) {
          const cxWorld = dataBus.relClk[j]! - viewBase + dataNBL * 0.5;
          drawLabel(((cxWorld - viewFrac) / vw) * W, dataSwimlaneData[dataBus.laneIdx[j]! * 4]!, spec.commandNames[dataBus.cmdId[j]!] ?? '?');
        }
      }
    }

    drawLinked(ctx);
    drawCursorLine(ctx, W, H);
  }

  // ── GPU lifecycle ─────────────────────────────────────────────

  let refreshCommandLookupTextures: (() => void) | null = null;
  let refreshRequestLookupTextures: (() => void) | null = null;

  /** Destroy the regl context; `release` also drops the WebGL context (the canvas is going away). */
  function destroyGpu(release: boolean) {
    if (!regl) return;
    const gl = (regl as unknown as { _gl?: WebGLRenderingContext })._gl;
    regl.destroy();
    if (release) gl?.getExtension('WEBGL_lose_context')?.loseContext();
    regl = null;
    bankLayer = null;
    _drawCmd = _drawData = _drawCmdHover = _drawDataHover = _drawReq = _drawReqHover = _drawGrid = null;
    cmdSwimlaneTex = cmdLookupTex = dataSwimlaneTex = dataLookupTex = reqSwimlaneTex = reqLookupTex = null;
    cmdRelClkBuf = cmdCmdIdBuf = cmdLaneIdxBuf = dataRelClkBuf = dataCmdIdBuf = dataLaneIdxBuf = null;
    reqRelClkBuf = reqDurationBuf = reqQueueDelayBuf = reqTypeIdBuf = reqLaneIdxBuf = gridBuf = null;
    refreshCommandLookupTextures = null;
    refreshRequestLookupTextures = null;
    gpuGeneration = -1;
  }

  /** Live refresh of the same session: re-upload this view's buffers in place. */
  function uploadModel(m: TraceModel) {
    const prevSlots = reqSlotCount;
    adoptModel(m);
    if (view === 'command' && cmdRelClkBuf && dataBus) {
      cmdRelClkBuf(m.cmdRelClk);
      cmdCmdIdBuf!(m.cmdId);
      cmdLaneIdxBuf!(m.cmdLaneIdx);
      dataRelClkBuf!(dataBus.relClk);
      dataCmdIdBuf!(dataBus.cmdId);
      dataLaneIdxBuf!(dataBus.laneIdx);
    }
    if (view === 'request' && reqRelClkBuf && reqData) {
      reqRelClkBuf(reqData.relClk);
      reqDurationBuf!(reqData.duration);
      reqQueueDelayBuf!(reqData.queueDelay);
      reqTypeIdBuf!(reqData.typeId);
      reqLaneIdxBuf!(reqData.slotId);
    }
    // New request slots (or new rows in the sidebar) need fresh lane lookups.
    if (reqSlotCount !== prevSlots) lastLayoutVersion = -1;
  }

  function initGpu(el: HTMLCanvasElement, m: TraceModel) {
    destroyGpu(false);
    adoptModel(m);
    sizeCanvases(el);
    const spec = sessionStore.spec;
    if (!spec) return;

    try {
      regl = createREGL({
        canvas: el,
        attributes: { antialias: false, alpha: false, stencil: true },
        extensions: ['angle_instanced_arrays', 'oes_texture_float'],
      });
    } catch (e) {
      console.error('Failed to initialize regl:', e);
      return;
    }
    const r = regl;
    gpuGeneration = m.sessionGeneration;
    lastLayoutVersion = -1;

    const quad: [number, number][] = [
      [0, 0], [1, 0], [0, 1],
      [0, 1], [1, 0], [1, 1],
    ];
    const lookupTex = (width: number, data?: Float32Array) => r.texture({
      width: Math.max(width, 1), height: 1, data: data ?? new Float32Array(Math.max(width, 1) * 4),
      format: 'rgba', type: 'float', min: 'nearest', mag: 'nearest',
    });
    const resolution = (ctx: any) => [ctx.viewportWidth / dpr, ctx.viewportHeight / dpr];
    const alphaBlend = {
      enable: true,
      func: { srcRGB: 'src alpha', srcAlpha: 'one', dstRGB: 'one minus src alpha', dstAlpha: 'one minus src alpha' },
    } as const;

    interface TraceProps {
      viewRange: [number, number];
      viewBase: number;
      offset: number;
      instances: number;
      forceY?: number;
    }
    const instanced = {
      depth: { enable: false },
      instances: ((_: any, p: TraceProps) => p.instances) as any,
      count: 6,
    };
    const attr = (buffer: createREGL.Buffer, bytes: number, normalized?: false) => ({
      buffer, divisor: 1, ...(normalized === false ? { normalized } : {}),
      offset: ((_: any, p: TraceProps) => p.offset * bytes) as any,
    });

    if (view !== 'bank') {
      gridBuf = r.buffer({ length: GRID_MAX_LINES * 4, type: 'float', usage: 'dynamic' });
      _drawGrid = r({
        vert: GRID_VERT,
        frag: GRID_FRAG,
        attributes: { a_quad: quad, a_yScreen: { buffer: gridBuf, divisor: 1 } },
        uniforms: { u_resolutionY: (ctx: any) => ctx.viewportHeight / dpr },
        depth: { enable: false },
        instances: ((_: any, p: { count: number }) => p.count) as any,
        count: 6,
      });
    }

    if (view === 'command') {
      const bus = m.dataBus;
      cmdRelClkBuf = r.buffer({ data: m.cmdRelClk, type: 'float' });
      cmdCmdIdBuf = r.buffer({ data: m.cmdId, type: 'uint8' });
      cmdLaneIdxBuf = r.buffer({ data: m.cmdLaneIdx, type: 'uint16' });
      dataRelClkBuf = r.buffer({ data: bus.relClk, type: 'float' });
      dataCmdIdBuf = r.buffer({ data: bus.cmdId, type: 'uint8' });
      dataLaneIdxBuf = r.buffer({ data: bus.laneIdx, type: 'uint16' });
      cmdLookupTex = lookupTex(256, buildCmdTextureData(spec, sessionStore.commandColors, 1, spec.commandCycles));
      dataLookupTex = lookupTex(256, buildCmdTextureData(spec, sessionStore.commandColors, dataNBL));
      cmdSwimlaneTex = lookupTex(totalLanes);
      dataSwimlaneTex = lookupTex(totalLanes);

      refreshCommandLookupTextures = () => {
        const s = sessionStore.spec;
        if (!s || !cmdLookupTex || !dataLookupTex) return;
        setLookupTexture(cmdLookupTex, 256, buildCmdTextureData(s, sessionStore.commandColors, 1, s.commandCycles));
        setLookupTexture(dataLookupTex, 256, buildCmdTextureData(s, sessionStore.commandColors, dataNBL));
      };

      const busDraw = (vert: string, frag: string, relBuf: createREGL.Buffer, idBuf: createREGL.Buffer, laneBuf: createREGL.Buffer,
        lookup: () => createREGL.Texture2D, lanes: () => createREGL.Texture2D, hover: boolean) => r({
        vert,
        frag,
        attributes: {
          a_quad: quad,
          a_relClk: attr(relBuf, 4),
          a_cmdId: attr(idBuf, 1, false),
          a_laneIdx: attr(laneBuf, 2, false),
        },
        uniforms: {
          u_cmdLookup: lookup,
          u_lanes: lanes,
          u_viewRange: r.prop<TraceProps, 'viewRange'>('viewRange'),
          u_viewBase: r.prop<TraceProps, 'viewBase'>('viewBase'),
          u_resolution: resolution,
          u_rowHeight: ROW_HEIGHT,
          u_laneCount: Math.max(totalLanes, 1),
          ...(hover ? {} : { u_forceY: ((_: any, p: TraceProps) => p.forceY ?? -1) as any }),
        },
        ...(hover ? { blend: alphaBlend } : {}),
        ...instanced,
      });
      _drawCmd = busDraw(TRACE_VERT, TRACE_FRAG, cmdRelClkBuf, cmdCmdIdBuf, cmdLaneIdxBuf, () => cmdLookupTex!, () => cmdSwimlaneTex!, false);
      _drawData = busDraw(TRACE_VERT, TRACE_FRAG, dataRelClkBuf, dataCmdIdBuf, dataLaneIdxBuf, () => dataLookupTex!, () => dataSwimlaneTex!, false);
      _drawCmdHover = busDraw(TRACE_HOVER_VERT, REQ_HOVER_FRAG, cmdRelClkBuf, cmdCmdIdBuf, cmdLaneIdxBuf, () => cmdLookupTex!, () => cmdSwimlaneTex!, true);
      _drawDataHover = busDraw(TRACE_HOVER_VERT, REQ_HOVER_FRAG, dataRelClkBuf, dataCmdIdBuf, dataLaneIdxBuf, () => dataLookupTex!, () => dataSwimlaneTex!, true);
    }

    if (view === 'request') {
      const req = m.reqData;
      reqRelClkBuf = r.buffer({ data: req.relClk, type: 'float' });
      reqDurationBuf = r.buffer({ data: req.duration, type: 'float' });
      reqQueueDelayBuf = r.buffer({ data: req.queueDelay, type: 'float' });
      reqTypeIdBuf = r.buffer({ data: req.typeId, type: 'uint8' });
      reqLaneIdxBuf = r.buffer({ data: req.slotId, type: 'uint16' });
      reqLookupTex = lookupTex(256, buildReqLookupTexture(sessionStore.requestTypeColors));
      reqSwimlaneTex = lookupTex(req.reqSlotCount);

      refreshRequestLookupTextures = () => {
        if (reqLookupTex) setLookupTexture(reqLookupTex, 256, buildReqLookupTexture(sessionStore.requestTypeColors));
      };

      const reqDraw = (frag: string) => r({
        vert: REQ_VERT,
        frag,
        attributes: {
          a_quad: quad,
          a_relClk: attr(reqRelClkBuf!, 4),
          a_cmdId: attr(reqTypeIdBuf!, 1, false),
          a_laneIdx: attr(reqLaneIdxBuf!, 2, false),
          a_duration: attr(reqDurationBuf!, 4),
          a_queueDelay: attr(reqQueueDelayBuf!, 4),
        },
        uniforms: {
          u_cmdLookup: () => reqLookupTex!,
          u_lanes: () => reqSwimlaneTex!,
          u_viewRange: r.prop<TraceProps, 'viewRange'>('viewRange'),
          u_viewBase: r.prop<TraceProps, 'viewBase'>('viewBase'),
          u_resolution: resolution,
          u_rowHeight: REQ_BAR_HEIGHT,
          u_laneCount: () => Math.max(reqSlotCount, 1),
        },
        blend: alphaBlend,
        ...instanced,
      });
      _drawReq = reqDraw(REQ_FRAG);
      _drawReqHover = reqDraw(REQ_HOVER_FRAG);
    }

    if (view === 'bank') {
      bankLayer = createBankLayer(r, () => dpr);
      if (bankState) bankLayer.setData(bankState);
    }

    r.frame(drawFrame);
  }

  function drawFrame() {
    const el = canvas.value;
    if (!el || !regl) return;
    tl.advance();
    if ((window.devicePixelRatio || 1) !== dpr) sizeCanvases(el);
    regl.clear({ color: BG_COLOR, depth: 1, stencil: 0 });

    if (panel.layoutVersion !== lastLayoutVersion) {
      lastLayoutVersion = panel.layoutVersion;
      refreshSwimlanes();
      refreshGrid();
    }

    const relStart = viewState.start - referenceTime;
    const relEnd = relStart + viewState.duration;
    const viewBase = Math.floor(relStart);
    const vr: [number, number] = [relStart - viewBase, relEnd - viewBase];

    if (view === 'command') {
      if (cmdBusVisible && N > 0 && cmdRelClk && _drawCmd) {
        const startIdx = Math.max(0, bisectLeft(cmdRelClk, relStart - maxCommandDuration(sessionStore.spec), N));
        const count = Math.min(N, bisectRight(cmdRelClk, relEnd, N)) - startIdx;
        if (count > 0) {
          _drawCmd({ viewRange: vr, viewBase, offset: startIdx, instances: count });
          if (cmdSumY !== null) _drawCmd({ viewRange: vr, viewBase, offset: startIdx, instances: count, forceY: cmdSumY });
        }
      }
      if (dataBusVisible && dataBus && dataBus.count > 0 && _drawData) {
        const M = dataBus.count;
        const startIdx = Math.max(0, bisectLeft(dataBus.relClk, relStart - dataNBL, M));
        const count = Math.min(M, bisectRight(dataBus.relClk, relEnd, M)) - startIdx;
        if (count > 0) _drawData({ viewRange: vr, viewBase, offset: startIdx, instances: count });
      }
    } else if (view === 'request' && reqBusVisible && reqData && reqData.count > 0 && _drawReq) {
      const R = reqData.count;
      const startIdx = Math.max(0, bisectLeft(reqData.relClk, relStart - reqData.maxDuration, R));
      const count = Math.min(R, bisectRight(reqData.relClk, relEnd, R)) - startIdx;
      if (count > 0) _drawReq({ viewRange: vr, viewBase, offset: startIdx, instances: count });
    } else if (view === 'bank' && bankLayer) {
      bankLayer.draw({ start: viewState.start, duration: viewState.duration });
    }

    if (gridLineCount > 0 && _drawGrid) _drawGrid({ count: gridLineCount });

    const hov = hoveredEvent.value;
    if (view === 'command' && hov && dataIdxByOrigIdx && (hov.bus === 'command' || hov.bus === 'data')) {
      const i = hov.index;
      if (i >= 0 && i < N) {
        if (cmdBusVisible && _drawCmdHover) _drawCmdHover({ viewRange: vr, viewBase, offset: i, instances: 1 });
        const dj = dataIdxByOrigIdx[i]!;
        if (dj >= 0 && dataBusVisible && _drawDataHover) _drawDataHover({ viewRange: vr, viewBase, offset: dj, instances: 1 });
      }
    }
    if (view === 'request' && hov?.bus === 'request' && hov.requestGroupIndex != null && reqData && reqBusVisible && _drawReqHover) {
      const j = hov.requestGroupIndex;
      if (j >= 0 && j < reqData.count) _drawReqHover({ viewRange: vr, viewBase, offset: j, instances: 1 });
    }

    drawOverlayLabels();
  }

  /** Bring the GPU side in line with the timeline's current model. */
  function sync() {
    const m = tl.model.value;
    const el = canvas.value;
    if (!m || !el) return;
    if (view === 'bank') void tl.ensureBankState();
    if (!regl || gpuGeneration !== m.sessionGeneration) initGpu(el, m);
    else if (m !== model) uploadModel(m);
  }

  // ── Interaction ───────────────────────────────────────────────

  function setHover(clientX: number, clientY: number) {
    if (view === 'command' || view === 'request') hoveredEvent.value = hitTest(clientX, clientY);
    else if (view === 'throughput') tl.throughputHover.value = hitTestThroughput(clientX, clientY);
    else tl.bankHover.value = bankHitTest(clientX, clientY);
  }

  function clearHover() {
    if (view === 'command' || view === 'request') hoveredEvent.value = null;
    else if (view === 'throughput') tl.throughputHover.value = null;
    else tl.bankHover.value = null;
  }

  function bindInteraction(el: HTMLCanvasElement) {
    abortCtrl = new AbortController();
    const signal = abortCtrl.signal;

    let dragging = false;
    let dragMoved = false;
    let dragStartX = 0;
    let dragStartTime = 0;
    let rangeSelecting = false;
    let rangeAnchorTime = 0;

    const timeAt = (clientX: number) => {
      const r = el.getBoundingClientRect();
      const t = viewState.start + ((clientX - r.left) / r.width) * viewState.duration;
      return Math.max(viewState.minTime, Math.min(viewState.maxTime, t));
    };

    el.addEventListener('wheel', (e) => {
      e.preventDefault();
      const r = el.getBoundingClientRect();
      const frac = (e.clientX - r.left) / r.width;
      const anchor = viewState.start + frac * viewState.duration;
      const factor = e.deltaY > 0 ? 1.15 : 1 / 1.15;
      const dur = Math.max(viewState.minDuration, Math.min(viewState.duration * factor, viewState.maxDuration));
      viewState.start = Math.max(viewState.minTime, Math.min(anchor - frac * dur, viewState.maxTime - dur));
      viewState.duration = dur;
    }, { passive: false, signal });

    el.addEventListener('mousedown', (e) => {
      if (e.button !== 0) return;
      dragMoved = false;
      dragStartX = e.clientX;
      if (cursorMode.value && !e.shiftKey && !e.ctrlKey) return;
      if (e.shiftKey || e.ctrlKey) {
        rangeSelecting = true;
        rangeAnchorTime = timeAt(e.clientX);
        rangeSelection.value = { startTime: rangeAnchorTime, endTime: rangeAnchorTime };
        return;
      }
      rangeSelection.value = null;
      dragging = true;
      dragStartTime = viewState.start;
    }, { signal });

    window.addEventListener('mousemove', (e) => {
      if (Math.abs(e.clientX - dragStartX) > 3 && (rangeSelecting || dragging)) dragMoved = true;
      if (rangeSelecting) {
        const t = timeAt(e.clientX);
        rangeSelection.value = { startTime: Math.min(rangeAnchorTime, t), endTime: Math.max(rangeAnchorTime, t) };
        return;
      }
      if (!dragging) return;
      const r = el.getBoundingClientRect();
      const s = dragStartTime - ((e.clientX - dragStartX) / r.width) * viewState.duration;
      viewState.start = Math.max(viewState.minTime, Math.min(s, viewState.maxTime - viewState.duration));
    }, { signal });

    window.addEventListener('mouseup', (e: MouseEvent) => {
      if (rangeSelecting) {
        rangeSelecting = false;
        const sel = rangeSelection.value;
        if (!sel || sel.endTime - sel.startTime < 1) {
          rangeSelection.value = null;
        } else if (e.ctrlKey) {
          const dur = Math.max(viewState.minDuration, Math.min(sel.endTime - sel.startTime, viewState.maxDuration));
          viewState.duration = dur;
          viewState.start = Math.max(viewState.minTime, Math.min(sel.startTime, viewState.maxTime - dur));
        }
        return;
      }
      dragging = false;
    }, { signal });

    el.addEventListener('mousemove', (e) => {
      if (!rangeSelecting) setHover(e.clientX, e.clientY);
    }, { signal });

    el.addEventListener('mouseleave', clearHover, { signal });

    el.addEventListener('click', (e) => {
      if (e.button !== 0 || dragMoved || e.shiftKey || e.ctrlKey) return;
      if (cursorMode.value) {
        placeCursorAtClientX(e.clientX);
        clearHover();
        return;
      }
      if (view === 'bank') {
        const bh = bankHitTest(e.clientX, e.clientY);
        if (bh && bh.cause >= 0) {
          tl.bankHover.value = null;
          tl.focusCommand(bh.cause, 'bank');
        }
        return;
      }
      const hit = hitTest(e.clientX, e.clientY);
      if (hit) {
        hoveredEvent.value = null;
        tl.navigateToCounterpart(hit, view);
      } else {
        tl.clearSelection();
      }
    }, { signal });

    resizeObs = new ResizeObserver(() => {
      sizeCanvases(el);
      lastLayoutVersion = -1;
      regl?.poll();
    });
    resizeObs.observe(el);
  }

  // ── Lifecycle ─────────────────────────────────────────────────

  onMounted(() => {
    const el = canvas.value;
    if (!el) return;
    sizeCanvases(el);
    bindInteraction(el);
    if (view === 'bank') {
      bankState = tl.bankState.value;
      tl.panelDebug.bank = {
        refreshBankStats() {
          recomputeBankStats();
          return tl.bankStats.value;
        },
        headerTitles: () => Object.fromEntries(bankHeaderTitles),
      };
    }
    if (view === 'command') tl.linkGeometry.cmd = i => linkAnchor('cmd', i);
    if (view === 'request') tl.linkGeometry.req = j => linkAnchor('req', j);
    sync();
  });

  watch(tl.model, sync);

  onUnmounted(() => {
    if (bankStatsTimer) clearTimeout(bankStatsTimer);
    abortCtrl?.abort();
    resizeObs?.disconnect();
    destroyGpu(true);
    clearHover();
    if (view === 'command') delete tl.linkGeometry.cmd;
    if (view === 'request') delete tl.linkGeometry.req;
    if (view === 'bank') {
      tl.bankStats.value = null;
      delete tl.panelDebug.bank;
    }
  });

  if (view === 'command') {
    watch(() => sessionStore.commandColors, () => refreshCommandLookupTextures?.(), { deep: true });
  }
  if (view === 'request') {
    watch(() => sessionStore.requestTypeColors, () => refreshRequestLookupTextures?.(), { deep: true });
  }
  if (view === 'bank') {
    watch(tl.bankState, applyBankState);
    watch(() => [viewState.start, viewState.duration, rangeSelection.value, panel.layoutVersion] as const, scheduleBankStats);
  }

  /** The pointer is over something a click navigates to. */
  const clickable = computed(() => !cursorMode.value && (view === 'bank'
    ? (tl.bankHover.value?.cause ?? -1) >= 0
    : (view === 'command' || view === 'request') && hoveredEvent.value !== null));

  return { clickable };
}
