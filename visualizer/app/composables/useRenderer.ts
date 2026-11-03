// One WebGL renderer per docked panel. Each instance draws a single view; the time axis,
// cursor, range and the trace engine come from the page's shared timeline (useTimeline).
//
// A panel draws the tiles in view from their detail, each placed by its origin, so times stay exact
// at any trace length. When the view holds too many bars, or a tile's detail is still loading, it
// draws the tile summaries as one block per pixel instead (render/heat.ts).
import createREGL from 'regl';
import type { RowLayout, BusType, ViewMode } from '~/stores/ui';
import { nodeId, computeDefaultExpanded, structuralDepth as specStructuralDepth } from '~/utils/traceTree';
import { CMD_PALETTE, defaultCmdColor, hex2rgb } from '~/utils/commandColors';
import type { RequestTypeStoredColors } from '~/utils/commandColorStorage';
import {
  BANK_COLORS,
  bankCoordsName,
  bankCoordsRange,
  bankDomOf,
  bankKindColor,
  bankKindName,
  bankLaneLabel,
  bankLanePath,
  BANK_KIND,
} from '~/utils/bankState';
import { bankTitle, createBankLayer, type BankLayer, type BankTileGpu } from '~/composables/bankLayer';
import type { TraceEngine } from '~/engine/client';
import { hiddenLane } from '~/engine/lanes';
import { bankWindow, busCycles, coverageWindow, lowerBound, recordEnd, recordStart } from '~/engine/stats';
import { burstCycles, commandBusCount, commandDuration, dataBusCount, dataBusTiming, maxCommandDuration } from '~/engine/timing';
import { REQ_STACKS_DRAWN, SUBBINS, TILE, type IntervalPieces, type TileDetail, type TileSummary } from '~/engine/types';
import { buildHeat, createHeatLayer, type HeatColors, type HeatLayer, type HeatRow } from '~/render/heat';
import type {
  BankHover,
  BankShares,
  HitResult,
  LinkAnchor,
  LinkedSelection,
  ThroughputHover,
} from '~/composables/useTimeline';
import { PAPER } from '~/utils/theme';
import { axisTicks } from '~/utils/timeTicks';

// ── Constants ───────────────────────────────────────────────────────

const ROW_HEIGHT = 16;
/** Request bars: height, gap between stacked slots, slots shown per bank (canvas + sidebar must agree). */
const REQ_BAR_HEIGHT = 10;
const REQ_STACK_GAP = 2;
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
/**
 * Bars drawn one by one up to this many in view; beyond it, blocks from the tile summaries. A
 * software WebGL renderer pays per instance, so it switches to blocks sooner.
 */
const INSTANCE_LIMIT = 400_000;
const INSTANCE_LIMIT_SOFTWARE = 40_000;
/** Half-width of the box around a selected command's marker in Bank state (markers reach 6.5 px). */
const BANK_MARKER_HALF_PX = 7;
/** Opacity of the blocks standing in for tiles whose detail is still loading. */
const HEAT_LOADING_ALPHA = 0.3;
/** Bank markers are drawn up to this many commands in view. */
const MARKER_LIMIT = 50_000;
const MARKER_LIMIT_SOFTWARE = 10_000;
/** Renderer strings of WebGL implementations that run on the CPU. */
const SOFTWARE_GL = /swiftshader|llvmpipe|softpipe|software|basic render/i;
/** GPU memory per panel for tile buffers. */
const GPU_BUDGET = 192 * 2 ** 20;
const VGRID_COLOR = PAPER.vgrid;
const RANGE_FILL = PAPER.rangeFill;
const RANGE_EDGE = PAPER.rangeEdge;
const CURSOR_COLOR = PAPER.cursor;
const CURSOR_FILL = PAPER.cursorFill;
/** Keep throughput point count bounded at wide zoom levels. */
const TP_MAX_DRAW_POINTS = 480;
const TP_POINT_RADIUS = 2.3;
const TP_HOVER_RADIUS = 8;
const TP_MARKER_MIN_GAP_PX = 9;
/** Throughput charts span at least 0–4%, so a quiet window keeps whole-percent ticks. */
const TP_MIN_SCALE = 0.04;

/** Running maximum of t + dur. */
function prefixMaxEnd(t: Float32Array, dur: Float32Array): Float32Array {
  const out = new Float32Array(t.length);
  let m = -Infinity;
  for (let j = 0; j < t.length; j++) {
    m = Math.max(m, t[j]! + dur[j]!);
    out[j] = m;
  }
  return out;
}

// ── Shaders ─────────────────────────────────────────────────────────
// Instance times are offsets from their tile's origin; u_oInt + u_oFrac = origin - view start,
// split so that the sum stays exact in single precision.

const TRACE_VERT = `
precision highp float;
attribute vec2 a_quad;
attribute float a_t;
attribute float a_cmdId;
attribute float a_laneIdx;
uniform sampler2D u_cmdLookup;
uniform sampler2D u_lanes;
uniform float u_oInt;
uniform float u_oFrac;
uniform float u_viewW;
uniform vec2 u_resolution;
uniform float u_rowHeight;
uniform float u_laneCount;
/** >= 0: draw every instance on this row (CMD BUS summary lane) instead of its own lane. */
uniform float u_forceY;
uniform float u_hover;
varying vec3 v_color;
varying vec2 v_quad;
varying float v_duration;
void main() {
  vec4 cmdProps = texture2D(u_cmdLookup, vec2((a_cmdId + 0.5) / 256.0, 0.5));
  v_color = cmdProps.rgb;
  // At least 1 px wide, so commands stay visible when zoomed out.
  float duration = max(cmdProps.a, u_viewW / u_resolution.x);
  v_quad = a_quad;
  v_duration = duration;
  float x = (a_t + u_oInt) + u_oFrac + a_quad.x * duration;
  float yCenter = u_forceY >= 0.0 ? u_forceY : texture2D(u_lanes, vec2((a_laneIdx + 0.5) / u_laneCount, 0.5)).r;
  float yScreen = yCenter + (a_quad.y - 0.5) * u_rowHeight;
  gl_Position = vec4(x / u_viewW * 2.0 - 1.0, 1.0 - (yScreen / u_resolution.y) * 2.0, 0.0, 1.0);
}
`;

const TRACE_FRAG = `
precision highp float;
varying vec3 v_color;
void main() {
  gl_FragColor = vec4(v_color, 1.0);
}
`;

const REQ_VERT = `
precision highp float;
attribute vec2 a_quad;
attribute float a_t;
attribute float a_dur;
attribute float a_queue;
attribute float a_lane;
attribute float a_stack;
attribute float a_flags;
uniform sampler2D u_typeLookup;
uniform sampler2D u_slots;
uniform float u_slotCount;
uniform float u_oInt;
uniform float u_oFrac;
uniform float u_viewW;
uniform vec2 u_resolution;
uniform float u_rowHeight;
varying vec3 v_color;
varying vec2 v_quad;
varying float v_duration;
varying float v_queueDelay;
varying float v_inFlight;
void main() {
  float type = mod(a_flags, 2.0) > 0.5 ? 1.0 : (a_flags >= 4.0 ? 2.0 : 0.0);
  v_color = texture2D(u_typeLookup, vec2((type + 0.5) / 256.0, 0.5)).rgb;
  v_quad = a_quad;
  v_duration = a_dur;
  v_queueDelay = a_queue;
  v_inFlight = mod(floor(a_flags / 2.0), 2.0);
  float slot = a_lane * ${REQ_STACKS_DRAWN.toFixed(1)} + min(a_stack, ${(REQ_STACKS_DRAWN - 1).toFixed(1)});
  float yCenter = texture2D(u_slots, vec2((slot + 0.5) / u_slotCount, 0.5)).r;
  float x = (a_t + u_oInt) + u_oFrac + a_quad.x * max(a_dur, u_viewW / u_resolution.x);
  float yScreen = yCenter + (a_quad.y - 0.5) * u_rowHeight;
  gl_Position = vec4(x / u_viewW * 2.0 - 1.0, 1.0 - (yScreen / u_resolution.y) * 2.0, 0.0, 1.0);
}
`;

const REQ_FRAG = `
precision highp float;
varying vec3 v_color;
varying vec2 v_quad;
varying float v_duration;
varying float v_queueDelay;
varying float v_inFlight;
void main() {
  float qf = clamp(v_queueDelay / max(v_duration, 1e-6), 0.0, 1.0);
  float alpha = v_quad.x < qf ? 0.8 : 1.0;
  // Unfinished at the end of the trace: diagonal hatching.
  if (v_inFlight > 0.5) alpha *= mix(0.3, 1.0, step(0.5, fract((gl_FragCoord.x + gl_FragCoord.y) / 8.0)));
  gl_FragColor = vec4(v_color, alpha);
}
`;

const HOVER_FRAG = `
precision highp float;
varying vec2 v_quad;
varying float v_duration;
uniform float u_viewW;
uniform vec2 u_resolution;
uniform float u_rowHeight;
void main() {
  float barWpx = (v_duration / max(u_viewW, 1e-6)) * u_resolution.x;
  float edgeX = min(v_quad.x, 1.0 - v_quad.x) * barWpx;
  float edgeY = min(v_quad.y, 1.0 - v_quad.y) * u_rowHeight;
  float edgeDist = min(edgeX, edgeY);
  float border = 1.0 - step(1.5, edgeDist);
  float tint = 0.28 * step(1.5, edgeDist);
  gl_FragColor = vec4(0.122, 0.137, 0.157, max(border, tint * 0.45));
}
`;

// ── Pure helpers ────────────────────────────────────────────────────

function bankShares(acc: { banks: number; t0: number; t1: number; starved: number; bus: number; bg: number; act: number; pre: number; idle: number; wrrec: number; ref: number }): BankShares {
  const tot = acc.banks * (acc.t1 - acc.t0);
  const p = (x: number) => (tot > 0 ? (x / tot) * 100 : 0);
  return { starved: p(acc.starved), bus: p(acc.bus), bg: p(acc.bg), act: p(acc.act), pre: p(acc.pre), idle: p(acc.idle), wrrec: p(acc.wrrec), ref: p(acc.ref) };
}

function cmdTextureData(names: string[], meta: { isOpening: boolean; isClosing: boolean; isAccessing: boolean; isRefreshing: boolean }[], userColors: Record<number, string>, durations: ArrayLike<number>): Float32Array {
  const data = new Float32Array(256 * 4);
  const [fr, fg, fb] = hex2rgb(CMD_PALETTE.fallback);
  for (let i = 0; i < 256; i++) data.set([fr, fg, fb, 1], i * 4);
  for (let id = 0; id < names.length; id++) {
    const [r, g, b] = hex2rgb(userColors[id] ?? defaultCmdColor(names[id]!, meta[id] as any));
    data.set([r, g, b, durations[id] ?? 1], id * 4);
  }
  return data;
}

function reqTextureData(colors: RequestTypeStoredColors): Float32Array {
  const data = new Float32Array(256 * 4);
  const set = (i: number, hex: string) => {
    const [r, g, b] = hex2rgb(hex);
    data.set([r, g, b, 1], i * 4);
  };
  for (let i = 0; i < 256; i++) set(i, colors.maintenance);
  set(0, colors.read);
  set(1, colors.write);
  return data;
}

/** Canvas y of the hidden lane: far above the canvas, so bars there are never drawn or hit. */
const HIDDEN_Y = -1e6;

/** Lane y-centres of `busType` rows (strides tile the lanes; one extra entry for the hidden lane). */
function laneLookup(rows: RowLayout[], strides: number[], totalLanes: number, canvasTop: number, busType: BusType): Float32Array {
  const data = new Float32Array((totalLanes + 1) * 4);
  data[hiddenLane(totalLanes) * 4] = HIDDEN_Y;
  for (const row of rows) {
    if (row.busType !== busType || row.depth === 0 || row.depth > strides.length) continue;
    const yCenter = row.top + row.height / 2 - canvasTop;
    let base = 0;
    for (let k = 0; k < row.depth; k++) base += row.path[k]! * strides[k]!;
    const range = strides[row.depth - 1]!;
    for (let j = 0; j < range; j++) data[(base + j) * 4] = yCenter;
  }
  return data;
}

/** y-centres of request slots (lane * REQ_STACKS_DRAWN + stack). */
function slotLookup(rows: RowLayout[], strides: number[], totalLanes: number, canvasTop: number, maxRows: Uint16Array): Float32Array {
  const data = new Float32Array(totalLanes * REQ_STACKS_DRAWN * 4);
  const pitch = REQ_BAR_HEIGHT + REQ_STACK_GAP;
  for (const row of rows) {
    if (row.busType !== 'request' || row.depth === 0 || row.depth > strides.length) continue;
    let base = 0;
    for (let k = 0; k < row.depth; k++) base += row.path[k]! * strides[k]!;
    const range = strides[row.depth - 1]!;
    // A collapsed group row overlays all of its lanes' requests within its own height.
    const leaf = row.depth === strides.length;
    const rowTop = row.top - canvasTop;
    for (let j = 0; j < range; j++) {
      const lane = base + j;
      const stacks = Math.min(REQ_STACKS_DRAWN, Math.max(1, maxRows[lane] ?? 1));
      for (let s = 0; s < REQ_STACKS_DRAWN; s++) {
        const slot = Math.min(s, stacks - 1);
        data[(lane * REQ_STACKS_DRAWN + s) * 4] = leaf ? rowTop + REQ_ROW_PAD + slot * pitch + REQ_BAR_HEIGHT * 0.5 : rowTop + row.height * 0.5;
      }
    }
  }
  return data;
}

/** Lanes [first, first + count) under a tree path. */
function laneRangeOf(path: number[], strides: number[], totalLanes: number): [number, number] {
  let first = 0;
  for (let k = 0; k < path.length; k++) first += path[k]! * strides[k]!;
  return [first, strides[path.length - 1] ?? totalLanes];
}

/** Interval piece of domain d containing time x (relative to the tile origin), or the gap around it. */
function pieceAt(p: IntervalPieces | null, d: number, x: number): { inside: boolean; q: number; prevEnd: number; nextStart: number } {
  if (!p) return { inside: false, q: -1, prevEnd: Number.NEGATIVE_INFINITY, nextStart: Number.POSITIVE_INFINITY };
  let prevEnd = Number.NEGATIVE_INFINITY;
  for (let q = lowerBound(p.dom, d); q < p.dom.length && p.dom[q] === d; q++) {
    if (p.s[q]! > x) return { inside: false, q: -1, prevEnd, nextStart: p.s[q]! };
    if (x < p.e[q]!) return { inside: true, q, prevEnd, nextStart: Number.POSITIVE_INFINITY };
    prevEnd = p.e[q]!;
  }
  return { inside: false, q: -1, prevEnd, nextStart: Number.POSITIVE_INFINITY };
}

interface CmdTileGpu {
  detail: TileDetail;
  bytes: number;
  cmd: { t: createREGL.Buffer; id: createREGL.Buffer; lane: createREGL.Buffer; n: number };
  fan: { t: createREGL.Buffer; id: createREGL.Buffer; lane: createREGL.Buffer; n: number };
  bus: { t: createREGL.Buffer; id: createREGL.Buffer; lane: createREGL.Buffer; n: number };
  destroy(): void;
}

interface ReqTileGpu {
  detail: TileDetail;
  bytes: number;
  t: createREGL.Buffer;
  dur: createREGL.Buffer;
  queue: createREGL.Buffer;
  lane: createREGL.Buffer;
  stack: createREGL.Buffer;
  flags: createREGL.Buffer;
  n: number;
  /** Latest end among requests 0..j (tile-relative): the requests before the first one ending in view are skipped. */
  endMax: Float32Array;
  destroy(): void;
}

type TileGpu = CmdTileGpu | ReqTileGpu | BankTileGpu;

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
  let removeDraw: (() => void) | null = null;

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

  // ── Engine and GPU state ───────────────────────────────────────
  let engine: TraceEngine | null = null;
  let gpuEngine: TraceEngine | null = null;
  let strides: number[] = [];
  let totalLanes = 1;
  let maxCmdDur = 1;
  let maxBurst = 4;
  /** WebGL runs on the CPU here (no GPU, or GPU blocklisted): draw fewer instances. */
  let softwareGl = false;

  let cmdBusVisible = false;
  /** Canvas y of the CMD BUS summary row (Commands view), null when not shown. */
  let cmdSumY: number | null = null;
  let dataBusVisible = false;
  let reqBusVisible = false;
  let lastLayoutVersion = -1;
  let cmdLaneY: Float32Array | null = null;
  let dataLaneY: Float32Array | null = null;
  let reqSlotY: Float32Array | null = null;

  let cmdLookupTex: createREGL.Texture2D | null = null;
  let dataLookupTex: createREGL.Texture2D | null = null;
  let cmdLanesTex: createREGL.Texture2D | null = null;
  let dataLanesTex: createREGL.Texture2D | null = null;
  let reqLookupTex: createREGL.Texture2D | null = null;
  let reqSlotsTex: createREGL.Texture2D | null = null;
  let drawTrace: createREGL.DrawCommand | null = null;
  let drawTraceHover: createREGL.DrawCommand | null = null;
  let drawReq: createREGL.DrawCommand | null = null;
  let drawReqHover: createREGL.DrawCommand | null = null;
  let heat: HeatLayer | null = null;
  let heatKey = '';
  let heatBuiltAt = 0;
  let bankLayer: BankLayer | null = null;
  const gpuTiles = new Map<number, TileGpu>();
  let gpuBytes = 0;

  // ── Bank State statistics ──────────────────────────────────────
  /** Panel titles per sidebar header row (coords.join(',')), refreshed by recomputeBankStats. */
  const bankHeaderTitles = new Map<string, string>();
  let bankStatsTimer: ReturnType<typeof setTimeout> | null = null;
  let bankStatsAgain = false;
  let bankStatsVersion = -1;

  // ── Layout ────────────────────────────────────────────────────

  function refreshLayout() {
    const el = canvas.value;
    const e = engine;
    if (!el || !e || !regl) return;
    const canvasTop = el.getBoundingClientRect().top;
    const rows = panel.rowLayout;
    const set = (tex: createREGL.Texture2D | null, width: number, data: Float32Array) => {
      (tex as any)?.({ width: Math.max(width, 1), height: 1, data, format: 'rgba', type: 'float', min: 'nearest', mag: 'nearest' });
    };
    if (view === 'command') {
      cmdBusVisible = rows.some(r => r.busType === 'command' || r.busType === 'cmd_sum');
      const sumRow = rows.find(r => r.busType === 'cmd_sum');
      cmdSumY = sumRow ? sumRow.top + sumRow.height / 2 - canvasTop : null;
      dataBusVisible = rows.some(r => r.busType === 'data');
      cmdLaneY = laneLookup(rows, strides, totalLanes, canvasTop, 'command');
      dataLaneY = laneLookup(rows, strides, totalLanes, canvasTop, 'data');
      set(cmdLanesTex, totalLanes + 1, cmdLaneY);
      set(dataLanesTex, totalLanes + 1, dataLaneY);
    } else if (view === 'request') {
      reqBusVisible = rows.some(r => r.busType === 'request');
      reqSlotY = slotLookup(rows, strides, totalLanes, canvasTop, e.maxRows);
      set(reqSlotsTex, totalLanes * REQ_STACKS_DRAWN, reqSlotY);
    } else if (view === 'bank') {
      bankLayer?.setLayout(rows, canvasTop);
    }
    heatKey = '';
  }

  /** Summary rows of this panel, for zoomed-out blocks. */
  function heatRows(canvasTop: number): HeatRow[] {
    const e = engine!;
    const out: HeatRow[] = [];
    const L = e.ctx.bank?.L;
    for (const r of panel.rowLayout) {
      const y = r.top - canvasTop;
      if (y > cssH || y + r.height < 0) continue;
      if (view === 'command' || view === 'request') {
        if (r.busType === 'cmd_sum') {
          out.push({ kind: 'cmdsum', y: y + r.height / 2 - ROW_HEIGHT / 2, h: ROW_HEIGHT, first: 0, count: 0 });
          continue;
        }
        if (r.busType !== 'command' && r.busType !== 'data' && r.busType !== 'request') continue;
        if (r.depth === 0 || r.depth > strides.length) continue;
        const [first, count] = laneRangeOf(r.path, strides, totalLanes);
        const kind = r.busType === 'command' ? 'cmd' : r.busType === 'data' ? 'data' : 'req';
        const h = kind === 'req' ? Math.max(REQ_BAR_HEIGHT, r.height - 2 * REQ_ROW_PAD) : ROW_HEIGHT;
        out.push({ kind, y: kind === 'req' ? y + REQ_ROW_PAD : y + r.height / 2 - h / 2, h, first, count });
      } else if (view === 'bank' && L) {
        if (r.busType === 'bank') {
          const inset = Math.max(1, Math.round(r.height * 0.06));
          out.push({ kind: 'bank', y: y + inset, h: r.height - 2 * inset, first: r.path[0]!, count: 1 });
        } else if (r.busType === 'bk_bus' || r.busType === 'bk_faw') {
          const inset = Math.max(1, Math.round(r.height * 0.06));
          out.push({ kind: r.busType === 'bk_bus' ? 'bkbus' : 'bkfaw', y: y + inset, h: r.height - 2 * inset, first: bankCoordsRange(L, r.path).dom, count: 1 });
        }
      }
    }
    return out;
  }

  function heatColors(): HeatColors {
    const spec = engine!.spec;
    const c = (id: number): [number, number, number] => hex2rgb(sessionStore.commandColors[id] ?? defaultCmdColor(spec.commandNames[id] ?? '', spec.commandMeta[id] ?? ({} as any)));
    return {
      cmd: id => (id < spec.commandNames.length ? c(id) : hex2rgb(CMD_PALETTE.fallback)),
      read: hex2rgb(CMD_PALETTE.readAccess),
      write: hex2rgb(CMD_PALETTE.writeAccess),
      other: hex2rgb('#52525b'),
      data: hex2rgb(CMD_PALETTE.readAccess),
      request: hex2rgb(sessionStore.requestTypeColors.read),
    };
  }

  // ── Tiles in view ─────────────────────────────────────────────

  /** Records drawn in [t0, t1): those intersecting it, plus neighbours whose bars reach into it. */
  function visibleRecords(e: TraceEngine, t0: number, t1: number): number[] {
    let [i0, i1] = e.range(t0, t1);
    while (i0 > 0 && e.tiles[i0 - 1]!.maxEnd > t0) i0--;
    while (i1 < e.tiles.length && e.tiles[i1]!.minStart < t1) i1++;
    const out: number[] = [];
    for (let i = i0; i < i1; i++) out.push(i);
    return out;
  }

  /** Commands of a record in the sub-bins the view covers (only the part of a tile in view is drawn). */
  function commandsInView(r: TileSummary): number {
    const binW = (r.span * TILE) / SUBBINS;
    const b0 = Math.min(SUBBINS - 1, Math.max(0, Math.floor((viewState.start - r.k * TILE) / binW)));
    const b1 = Math.min(SUBBINS - 1, Math.max(b0, Math.floor((viewState.start + viewState.duration - r.k * TILE) / binW)));
    let c = 0;
    for (let b = b0; b <= b1; b++) c += r.subReads[b]! + r.subWrites[b]! + r.subOthers[b]!;
    return c;
  }

  /** Instances drawn for the view: from each record, the share of its commands in view. */
  function instancesIn(e: TraceEngine, recs: number[]): number {
    let n = 0;
    for (const i of recs) {
      const r = e.tiles[i]!;
      const all = view === 'command' ? r.nCmd + r.nFan + r.nBurst : view === 'request' ? r.nReq : view === 'bank' ? r.nCmd * 3 : 0;
      n += r.nCmd ? Math.ceil((all * commandsInView(r)) / r.nCmd) : all;
    }
    return n;
  }

  function instanceLimit(): number {
    return softwareGl ? INSTANCE_LIMIT_SOFTWARE : INSTANCE_LIMIT;
  }

  function evict() {
    while (gpuBytes > GPU_BUDGET && gpuTiles.size > 1) {
      const [k, g] = gpuTiles.entries().next().value!;
      gpuTiles.delete(k);
      gpuBytes -= g.bytes;
      g.destroy();
    }
  }

  function clearTiles() {
    for (const g of gpuTiles.values()) g.destroy();
    gpuTiles.clear();
    gpuBytes = 0;
  }

  /** GPU buffers of record k's detail (uploaded on first use; re-uploaded when the detail changes). */
  function tileGpu(d: TileDetail): TileGpu | null {
    const r = regl;
    if (!r) return null;
    const have = gpuTiles.get(d.k);
    if (have && have.detail === d) {
      gpuTiles.delete(d.k);
      gpuTiles.set(d.k, have);
      return have;
    }
    if (have) {
      gpuTiles.delete(d.k);
      gpuBytes -= have.bytes;
      have.destroy();
    }
    const bufs: createREGL.Buffer[] = [];
    const buf = (data: ArrayBufferView, type: 'float' | 'uint8' | 'uint16') => {
      const b = r.buffer({ data: data.byteLength ? data as any : new (data.constructor as any)(1), type });
      bufs.push(b);
      return b;
    };
    let g: TileGpu;
    if (view === 'command') {
      g = {
        detail: d,
        bytes: (d.cmdT.length + d.fanT.length + d.busT.length) * 7,
        cmd: { t: buf(d.cmdT, 'float'), id: buf(d.cmdId, 'uint8'), lane: buf(d.cmdLane, 'uint16'), n: d.cmdT.length },
        fan: { t: buf(d.fanT, 'float'), id: buf(d.fanCmd, 'uint8'), lane: buf(d.fanLane, 'uint16'), n: d.fanT.length },
        bus: { t: buf(d.busT, 'float'), id: buf(d.busCmd, 'uint8'), lane: buf(d.busLane, 'uint16'), n: d.busT.length },
        destroy: () => bufs.forEach(b => b.destroy()),
      };
    } else if (view === 'request') {
      g = {
        detail: d,
        bytes: d.reqT.length * 21,
        t: buf(d.reqT, 'float'),
        dur: buf(d.reqDur, 'float'),
        queue: buf(d.reqQueue, 'float'),
        lane: buf(d.reqLane, 'uint16'),
        stack: buf(d.reqStack, 'uint8'),
        flags: buf(d.reqFlags, 'uint8'),
        n: d.reqT.length,
        endMax: prefixMaxEnd(d.reqT, d.reqDur),
        destroy: () => bufs.forEach(b => b.destroy()),
      };
    } else if (view === 'bank' && bankLayer) {
      g = bankLayer.upload(d);
    } else {
      return null;
    }
    gpuTiles.set(d.k, g);
    gpuBytes += g.bytes;
    evict();
    return g;
  }

  /** Uniforms placing a tile whose origin is `origin`. */
  /** A tile's origin relative to the view start (split for the shaders), and cycles per CSS pixel. */
  function place(origin: number): { oInt: number; oFrac: number; viewW: number; px: number } {
    const d = origin - viewState.start;
    const oInt = Math.floor(d);
    return { oInt, oFrac: d - oInt, viewW: viewState.duration, px: viewState.duration / cssW };
  }

  /** Instances [a, b) of a tile's time-sorted set that can show in view; `back` is the longest bar. */
  function inView(t: Float32Array, d: TileDetail, back: number): [number, number] {
    const t0 = viewState.start - d.origin;
    const px = viewState.duration / cssW;
    const a = lowerBound(t, t0 - back - px);
    return [a, lowerBound(t, t0 + viewState.duration + px, a)];
  }

  // ── Hit testing ───────────────────────────────────────────────

  function timeAtClientX(clientX: number): number {
    const rect = canvas.value!.getBoundingClientRect();
    return viewState.start + ((clientX - rect.left) / rect.width) * viewState.duration;
  }

  function cmdHit(e: TraceEngine, d: TileDetail, j: number, bus: BusType, duration: number, busClk?: number): HitResult {
    const L = e.ctx.levels;
    return {
      index: d.cmdFirst + j,
      cmdId: d.cmdId[j]!,
      clk: d.rawClk[j]!,
      typeId: d.rawType[j]!,
      sourceId: d.rawSrc[j]!,
      addr: Array.from(d.rawAddr.subarray(j * L, (j + 1) * L)),
      bus,
      duration,
      reqId: d.rawReqId[j]!,
      ...(busClk !== undefined ? { busClk } : {}),
    };
  }

  /** Detail of the record holding trace entry gidx (only if loaded). */
  function detailOfEntry(e: TraceEngine, gidx: number): TileDetail | undefined {
    const i = e.recordOfEntry(gidx);
    return i >= 0 ? e.peek(e.tiles[i]!.k) : undefined;
  }

  function hitTest(clientX: number, clientY: number): HitResult | null {
    const e = engine;
    const el = canvas.value;
    if (!e || !el || (view !== 'command' && view !== 'request')) return null;
    const rect = el.getBoundingClientRect();
    const cy = clientY - rect.top;
    const t = timeAtClientX(clientX);
    const minDur = viewState.duration / rect.width;
    const recs = visibleRecords(e, t - Math.max(maxCmdDur, maxBurst, minDur) - 1, t + 1);
    if (instancesIn(e, visibleRecords(e, viewState.start, viewState.start + viewState.duration)) > instanceLimit()) return null;

    if (view === 'request') {
      if (!reqBusVisible || !reqSlotY) return null;
      for (const i of recs) {
        const d = e.peek(e.tiles[i]!.k);
        if (!d) continue;
        const x = t - d.origin;
        for (let j = 0, n = lowerBound(d.reqT, x + 1e-9); j < n; j++) {
          const dur = Math.max(d.reqDur[j]!, minDur);
          if (x < d.reqT[j]! || x > d.reqT[j]! + dur) continue;
          const slot = d.reqLane[j]! * REQ_STACKS_DRAWN + Math.min(d.reqStack[j]!, REQ_STACKS_DRAWN - 1);
          if (Math.abs(cy - reqSlotY[slot * 4]!) > REQ_BAR_HEIGHT / 2 + 1) continue;
          return tl.hitForRequest(d.k, j);
        }
      }
      return null;
    }

    const spec = e.spec;
    // CMD BUS summary row, then each command on its lane, multi-bank instances, data bursts.
    for (const i of recs) {
      const d = e.peek(e.tiles[i]!.k);
      if (!d) continue;
      const x = t - d.origin;
      const lo = lowerBound(d.cmdT, x - Math.max(maxCmdDur, minDur));
      const hi = lowerBound(d.cmdT, x + 1e-9);
      for (let j = hi - 1; j >= lo; j--) {
        const dur = commandDuration(spec, d.cmdId[j]!);
        if (x < d.cmdT[j]! || x > d.cmdT[j]! + Math.max(dur, minDur)) continue;
        const onSum = cmdSumY !== null && Math.abs(cy - cmdSumY) <= ROW_HEIGHT / 2;
        const onLane = cmdBusVisible && cmdLaneY && Math.abs(cy - cmdLaneY[d.cmdLane[j]! * 4]!) <= ROW_HEIGHT / 2;
        if (onSum || onLane) return cmdHit(e, d, j, 'command', dur);
      }
      if (cmdBusVisible && cmdLaneY) {
        for (let j = lowerBound(d.fanT, x - Math.max(maxCmdDur, minDur)); j < d.fanT.length && d.fanT[j]! <= x; j++) {
          const dur = commandDuration(spec, d.fanCmd[j]!);
          if (x > d.fanT[j]! + Math.max(dur, minDur)) continue;
          if (Math.abs(cy - cmdLaneY[d.fanLane[j]! * 4]!) > ROW_HEIGHT / 2) continue;
          const g = d.fanIdx[j]!;
          const dd = detailOfEntry(e, g);
          if (dd) return cmdHit(e, dd, g - dd.cmdFirst, 'command', dur);
        }
      }
      if (dataBusVisible && dataLaneY) {
        const burst = burstCycles(spec);
        for (let j = lowerBound(d.busT, x - Math.max(maxBurst, minDur)); j < d.busT.length && d.busT[j]! <= x; j++) {
          const dur = burst[d.busCmd[j]!]!;
          if (x > d.busT[j]! + Math.max(dur, minDur)) continue;
          if (Math.abs(cy - dataLaneY[d.busLane[j]! * 4]!) > ROW_HEIGHT / 2) continue;
          const g = d.busIdx[j]!;
          const dd = detailOfEntry(e, g);
          if (dd) return cmdHit(e, dd, g - dd.cmdFirst, 'data', dur, d.origin + d.busT[j]!);
        }
      }
    }
    return null;
  }

  // ── Throughput ────────────────────────────────────────────────

  interface TpPoint { x: number; a: number; b: number; data: number; cmd: number }
  let tpCache: { key: string; points: TpPoint[]; nodes: Map<string, number[]>; nodeMax: Map<number, number> } | null = null;

  /** Data-bus cycles of lanes [first, first + count) in [a, b). */
  function laneBusy(e: TraceEngine, a: number, b: number, first: number, count: number): number {
    const [i0, i1] = e.range(a, b);
    let sum = 0;
    for (let i = i0; i < i1; i++) {
      const r = e.tiles[i]!;
      const d = recordStart(r) >= a && recordEnd(r) <= b ? undefined : e.detail(r.k);
      if (d) {
        for (let j = lowerBound(d.busT, a - d.origin), z = lowerBound(d.busT, b - d.origin); j < z; j++) {
          const l = d.busLane[j]!;
          if (l >= first && l < first + count) sum += d.busLen[j]!;
        }
        continue;
      }
      const w = (r.span * TILE) / SUBBINS;
      for (let sb = 0; sb < SUBBINS; sb++) {
        const s0 = recordStart(r) + sb * w;
        const f = Math.max(0, Math.min(b, s0 + w) - Math.max(a, s0)) / w;
        if (f <= 0) continue;
        for (let l = first; l < first + count; l++) sum += f * r.laneBusy[l * SUBBINS + sb]!;
      }
    }
    return sum;
  }

  function throughputPoints(e: TraceEngine, W: number): { points: TpPoint[]; nodes: Map<string, number[]>; nodeMax: Map<number, number> } {
    const rows = panel.rowLayout.filter(r => r.busType === 'tp_node');
    const key = `${viewState.start}|${viewState.duration}|${W}|${e.version}|${rows.map(r => r.path.join('.')).join(',')}`;
    if (tpCache?.key === key) return tpCache;
    const nBL = Math.max(dataBusTiming(e.spec).burst, ...burstCycles(e.spec));
    const maxPoints = Math.max(120, Math.min(TP_MAX_DRAW_POINTS, Math.floor(W * 0.45)));
    const bin = Math.max(nBL * 32, viewState.duration / maxPoints);
    const t0 = Math.max(viewState.minTime, Math.floor(viewState.start / bin) * bin);
    const t1 = Math.min(viewState.maxTime, viewState.start + viewState.duration);
    const dataBuses = dataBusCount(e.spec);
    const cmdBuses = commandBusCount(e.spec);
    const points: TpPoint[] = [];
    for (let a = t0; a < t1; a += bin) {
      const b = Math.min(a + bin, viewState.maxTime);
      if (b <= a) break;
      const c = busCycles(e, e.ctx, a, b);
      points.push({ x: (((a + b) / 2 - viewState.start) / viewState.duration) * W, a, b, data: Math.min(1, c.data / ((b - a) * dataBuses)), cmd: Math.min(1, c.cmd / ((b - a) * cmdBuses)) });
    }
    const nodes = new Map<string, number[]>();
    const nodeMax = new Map<number, number>();
    for (const r of rows) {
      const [first, count] = laneRangeOf(r.path, strides, totalLanes);
      const v = points.map(p => Math.min(1, laneBusy(e, p.a, p.b, first, count) / (p.b - p.a)));
      nodes.set(r.path.join('.'), v);
      nodeMax.set(r.path.length, Math.max(nodeMax.get(r.path.length) ?? 0.01, ...v));
    }
    tpCache = { key, points, nodes, nodeMax };
    return tpCache;
  }

  function nodeLabel(path: number[]): string {
    const names = sessionStore.spec?.levelNames ?? [];
    const from = names[0] === 'Channel' ? 1 : 0;
    return path.slice(from).map((v, i) => `${names[from + i]} ${v}`).join(' · ');
  }

  function chartGeometry(areaTop: number, areaH: number) {
    const pad = 8;
    return { chartTop: areaTop + pad, chartH: areaH - pad * 2 };
  }

  function hitTestThroughput(clientX: number, clientY: number): ThroughputHover | null {
    const el = canvas.value;
    const e = engine;
    if (!el || !e || view !== 'throughput') return null;
    const rect = el.getBoundingClientRect();
    if (clientX < rect.left || clientX > rect.right || clientY < rect.top || clientY > rect.bottom) return null;
    const { points, nodes } = throughputPoints(e, rect.width);
    if (points.length === 0) return null;
    const cx = clientX - rect.left;
    const cy = clientY - rect.top;
    const rows = panel.rowLayout;
    const node = rows.find(r => r.busType === 'tp_node' && clientY >= r.top && clientY < r.top + r.height);
    if (node) {
      let best = 0;
      for (let i = 1; i < points.length; i++) if (Math.abs(points[i]!.x - cx) < Math.abs(points[best]!.x - cx)) best = i;
      const p = points[best]!;
      return { series: 'node', value: nodes.get(node.path.join('.'))?.[best] ?? 0, binStartTime: p.a, binEndTime: p.b, path: node.path, label: nodeLabel(node.path) };
    }
    for (const series of ['data', 'cmd'] as const) {
      const row = rows.find(r => r.busType === (series === 'data' ? 'tp_data' : 'tp_cmd'));
      if (!row || clientY < row.top || clientY > row.top + row.height) continue;
      const { chartTop, chartH } = chartGeometry(row.top - rect.top, row.height);
      const yMax = Math.max(TP_MIN_SCALE, ...points.map(p => p[series]));
      let best = -1;
      let bestD2 = TP_HOVER_RADIUS * TP_HOVER_RADIUS;
      points.forEach((p, i) => {
        const y = chartTop + chartH - (p[series] / yMax) * chartH;
        const d2 = (p.x - cx) ** 2 + (y - cy) ** 2;
        if (d2 <= bestD2) {
          bestD2 = d2;
          best = i;
        }
      });
      if (best < 0) return null;
      const p = points[best]!;
      return { series, value: p[series], binStartTime: p.a, binEndTime: p.b };
    }
    return null;
  }

  // ── Bank State hover and statistics ───────────────────────────

  function bankHitTest(clientX: number, clientY: number): BankHover | null {
    const el = canvas.value;
    const e = engine;
    const b = e?.ctx.bank;
    if (!el || !e || !b || view !== 'bank') return null;
    const rect = el.getBoundingClientRect();
    if (clientX < rect.left || clientX > rect.right || clientY < rect.top || clientY > rect.bottom) return null;
    const row = panel.rowLayout.find(r => clientY >= r.top && clientY < r.top + r.height
      && (r.busType === 'bank' || r.busType === 'bk_bus' || r.busType === 'bk_faw'));
    if (!row) return null;
    const t = timeAtClientX(clientX);
    const ri = e.recordIndexAt(t);
    if (ri < 0) return null;
    const d = e.detail(e.tiles[ri]!.k);
    if (!d) return null;
    const L = b.L;
    const x = t - d.origin;
    const causeInfo = (g: number) => {
      const dd = g >= 0 ? detailOfEntry(e, g) : undefined;
      return dd ? { causeName: e.spec.commandNames[dd.cmdId[g - dd.cmdFirst]!] ?? '?', causeClk: dd.rawClk[g - dd.cmdFirst]! } : {};
    };
    const trueEnd = (v: number, set: number, index: number) => (Number.isNaN(v) ? e.crossEnd(d.k, set, index) ?? viewState.maxTime : v);

    if (row.busType === 'bank') {
      const lane = row.path[0]!;
      for (let j = lowerBound(d.spanLane, lane); j < d.spanLane.length && d.spanLane[j] === lane; j++) {
        if (x < d.spanS[j]! || x >= d.spanE[j]!) continue;
        const kind = d.spanKind[j]!;
        const cause = d.spanCause[j]!;
        const out: BankHover = {
          row: 'bank',
          title: bankLaneLabel(L, lane),
          path: bankLanePath(L, lane),
          state: bankKindName(kind),
          color: bankKindColor(kind),
          start: d.spanTrueS[j]!,
          end: trueEnd(d.spanTrueE[j]!, 0, lane),
          cause,
          ...causeInfo(cause),
        };
        if (kind === BANK_KIND.OPEN) {
          out.sub = 'not blocked (open-but-starved)';
          out.subColor = BANK_COLORS.open;
          const sets: ['bus' | 'bg', IntervalPieces | null, number | null, number][] = [['bus', d.bus, L.busLevel, 1], ['bg', d.bg, L.bgLevel, 2]];
          for (const [name, p, level, set] of sets) {
            if (!p || level === null) continue;
            const dom = bankDomOf(L, lane, level);
            const hit = pieceAt(p, dom, x);
            if (!hit.inside) continue;
            out.sub = name === 'bus' ? 'data bus busy' : 'bank group blocked (tCCD_L)';
            out.subColor = name === 'bus' ? BANK_COLORS.bus : BANK_COLORS.bg;
            out.subStart = p.trueS[hit.q]!;
            out.subEnd = trueEnd(p.trueE[hit.q]!, set, dom);
            break;
          }
        }
        return out;
      }
      return null;
    }

    const isBus = row.busType === 'bk_bus';
    const p = isBus ? d.bus : d.faw;
    if (!p) return null;
    const dom = bankCoordsRange(L, row.path).dom;
    const hit = pieceAt(p, dom, x);
    const cause = hit.inside ? p.cause[hit.q]! : -1;
    return {
      row: isBus ? 'bus' : 'faw',
      title: isBus ? 'DATA BUS' : 'ACT / tFAW',
      path: bankCoordsName(L, row.path, `Channel ${sessionStore.header?.channelId ?? 0}`),
      state: isBus
        ? (hit.inside ? 'data bus busy (back-to-back bursts form one stretch; the cause is its first)' : 'bus idle')
        : (hit.inside ? 'four ACTs in the trailing tFAW: no ACT allowed' : 'ACT allowed'),
      color: hit.inside ? (isBus ? BANK_COLORS.busLane : BANK_COLORS.fawLane) : '#ffffff',
      start: hit.inside ? p.trueS[hit.q]! : (Number.isFinite(hit.prevEnd) ? d.origin + hit.prevEnd : d.origin),
      end: hit.inside
        ? trueEnd(p.trueE[hit.q]!, isBus ? 1 : 3, dom)
        : (Number.isFinite(hit.nextStart) ? d.origin + hit.nextStart : Math.min(d.origin + d.span * TILE, engine?.endClk ?? Infinity)),
      cause,
      ...causeInfo(cause),
    };
  }

  function recomputeBankStats() {
    const e = engine;
    const b = e?.ctx.bank;
    const geom = bankLayer?.geometry();
    if (!e || !b || !geom) {
      tl.bankStats.value = null;
      bankHeaderTitles.clear();
      return;
    }
    bankStatsVersion = e.version;
    const sel = rangeSelection.value;
    const scope: 'view' | 'range' = sel && sel.endTime > sel.startTime ? 'range' : 'view';
    const t0 = Math.max(viewState.minTime, scope === 'range' ? sel!.startTime : viewState.start);
    const t1 = scope === 'range' ? sel!.endTime : viewState.start + viewState.duration;
    if (!(t1 > t0)) return;
    const L = b.L;
    const hasWrrec = b.rules.cmd.some(r => r.action === 5);
    const hasRef = b.rules.cmd.some(r => r.action === 6);
    const opts = { hasBg: L.bgLevel !== null, hasWrrec, hasRef };
    const frontier = e.readFrontier;
    const unread = t0 >= frontier ? 'all' as const : t1 > frontier ? 'part' as const : undefined;
    bankHeaderTitles.clear();
    for (const h of geom.headers) {
      const { level, dom, first, count } = bankCoordsRange(L, h.coords);
      const lanes = Array.from({ length: count }, (_, i) => first + i);
      const acc = bankWindow(e, e.ctx, lanes, t0, t1);
      const busPct = level === L.busLevel ? coverageWindow(e, 'bus', dom, t0, t1).value * 100 : null;
      const fawPct = L.fawLevel !== null && level === L.fawLevel ? coverageWindow(e, 'faw', dom, t0, t1).value * 100 : null;
      bankHeaderTitles.set(h.coords.join(','), bankTitle({ busPct, fawPct }, bankShares(acc), { ...opts, range: scope === 'range', unread }));
    }
    const lanes = geom.visibleLanes.length > 0 ? geom.visibleLanes : Array.from({ length: L.numBanks }, (_, i) => i);
    const acc = bankWindow(e, e.ctx, lanes, t0, t1);
    const mean = (set: 'bus' | 'faw', level: number | null) => {
      if (level === null || (set === 'faw' && !e.ctx.bank?.fawDoms)) return null;
      const doms = new Set(lanes.map(l => bankDomOf(L, l, level)));
      let sum = 0;
      for (const d of doms) sum += coverageWindow(e, set, d, t0, t1).value;
      return doms.size ? (sum / doms.size) * 100 : null;
    };
    tl.bankStats.value = {
      scope, t0, t1, banks: acc.banks,
      busPct: mean('bus', L.busLevel),
      fawPct: mean('faw', L.fawLevel),
      shares: bankShares(acc),
      ...opts,
      // A window reaching past what is read so far can only be estimated.
      exact: acc.exact && !unread,
    };
    tl.frames.invalidate();
  }

  /** Refresh now, then at most every BANK_STATS_THROTTLE_MS while the view keeps changing. */
  function scheduleBankStats() {
    if (view !== 'bank') return;
    if (bankStatsTimer) {
      bankStatsAgain = true;
      return;
    }
    recomputeBankStats();
    bankStatsTimer = setTimeout(() => {
      bankStatsTimer = null;
      if (bankStatsAgain) {
        bankStatsAgain = false;
        scheduleBankStats();
      }
    }, BANK_STATS_THROTTLE_MS);
  }

  // ── Linked selection ──────────────────────────────────────────

  /** Canvas rectangle of a command bar (in its lane), or null when it is not on screen. */
  function cmdRect(gidx: number): { x: number; y: number; w: number; h: number } | null {
    const e = engine;
    if (!e || !cmdLaneY || !cmdBusVisible) return null;
    const d = detailOfEntry(e, gidx);
    if (!d) return null;
    const j = gidx - d.cmdFirst;
    // 0: lane not in the tree layout; HIDDEN_Y: shown on the CMD BUS row only.
    const y = cmdLaneY[d.cmdLane[j]! * 4]!;
    if (y === 0 || y <= HIDDEN_Y / 2) return null;
    const pxPerCycle = cssW / viewState.duration;
    const x = (d.origin + d.cmdT[j]! - viewState.start) * pxPerCycle;
    return { x, y: y - ROW_HEIGHT / 2, w: Math.max(2, commandDuration(e.spec, d.cmdId[j]!) * pxPerCycle), h: ROW_HEIGHT };
  }

  function reqRect(g: LinkedSelection): { x: number; y: number; w: number; h: number } | null {
    const e = engine;
    if (!e || !reqSlotY || !reqBusVisible || g.reqK < 0) return null;
    const d = e.peek(g.reqK);
    if (!d || g.reqJ >= d.reqT.length) return null;
    const j = g.reqJ;
    const y = reqSlotY[(d.reqLane[j]! * REQ_STACKS_DRAWN + Math.min(d.reqStack[j]!, REQ_STACKS_DRAWN - 1)) * 4]!;
    if (y === 0) return null;
    const pxPerCycle = cssW / viewState.duration;
    return { x: (d.origin + d.reqT[j]! - viewState.start) * pxPerCycle, y: y - REQ_BAR_HEIGHT / 2, w: Math.max(2, d.reqDur[j]! * pxPerCycle), h: REQ_BAR_HEIGHT };
  }

  /**
   * Box around a command's marker in its bank's row (Bank state), or null when that row is hidden.
   * A multi-bank command gets the box in its first bank.
   */
  function bankMarkerRect(gidx: number): { x: number; y: number; w: number; h: number } | null {
    const e = engine;
    const geom = bankLayer?.geometry();
    if (!e || !geom) return null;
    const d = detailOfEntry(e, gidx);
    if (!d) return null;
    const j = gidx - d.cmdFirst;
    const lane = d.cmdLane[j]!;
    const top = geom.laneTop[lane];
    if (top === undefined || Number.isNaN(top)) return null;
    const x = (d.origin + d.cmdT[j]! - viewState.start) * (cssW / viewState.duration);
    return { x: x - BANK_MARKER_HALF_PX, y: top, w: 2 * BANK_MARKER_HALF_PX, h: geom.laneH[lane]! };
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
    if (view === 'throughput') return;
    const sel = tl.selection.value;
    const pv = tl.linkPreview.value;
    const draw = (g: LinkedSelection, color: string, width: number) => {
      if (view === 'request') {
        const r = reqRect(g);
        if (r) outlineBar(ctx, r, color, width);
      } else {
        for (const i of g.cmds) {
          const r = view === 'bank' ? bankMarkerRect(i) : cmdRect(i);
          if (r) outlineBar(ctx, r, color, width);
        }
      }
    };
    ctx.save();
    if (pv && !(sel && sel.req === pv.req && sel.cmds[0] === pv.cmds[0])) draw(pv, PAPER.muted, 1);
    if (sel) draw(sel, PAPER.accent, 1.5);
    ctx.restore();
  }

  function anchorOf(r: { x: number; y: number; w: number; h: number } | null): LinkAnchor | null {
    const el = canvas.value;
    if (!el || !r) return null;
    const rect = el.getBoundingClientRect();
    return {
      x: rect.left + r.x + r.w / 2, top: rect.top + r.y, bottom: rect.top + r.y + r.h,
      clipTop: rect.top, clipBottom: rect.bottom, clipLeft: rect.left, clipRight: rect.right,
    };
  }

  // ── 2D overlay ────────────────────────────────────────────────

  function placeCursorAtClientX(clientX: number) {
    tl.placeCursor(timeAtClientX(clientX), view);
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

  function drawThroughput(ctx: CanvasRenderingContext2D, e: TraceEngine, W: number, H: number, canvasTop: number) {
    const { points, nodes, nodeMax } = throughputPoints(e, W);
    if (points.length === 0) return;
    const hov = tl.throughputHover.value;
    const chart = (series: 'data' | 'cmd', areaTop: number, areaH: number, stroke: string, fill: string) => {
      const { chartTop, chartH } = chartGeometry(areaTop, areaH);
      if (chartH <= 0) return;
      const yMax = Math.max(TP_MIN_SCALE, ...points.map(p => p[series]));
      ctx.save();
      ctx.beginPath();
      ctx.rect(0, areaTop, W, areaH);
      ctx.clip();
      const sel = rangeSelection.value;
      if (sel) {
        const x0 = Math.max(0, Math.min(W, ((sel.startTime - viewState.start) / viewState.duration) * W));
        const x1 = Math.max(0, Math.min(W, ((sel.endTime - viewState.start) / viewState.duration) * W));
        if (x1 > x0) {
          ctx.fillStyle = PAPER.tp.rangeFill;
          ctx.fillRect(x0, areaTop, x1 - x0, areaH);
        }
      }
      const fracs = [0, 0.25, 0.5, 0.75, 1.0];
      for (const frac of fracs) {
        const y = chartTop + chartH - frac * chartH;
        ctx.beginPath();
        ctx.strokeStyle = PAPER.tp.grid;
        ctx.lineWidth = 1;
        ctx.moveTo(34, Math.round(y) + 0.5);
        ctx.lineTo(W, Math.round(y) + 0.5);
        ctx.stroke();
      }
      const yOf = (v: number) => chartTop + chartH - (v / yMax) * chartH;
      const line = [{ x: Math.min(0, points[0]!.x), y: yOf(points[0]![series]) }, ...points.map(p => ({ x: p.x, y: yOf(p[series]) }))];
      line.push({ x: Math.max(W, points[points.length - 1]!.x), y: line[line.length - 1]!.y });
      ctx.beginPath();
      line.forEach((p, i) => (i === 0 ? ctx.moveTo(p.x, p.y) : ctx.lineTo(p.x, p.y)));
      ctx.strokeStyle = stroke;
      ctx.lineWidth = 1.5;
      ctx.stroke();
      ctx.lineTo(line[line.length - 1]!.x, chartTop + chartH);
      ctx.lineTo(line[0]!.x, chartTop + chartH);
      ctx.closePath();
      ctx.fillStyle = fill;
      ctx.fill();
      ctx.beginPath();
      let lastX = Number.NEGATIVE_INFINITY;
      points.forEach((p, i) => {
        if (i !== points.length - 1 && p.x - lastX < TP_MARKER_MIN_GAP_PX) return;
        lastX = p.x;
        ctx.moveTo(p.x + TP_POINT_RADIUS, yOf(p[series]));
        ctx.arc(p.x, yOf(p[series]), TP_POINT_RADIUS, 0, Math.PI * 2);
      });
      ctx.fillStyle = PAPER.tp.markerFill;
      ctx.fill();
      ctx.strokeStyle = stroke;
      ctx.lineWidth = 1;
      ctx.stroke();
      if (hov && hov.series === series) {
        const hp = points.find(p => p.a === hov.binStartTime);
        if (hp) {
          ctx.beginPath();
          ctx.arc(hp.x, yOf(hp[series]), TP_POINT_RADIUS + 2, 0, Math.PI * 2);
          ctx.fillStyle = PAPER.tp.markerHalo;
          ctx.fill();
          ctx.beginPath();
          ctx.arc(hp.x, yOf(hp[series]), TP_POINT_RADIUS + 0.5, 0, Math.PI * 2);
          ctx.fillStyle = stroke;
          ctx.fill();
        }
      }
      // Axis labels last, so the line does not cover them.
      ctx.font = '9px ui-monospace, IBM Plex Mono, monospace';
      ctx.textAlign = 'right';
      ctx.textBaseline = 'middle';
      for (const frac of fracs) axisLabel(ctx, `${(yMax * frac * 100).toFixed(0)}%`, 30, chartTop + chartH - frac * chartH);
      ctx.restore();
    };
    const dataRow = panel.rowLayout.find(r => r.busType === 'tp_data');
    const cmdRow = panel.rowLayout.find(r => r.busType === 'tp_cmd');
    if (dataRow) chart('data', dataRow.top - canvasTop, dataRow.height, PAPER.tp.data, PAPER.tp.dataFill);
    if (cmdRow) chart('cmd', cmdRow.top - canvasTop, cmdRow.height, PAPER.tp.cmd, PAPER.tp.cmdFill);

    ctx.save();
    ctx.font = '9px ui-monospace, IBM Plex Mono, monospace';
    ctx.textAlign = 'right';
    ctx.textBaseline = 'top';
    for (const r of panel.rowLayout) {
      if (r.busType !== 'tp_node') continue;
      const top = r.top - canvasTop;
      if (top > H || top + r.height < 0) continue;
      const v = nodes.get(r.path.join('.'));
      if (!v) continue;
      const yMax = nodeMax.get(r.path.length) ?? 0.01;
      const pad = 3;
      const base = top + r.height - pad;
      const h = r.height - 2 * pad;
      const pts = points.map((p, i) => [p.x, base - (v[i]! / yMax) * h] as const);
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
      if (hov?.series === 'node' && hov.path?.length === r.path.length && hov.path.every((x, k) => x === r.path[k])) {
        const i = points.findIndex(p => p.a === hov.binStartTime);
        if (i >= 0) {
          ctx.beginPath();
          ctx.arc(pts[i]![0], pts[i]![1], TP_POINT_RADIUS + 0.5, 0, Math.PI * 2);
          ctx.fillStyle = PAPER.tp.data;
          ctx.fill();
        }
      }
    }
    ctx.restore();
  }

  function drawOverlay(e: TraceEngine | null, recs: number[], instances: boolean) {
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
    if (!spec || !e) return;
    const vw = viewState.duration;
    const canvasTop = el.getBoundingClientRect().top;

    // Group bands (Commands / Requests): expanded hierarchy rows, as in the Bank state view.
    if (view === 'command' || view === 'request') {
      const prefix = view === 'command' ? 'cmd' : 'req';
      const expanded = new Set(panel.expandedState.length > 0 ? panel.expandedState : computeDefaultExpanded(view, spec));
      const leafDepth = specStructuralDepth(spec);
      ctx.fillStyle = PAPER.groupBand;
      for (const r of panel.rowLayout) {
        if (r.busType !== view || r.depth >= leafDepth) continue;
        if (!expanded.has(nodeId(prefix, r.path))) continue;
        ctx.fillRect(0, r.top - canvasTop, W, r.height);
      }
      const dataSum = view === 'command' ? panel.rowLayout.find(r => r.busType === 'data') : undefined;
      if (dataSum) {
        ctx.fillStyle = PAPER.summaryRule;
        ctx.fillRect(0, Math.round(dataSum.top + dataSum.height - canvasTop) - 1, W, 1.5);
      }
    }

    // Row separators.
    if (view !== 'bank') {
      ctx.fillStyle = '#ebedef';
      for (const r of panel.rowLayout) {
        const y = r.top + r.height - canvasTop;
        if (y >= 0 && y <= H) ctx.fillRect(0, Math.round(y) - 1, W, 1);
      }
      // Vertical gridlines at the time axis's major ticks.
      const tck = sessionStore.getTimingValue('tCK_ps');
      const nsPerCycle = uiStore.displayTimelineInNs && tck ? tck / 1000 : null;
      ctx.beginPath();
      ctx.strokeStyle = VGRID_COLOR;
      ctx.lineWidth = 1;
      for (const t of axisTicks(viewState.start, vw, W, nsPerCycle, viewState.minTime, viewState.maxTime).major) {
        const x = Math.round(((t - viewState.start) / vw) * W) + 0.5;
        ctx.moveTo(x, 0);
        ctx.lineTo(x, H);
      }
      ctx.stroke();
    }

    // Range selection highlight.
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

    if (view === 'bank') {
      const L = e.ctx.bank?.L;
      if (bankLayer && L) {
        bankLayer.drawOverlay(ctx, W, bankHeaderTitles, c => (c.length === 0
          ? `Channel ${sessionStore.header?.channelId ?? 0}`
          : `${L.levelNames[L.firstLevel + c.length - 1]} ${c[c.length - 1]}`));
        if (instances && !showMarkers(e, recs)) {
          let n = 0;
          for (const i of recs) n += e.tiles[i]!.nCmd;
          ctx.save();
          ctx.font = '10px ui-monospace, IBM Plex Mono, monospace';
          ctx.textAlign = 'right';
          ctx.textBaseline = 'bottom';
          ctx.fillStyle = 'rgba(39,39,42,0.75)';
          ctx.fillText(`${n.toLocaleString()} commands in view — zoom in to see command markers`, W - 8, H - 6);
          ctx.restore();
        }
      }
      drawLinked(ctx);
      drawCursorLine(ctx, W, H);
      return;
    }

    if (view === 'throughput') {
      drawThroughput(ctx, e, W, H, canvasTop);
      drawCursorLine(ctx, W, H);
      return;
    }

    // Command names on wide-enough bars.
    if (view === 'command' && instances) {
      const pxPerCycle = W / vw;
      ctx.font = '10px ui-monospace, IBM Plex Mono, monospace';
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      const label = (sx: number, y: number, raw: string) => {
        const text = raw.length > 14 ? `${raw.slice(0, 13)}…` : raw;
        ctx.shadowColor = PAPER.barLabelShadow;
        ctx.shadowBlur = 4;
        ctx.fillStyle = PAPER.barLabel;
        ctx.fillText(text, sx, y);
        ctx.shadowBlur = 0;
      };
      let budget = CMD_LABEL_MAX_VISIBLE;
      const burst = burstCycles(spec);
      for (const i of recs) {
        const d = e.peek(e.tiles[i]!.k);
        if (!d || budget <= 0) continue;
        const a = viewState.start - d.origin;
        const b = a + vw;
        if (cmdBusVisible && cmdLaneY && pxPerCycle >= CMD_LABEL_MIN_PX_PER_CYCLE) {
          for (let j = lowerBound(d.cmdT, a - maxCmdDur), z = lowerBound(d.cmdT, b); j < z && budget > 0; j++, budget--) {
            const cid = d.cmdId[j]!;
            label((d.origin + d.cmdT[j]! + commandDuration(spec, cid) / 2 - viewState.start) * pxPerCycle, cmdLaneY[d.cmdLane[j]! * 4]!, spec.commandNames[cid] ?? '?');
          }
          for (let j = lowerBound(d.fanT, a - maxCmdDur), z = lowerBound(d.fanT, b); j < z && budget > 0; j++, budget--) {
            const cid = d.fanCmd[j]!;
            label((d.origin + d.fanT[j]! + commandDuration(spec, cid) / 2 - viewState.start) * pxPerCycle, cmdLaneY[d.fanLane[j]! * 4]!, spec.commandNames[cid] ?? '?');
          }
        }
        if (dataBusVisible && dataLaneY && pxPerCycle * maxBurst >= LABEL_MIN_BAR_WIDTH_PX) {
          for (let j = lowerBound(d.busT, a - maxBurst), z = lowerBound(d.busT, b); j < z && budget > 0; j++, budget--) {
            const cid = d.busCmd[j]!;
            label((d.origin + d.busT[j]! + burst[cid]! / 2 - viewState.start) * pxPerCycle, dataLaneY[d.busLane[j]! * 4]!, spec.commandNames[cid] ?? '?');
          }
        }
      }
    }

    drawLinked(ctx);
    drawCursorLine(ctx, W, H);
  }

  function showMarkers(e: TraceEngine, recs: number[]): boolean {
    let n = 0;
    for (const i of recs) n += commandsInView(e.tiles[i]!);
    return n <= (softwareGl ? MARKER_LIMIT_SOFTWARE : MARKER_LIMIT);
  }

  // ── GPU lifecycle ─────────────────────────────────────────────

  /** Destroy the regl context; `release` also drops the WebGL context (the canvas is going away). */
  function destroyGpu(release: boolean) {
    if (!regl) return;
    clearTiles();
    heat?.destroy();
    bankLayer?.destroy();
    const gl = (regl as unknown as { _gl?: WebGLRenderingContext })._gl;
    regl.destroy();
    if (release) gl?.getExtension('WEBGL_lose_context')?.loseContext();
    regl = null;
    heat = null;
    bankLayer = null;
    drawTrace = drawTraceHover = drawReq = drawReqHover = null;
    cmdLookupTex = dataLookupTex = cmdLanesTex = dataLanesTex = reqLookupTex = reqSlotsTex = null;
    gpuEngine = null;
  }

  function refreshCommandColors() {
    const e = engine;
    if (!e || !cmdLookupTex || !dataLookupTex) return;
    const spec = e.spec;
    const set = (tex: createREGL.Texture2D, data: Float32Array) => (tex as any)({ width: 256, height: 1, data, format: 'rgba', type: 'float' });
    set(cmdLookupTex, cmdTextureData(spec.commandNames, spec.commandMeta, sessionStore.commandColors, Array.from(spec.commandNames, (_, id) => commandDuration(spec, id))));
    set(dataLookupTex, cmdTextureData(spec.commandNames, spec.commandMeta, sessionStore.commandColors, burstCycles(spec)));
    heatKey = '';
    tl.frames.invalidate();
  }

  function refreshRequestColors() {
    if (!reqLookupTex) return;
    (reqLookupTex as any)({ width: 256, height: 1, data: reqTextureData(sessionStore.requestTypeColors), format: 'rgba', type: 'float' });
    heatKey = '';
    tl.frames.invalidate();
  }

  function initGpu(el: HTMLCanvasElement, e: TraceEngine) {
    destroyGpu(false);
    sizeCanvases(el);
    engine = e;
    strides = e.ctx.li.strides;
    totalLanes = e.ctx.li.totalLanes;
    maxCmdDur = maxCommandDuration(e.spec);
    maxBurst = Math.max(...burstCycles(e.spec), 1);
    try {
      regl = createREGL({
        canvas: el,
        attributes: { antialias: false, alpha: false, stencil: true },
        extensions: ['angle_instanced_arrays', 'oes_texture_float'],
      });
    } catch (err) {
      console.error('Failed to initialize regl:', err);
      return;
    }
    const r = regl;
    const gl = r._gl;
    const info = gl.getExtension('WEBGL_debug_renderer_info');
    softwareGl = SOFTWARE_GL.test(String(gl.getParameter(info ? info.UNMASKED_RENDERER_WEBGL : gl.RENDERER)));
    gpuEngine = e;
    lastLayoutVersion = -1;
    const quad: [number, number][] = [[0, 0], [1, 0], [0, 1], [0, 1], [1, 0], [1, 1]];
    const lookupTex = (width: number) => r.texture({ width: Math.max(width, 1), height: 1, data: new Float32Array(Math.max(width, 1) * 4), format: 'rgba', type: 'float', min: 'nearest', mag: 'nearest' });
    const resolution = (ctx: any) => [ctx.viewportWidth / dpr, ctx.viewportHeight / dpr];
    const alphaBlend = {
      enable: true,
      func: { srcRGB: 'src alpha', srcAlpha: 'one', dstRGB: 'one minus src alpha', dstAlpha: 'one minus src alpha' },
    } as const;
    heat = createHeatLayer(r, () => dpr);

    interface PlaceProps { oInt: number; oFrac: number; viewW: number; offset: number; instances: number }
    const attr = (pick: (p: any) => createREGL.Buffer, bytes: number, normalized?: false) => ((_: any, p: any) => ({
      buffer: pick(p), divisor: 1, offset: p.offset * bytes, ...(normalized === false ? { normalized } : {}),
    })) as any;
    const place = {
      u_oInt: r.prop<PlaceProps, 'oInt'>('oInt'),
      u_oFrac: r.prop<PlaceProps, 'oFrac'>('oFrac'),
      u_viewW: r.prop<PlaceProps, 'viewW'>('viewW'),
      u_resolution: resolution,
    };

    if (view === 'command') {
      cmdLookupTex = lookupTex(256);
      dataLookupTex = lookupTex(256);
      cmdLanesTex = lookupTex(totalLanes + 1);
      dataLanesTex = lookupTex(totalLanes + 1);
      refreshCommandColors();
      // props: t / id / lane buffers, lookup and lane textures, forceY.
      const traceDraw = (frag: string, hover: boolean) => r({
        vert: TRACE_VERT,
        frag,
        attributes: {
          a_quad: quad,
          a_t: attr(p => p.t, 4),
          a_cmdId: attr(p => p.id, 1, false),
          a_laneIdx: attr(p => p.lane, 2, false),
        },
        uniforms: {
          ...place,
          u_cmdLookup: r.prop<any, 'lookup'>('lookup'),
          u_lanes: r.prop<any, 'lanes'>('lanes'),
          u_rowHeight: ROW_HEIGHT,
          u_laneCount: totalLanes + 1,
          u_forceY: ((_: any, p: any) => p.forceY ?? -1) as any,
          u_hover: hover ? 1 : 0,
        },
        ...(hover ? { blend: alphaBlend } : {}),
        depth: { enable: false },
        instances: r.prop<PlaceProps, 'instances'>('instances'),
        count: 6,
      });
      drawTrace = traceDraw(TRACE_FRAG, false);
      drawTraceHover = traceDraw(HOVER_FRAG, true);
    }

    if (view === 'request') {
      reqLookupTex = lookupTex(256);
      reqSlotsTex = lookupTex(totalLanes * REQ_STACKS_DRAWN);
      refreshRequestColors();
      const reqDraw = (frag: string) => r({
        vert: REQ_VERT,
        frag,
        attributes: {
          a_quad: quad,
          a_t: attr(p => p.g.t, 4),
          a_dur: attr(p => p.g.dur, 4),
          a_queue: attr(p => p.g.queue, 4),
          a_lane: attr(p => p.g.lane, 2, false),
          a_stack: attr(p => p.g.stack, 1, false),
          a_flags: attr(p => p.g.flags, 1, false),
        },
        uniforms: {
          ...place,
          u_typeLookup: () => reqLookupTex!,
          u_slots: () => reqSlotsTex!,
          u_slotCount: totalLanes * REQ_STACKS_DRAWN,
          u_rowHeight: REQ_BAR_HEIGHT,
        },
        blend: alphaBlend,
        depth: { enable: false },
        instances: r.prop<PlaceProps, 'instances'>('instances'),
        count: 6,
      });
      drawReq = reqDraw(REQ_FRAG);
      drawReqHover = reqDraw(HOVER_FRAG);
    }

    if (view === 'bank' && e.ctx.bank) {
      bankLayer = createBankLayer(r, e.ctx.bank.L, e.ctx.bank.rules, () => dpr);
    }
  }

  /** Rebuild the zoomed-out image if the view or data moved on (at most every 40 ms). */
  function updateHeat(e: TraceEngine, recs: number[], canvasTop: number) {
    if (!heat) return;
    const rows = heatRows(canvasTop);
    const vs = viewState.start;
    const vw = viewState.duration;
    const img = heat.image;
    const rowsKey = rows.map(r => `${r.kind}${r.first}.${r.count}@${Math.round(r.y)}`).join('|');
    const key = `${e.version}|${rowsKey}|${recs.join(',')}`;
    const covered = img && img.t0 <= vs && img.t1 >= vs + vw && Math.abs(Math.log((img.t1 - img.t0) / (2 * vw))) < 0.15;
    if (key === heatKey && covered) return;
    const now = performance.now();
    if (img && now - heatBuiltAt < 40) {
      tl.frames.invalidate();
      return;
    }
    heatBuiltAt = now;
    heatKey = key;
    // Twice the view's width, so small pans reuse the image.
    const t0 = vs - vw / 2;
    const t1 = vs + vw * 1.5;
    const width = Math.min(4096, Math.max(2, Math.round(cssW * 2)));
    heat.set(buildHeat(e, recs, rows, t0, t1, width, heatColors()), rows);
  }

  function drawFrame() {
    const el = canvas.value;
    const e = engine;
    if (!el || !regl || !e) return;
    if ((window.devicePixelRatio || 1) !== dpr) sizeCanvases(el);
    regl.poll();
    regl.clear({ color: BG_COLOR, depth: 1, stencil: 0 });
    if (panel.layoutVersion !== lastLayoutVersion) {
      lastLayoutVersion = panel.layoutVersion;
      refreshLayout();
    }
    const canvasTop = el.getBoundingClientRect().top;
    const t0 = viewState.start;
    const t1 = t0 + viewState.duration;
    const recs = visibleRecords(e, t0, t1);
    const instances = view !== 'throughput' && instancesIn(e, recs) <= instanceLimit();

    // Tiles without detail (or every tile, zoomed out) are drawn from their summaries.
    const summaryRecs: number[] = [];
    const detailed: TileDetail[] = [];
    if (view !== 'throughput') {
      for (const i of recs) {
        const d = instances ? e.detail(e.tiles[i]!.k) : undefined;
        if (d) detailed.push(d);
        else summaryRecs.push(i);
      }
      if (summaryRecs.length) {
        updateHeat(e, summaryRecs, canvasTop);
        // Zoomed in, these are tiles whose detail is still loading: a faint placeholder, so their
        // coarse blocks do not pass for bars.
        heat?.draw(t0, viewState.duration, cssW, instances ? HEAT_LOADING_ALPHA : 1);
      } else {
        heat?.clear();
        heatKey = '';
      }
    }

    if (view === 'command' && drawTrace && cmdLookupTex && dataLookupTex && cmdLanesTex && dataLanesTex) {
      for (const d of detailed) {
        const g = tileGpu(d) as CmdTileGpu | null;
        if (!g) continue;
        const p = place(d.origin);
        if (cmdBusVisible) {
          const [a, b] = inView(d.cmdT, d, maxCmdDur);
          if (b > a) {
            drawTrace({ ...p, ...g.cmd, offset: a, instances: b - a, lookup: cmdLookupTex, lanes: cmdLanesTex });
            if (cmdSumY !== null) drawTrace({ ...p, ...g.cmd, offset: a, instances: b - a, lookup: cmdLookupTex, lanes: cmdLanesTex, forceY: cmdSumY });
          }
          const [fa, fb] = inView(d.fanT, d, maxCmdDur);
          if (fb > fa) drawTrace({ ...p, ...g.fan, offset: fa, instances: fb - fa, lookup: cmdLookupTex, lanes: cmdLanesTex });
        }
        if (dataBusVisible) {
          const [a, b] = inView(d.busT, d, maxBurst);
          if (b > a) drawTrace({ ...p, ...g.bus, offset: a, instances: b - a, lookup: dataLookupTex, lanes: dataLanesTex });
        }
      }
      // Hovered command: its bar, its other-bank instances and its burst.
      const hov = hoveredEvent.value;
      if (hov && drawTraceHover && (hov.bus === 'command' || hov.bus === 'data')) {
        for (const d of detailed) {
          const g = gpuTiles.get(d.k) as CmdTileGpu | undefined;
          if (!g) continue;
          const p = place(d.origin);
          const j = hov.index - d.cmdFirst;
          if (cmdBusVisible && j >= 0 && j < d.cmdT.length) drawTraceHover({ ...p, ...g.cmd, offset: j, instances: 1, lookup: cmdLookupTex, lanes: cmdLanesTex });
          for (let q = 0; q < d.fanIdx.length; q++) if (d.fanIdx[q] === hov.index && cmdBusVisible) drawTraceHover({ ...p, ...g.fan, offset: q, instances: 1, lookup: cmdLookupTex, lanes: cmdLanesTex });
          for (let q = 0; q < d.busIdx.length; q++) if (d.busIdx[q] === hov.index && dataBusVisible) drawTraceHover({ ...p, ...g.bus, offset: q, instances: 1, lookup: dataLookupTex, lanes: dataLanesTex });
        }
      }
    } else if (view === 'request' && drawReq && reqBusVisible) {
      for (const d of detailed) {
        const g = tileGpu(d) as ReqTileGpu | null;
        if (!g || !g.n) continue;
        const t0 = viewState.start - d.origin - viewState.duration / cssW;
        const a = lowerBound(g.endMax, t0);
        const b = lowerBound(d.reqT, t0 + viewState.duration * (1 + 2 / cssW), a);
        if (b > a) drawReq({ ...place(d.origin), g, offset: a, instances: b - a });
      }
      const hov = hoveredEvent.value;
      if (hov?.bus === 'request' && hov.reqK !== undefined && hov.reqJ !== undefined && drawReqHover) {
        const g = gpuTiles.get(hov.reqK) as ReqTileGpu | undefined;
        if (g && hov.reqJ < g.n) drawReqHover({ ...place(g.detail.origin), g, offset: hov.reqJ, instances: 1 });
      }
    } else if (view === 'bank' && bankLayer) {
      const tiles = detailed.map(d => [d, tileGpu(d) as BankTileGpu | null] as const);
      for (const [d, g] of tiles) if (g) bankLayer.drawTile(g, place(d.origin));
      for (const [d, g] of tiles) if (g) bankLayer.drawTileLanes(g, place(d.origin));
      if (showMarkers(e, recs)) for (const [d, g] of tiles) if (g) bankLayer.drawTileMarkers(g, place(d.origin));
      if (e.version !== bankStatsVersion) scheduleBankStats();
    }

    drawOverlay(e, recs, instances && summaryRecs.length < recs.length);
  }

  /** Bring the GPU side in line with the timeline's engine. */
  function sync() {
    const e = tl.engine.value;
    const el = canvas.value;
    if (!el) return;
    if (!e) {
      destroyGpu(false);
      engine = null;
      return;
    }
    if (!regl || gpuEngine !== e) initGpu(el, e);
    tl.frames.invalidate();
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

    const timeAt = (clientX: number) => Math.max(viewState.minTime, Math.min(viewState.maxTime, timeAtClientX(clientX)));

    el.addEventListener('wheel', (e) => {
      e.preventDefault();
      const r = el.getBoundingClientRect();
      // Deltas in px: a mouse notch is ~100 px, a trackpad sends many small events.
      const unit = e.deltaMode === WheelEvent.DOM_DELTA_LINE ? 40 : e.deltaMode === WheelEvent.DOM_DELTA_PAGE ? r.width : 1;
      // Shift+wheel pans, also where the browser does not turn it into a horizontal scroll.
      const shiftPan = e.shiftKey && !e.ctrlKey && e.deltaX === 0;
      const dx = (shiftPan ? e.deltaY : e.deltaX) * unit;
      const dy = shiftPan ? 0 : e.deltaY * unit;
      if (Math.abs(dx) > Math.abs(dy)) {
        // Horizontal swipe or Shift+wheel pans.
        tl.setView(viewState.start + (dx / r.width) * viewState.duration, viewState.duration);
        return;
      }
      if (dy === 0) return;
      const frac = (e.clientX - r.left) / r.width;
      const anchor = viewState.start + frac * viewState.duration;
      // ~15% per mouse notch, proportionally less per trackpad event; pinch (ctrlKey) is finer-grained.
      const factor = Math.exp(Math.max(-500, Math.min(500, dy)) * (e.ctrlKey ? 0.01 : 0.0014));
      const dur = Math.max(viewState.minDuration, Math.min(viewState.duration * factor, viewState.maxDuration));
      tl.setView(anchor - frac * dur, dur);
    }, { passive: false, signal });

    // Ctrl+click opens the context menu on macOS; Cmd works as Ctrl for range zoom.
    el.addEventListener('contextmenu', (e) => {
      if (e.ctrlKey || rangeSelecting) e.preventDefault();
    }, { signal });

    el.addEventListener('mousedown', (e) => {
      if (e.button !== 0) return;
      dragMoved = false;
      dragStartX = e.clientX;
      const zoomKey = e.ctrlKey || e.metaKey;
      if (cursorMode.value && !e.shiftKey && !zoomKey) return;
      if (e.shiftKey || zoomKey) {
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
      tl.setView(dragStartTime - ((e.clientX - dragStartX) / r.width) * viewState.duration, viewState.duration);
    }, { signal });

    window.addEventListener('mouseup', (e: MouseEvent) => {
      if (rangeSelecting) {
        rangeSelecting = false;
        const sel = rangeSelection.value;
        if (!sel || sel.endTime - sel.startTime < 1) {
          rangeSelection.value = null;
        } else if (e.ctrlKey || e.metaKey) {
          tl.setView(sel.startTime, sel.endTime - sel.startTime);
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
      if (e.button !== 0 || dragMoved || e.shiftKey || e.ctrlKey || e.metaKey) return;
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
      // Resizing clears both canvases; repaint now rather than show a blank frame. The other
      // panels and the leader lines follow on the next frame.
      drawFrame();
      tl.frames.invalidate();
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
      tl.panelDebug.bank = {
        refreshBankStats() {
          recomputeBankStats();
          return tl.bankStats.value;
        },
        headerTitles: () => Object.fromEntries(bankHeaderTitles),
      };
    }
    if (view === 'command') tl.linkGeometry.cmd = i => anchorOf(cmdRect(i));
    if (view === 'request') tl.linkGeometry.req = g => anchorOf(reqRect(g));
    if (view === 'bank') tl.linkGeometry.bank = i => anchorOf(bankMarkerRect(i));
    removeDraw = tl.frames.add(drawFrame);
    sync();
  });

  watch(tl.engine, sync);
  watch(() => panel.layoutVersion, () => tl.frames.invalidate());
  watch(() => sessionStore.requestLaneStackDepth, () => {
    lastLayoutVersion = -1;
    tl.frames.invalidate();
  });

  onUnmounted(() => {
    if (bankStatsTimer) clearTimeout(bankStatsTimer);
    removeDraw?.();
    abortCtrl?.abort();
    resizeObs?.disconnect();
    destroyGpu(true);
    clearHover();
    if (view === 'command') delete tl.linkGeometry.cmd;
    if (view === 'request') delete tl.linkGeometry.req;
    if (view === 'bank') delete tl.linkGeometry.bank;
    if (view === 'bank') {
      tl.bankStats.value = null;
      delete tl.panelDebug.bank;
    }
  });

  if (view === 'command') {
    watch(() => sessionStore.commandColors, refreshCommandColors, { deep: true });
  }
  if (view === 'request') {
    watch(() => sessionStore.requestTypeColors, refreshRequestColors, { deep: true });
  }
  if (view === 'bank') {
    watch(() => [viewState.start, viewState.duration, rangeSelection.value, panel.layoutVersion] as const, scheduleBankStats);
  }

  /** The pointer is over something a click navigates to. */
  const clickable = computed(() => !cursorMode.value && (view === 'bank'
    ? (tl.bankHover.value?.cause ?? -1) >= 0
    : (view === 'command' || view === 'request') && hoveredEvent.value !== null));

  return { clickable };
}
