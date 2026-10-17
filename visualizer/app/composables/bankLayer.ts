/**
 * Bank State view rendering: per-bank state spans, the stencil-masked "open ∩ bus busy" /
 * "open ∩ bank group blocked" shading, the DATA BUS / ACT-tFAW summary lanes (WebGL), and the
 * separators, domain titles and command markers (2D overlay).
 */
import type createREGL from 'regl';
import type { RowLayout } from '~/stores/ui';
import {
  BANK_COLORS,
  BANK_MARKER,
  BANK_MARKER_COLORS,
  bankCoordsRange,
  bankDomOf,
  bankIntervalRange,
  bankSpanRange,
  bankTargetResolver,
  type BankIntervalSet,
  type BankStateData,
} from '~/utils/bankState';
import { hex2rgb } from '~/utils/commandColors';
import { PAPER } from '~/utils/theme';

/** Maximum commands in view for which markers are drawn. */
const MARKER_MAX_COMMANDS = 5000;
const HI_SPLIT = 65536;

const SPAN_VERT = `
precision highp float;
attribute vec2 a_quad;
attribute float a_sHi;
attribute float a_sLo;
attribute float a_eHi;
attribute float a_eLo;
attribute float a_kind;
uniform float u_baseHi;
uniform float u_baseLo;
uniform float u_viewW;
uniform vec2 u_res;
uniform float u_yTop;
uniform float u_yH;
uniform float u_openPass;
uniform vec3 u_cIdle;
uniform vec3 u_cAct;
uniform vec3 u_cPre;
uniform vec3 u_cWrrec;
uniform vec3 u_cRef;
uniform vec3 u_cOpen;
varying vec3 v_color;
void main() {
  bool isOpen = abs(a_kind - 5.0) < 0.5;
  if ((u_openPass > 0.5) != isOpen) {
    v_color = vec3(0.0);
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    return;
  }
  float xs = (a_sHi - u_baseHi) + (a_sLo - u_baseLo);
  float xe = (a_eHi - u_baseHi) + (a_eLo - u_baseLo);
  float x = clamp(mix(xs, xe, a_quad.x), -u_viewW, 2.0 * u_viewW);
  float ndcX = (x / u_viewW) * 2.0 - 1.0;
  float y = u_yTop + a_quad.y * u_yH;
  float ndcY = 1.0 - (y / u_res.y) * 2.0;
  v_color = a_kind < 0.5 ? u_cIdle
    : a_kind < 1.5 ? u_cAct
    : a_kind < 2.5 ? u_cPre
    : a_kind < 3.5 ? u_cWrrec
    : a_kind < 4.5 ? u_cRef
    : u_cOpen;
  gl_Position = vec4(ndcX, ndcY, 0.0, 1.0);
}
`;

const COLOR_FRAG = `
precision highp float;
varying vec3 v_color;
void main() {
  gl_FragColor = vec4(v_color, 1.0);
}
`;

const IV_VERT = `
precision highp float;
attribute vec2 a_quad;
attribute float a_sHi;
attribute float a_sLo;
attribute float a_eHi;
attribute float a_eLo;
uniform float u_baseHi;
uniform float u_baseLo;
uniform float u_viewW;
uniform vec2 u_res;
uniform float u_yTop;
uniform float u_yBot;
uniform float u_minPx;
uniform vec3 u_color;
varying vec3 v_color;
void main() {
  float xs = (a_sHi - u_baseHi) + (a_sLo - u_baseLo);
  float xe = (a_eHi - u_baseHi) + (a_eLo - u_baseLo);
  // Summary lanes keep sub-pixel bursts visible when zoomed out.
  float minW = u_minPx * u_viewW / u_res.x;
  xe = max(xe, xs + minW);
  float x = clamp(mix(xs, xe, a_quad.x), -u_viewW, 2.0 * u_viewW);
  float ndcX = (x / u_viewW) * 2.0 - 1.0;
  float y = mix(u_yTop, u_yBot, a_quad.y);
  float ndcY = 1.0 - (y / u_res.y) * 2.0;
  v_color = u_color;
  gl_Position = vec4(ndcX, ndcY, 0.0, 1.0);
}
`;

export interface BankViewWindow {
  /** Absolute clock at the left edge of the canvas. */
  start: number;
  duration: number;
}

export interface BankHeaderRow {
  top: number;
  height: number;
  coords: number[];
}

/** Canvas-space geometry of the sidebar rows in bank mode. */
export interface BankGeometry {
  /** Lane top / height in canvas px; NaN for hidden lanes. */
  laneTop: Float32Array;
  laneH: Float32Array;
  visibleLanes: number[];
  /** Per data-bus domain: DATA BUS lane row (top, height), and y-extent of visible bank rows. */
  busLane: Map<number, { top: number; height: number }>;
  busExtent: Map<number, [number, number]>;
  fawLane: Map<number, { top: number; height: number }>;
  bgExtent: Map<number, [number, number]>;
  headers: BankHeaderRow[];
  /** y of the rule under each run of summary lanes (DATA BUS, ACT / tFAW). */
  summaryRules: number[];
}

/** Vertical inset of a lane inside its sidebar row: lanes span 5%..95% of it. */
export function bankLaneInset(h: number): number {
  return Math.max(1, Math.round(h * 0.06));
}

export function bankGeometry(bs: BankStateData, rows: RowLayout[], canvasTop: number): BankGeometry {
  const L = bs.layout;
  const NB = L.numBanks;
  const laneTop = new Float32Array(NB).fill(Number.NaN);
  const laneH = new Float32Array(NB).fill(Number.NaN);
  const g: BankGeometry = {
    laneTop,
    laneH,
    visibleLanes: [],
    busLane: new Map(),
    busExtent: new Map(),
    fawLane: new Map(),
    bgExtent: new Map(),
    headers: [],
    summaryRules: [],
  };
  const isSummary = (r: RowLayout | undefined) => r?.busType === 'bk_bus' || r?.busType === 'bk_faw';
  const extend = (m: Map<number, [number, number]>, d: number, top: number, bot: number) => {
    const e = m.get(d);
    if (!e) m.set(d, [top, bot]);
    else {
      e[0] = Math.min(e[0], top);
      e[1] = Math.max(e[1], bot);
    }
  };
  for (let i = 0; i < rows.length; i++) {
    const r = rows[i]!;
    const top = r.top - canvasTop;
    if (r.busType === 'bank') {
      const lane = r.path[0]!;
      if (lane < 0 || lane >= NB) continue;
      laneTop[lane] = top;
      laneH[lane] = r.height;
      g.visibleLanes.push(lane);
      const inset = bankLaneInset(r.height);
      extend(g.busExtent, bankDomOf(L, lane, L.busLevel), top + inset, top + r.height - inset);
      if (L.bgLevel !== null) extend(g.bgExtent, bankDomOf(L, lane, L.bgLevel), top + inset, top + r.height - inset);
    } else if (r.busType === 'bk_bus' || r.busType === 'bk_faw') {
      const dom = bankCoordsRange(L, r.path).dom;
      const kind = r.busType === 'bk_bus' ? 'bus' : 'faw';
      (kind === 'bus' ? g.busLane : g.fawLane).set(dom, { top, height: r.height });
      if (!isSummary(rows[i + 1])) g.summaryRules.push(top + r.height);
    } else if (r.busType === 'bk_hdr') {
      g.headers.push({ top, height: r.height, coords: r.path });
    }
  }
  return g;
}

function splitHiLo(src: Float64Array): { hi: Float32Array; lo: Float32Array } {
  const hi = new Float32Array(src.length);
  const lo = new Float32Array(src.length);
  for (let i = 0; i < src.length; i++) {
    const h = Math.floor(src[i]! / HI_SPLIT) * HI_SPLIT;
    hi[i] = h;
    lo[i] = src[i]! - h;
  }
  return { hi, lo };
}

export interface BankOverlayOptions {
  /** Raw trace columns, for markers. */
  clk: BigInt64Array;
  cmdId: Uint8Array;
  addr: Int32Array[];
  /** Trace index range [lo, hi) whose clocks fall inside [t0, t1). */
  commandRange: (t0: number, t1: number) => [number, number];
  /** Title text per header row, keyed by coords.join(','). */
  headerTitles: Map<string, string>;
  headerName: (coords: number[]) => string;
}

export function createBankLayer(regl: createREGL.Regl, pixelRatio: () => number = () => 1) {
  const quad: [number, number][] = [[0, 0], [1, 0], [0, 1], [0, 1], [1, 0], [1, 1]];

  const spanHi = regl.buffer({ data: new Float32Array(2), type: 'float' });
  const spanLo = regl.buffer({ data: new Float32Array(2), type: 'float' });
  const spanKind = regl.buffer({ data: new Uint8Array(2), type: 'uint8' });

  interface IvBuffers {
    sHi: createREGL.Buffer;
    sLo: createREGL.Buffer;
    eHi: createREGL.Buffer;
    eLo: createREGL.Buffer;
  }
  const makeIvBuffers = (): IvBuffers => ({
    sHi: regl.buffer({ data: new Float32Array(1), type: 'float' }),
    sLo: regl.buffer({ data: new Float32Array(1), type: 'float' }),
    eHi: regl.buffer({ data: new Float32Array(1), type: 'float' }),
    eLo: regl.buffer({ data: new Float32Array(1), type: 'float' }),
  });
  const ivBufs = { bus: makeIvBuffers(), bg: makeIvBuffers(), faw: makeIvBuffers() };

  const kindColors = {
    u_cIdle: hex2rgb(BANK_COLORS.idle),
    u_cAct: hex2rgb(BANK_COLORS.act),
    u_cPre: hex2rgb(BANK_COLORS.pre),
    u_cWrrec: hex2rgb(BANK_COLORS.wrrec),
    u_cRef: hex2rgb(BANK_COLORS.ref),
    u_cOpen: hex2rgb(BANK_COLORS.open),
  };

  interface ViewProps {
    baseHi: number;
    baseLo: number;
    viewW: number;
    offset: number;
    instances: number;
  }
  interface SpanProps extends ViewProps {
    yTop: number;
    yH: number;
  }
  interface IvProps extends ViewProps {
    yTop: number;
    yBot: number;
    color: [number, number, number];
    minPx: number;
  }

  const viewUniforms = {
    u_baseHi: ((_: any, p: ViewProps) => p.baseHi) as any,
    u_baseLo: ((_: any, p: ViewProps) => p.baseLo) as any,
    u_viewW: ((_: any, p: ViewProps) => p.viewW) as any,
    u_res: ((ctx: any) => [ctx.viewportWidth / pixelRatio(), ctx.viewportHeight / pixelRatio()]) as any,
  };

  const makeSpanDraw = (openPass: boolean) => regl({
    vert: SPAN_VERT,
    frag: COLOR_FRAG,
    attributes: {
      a_quad: quad,
      a_sHi: { buffer: spanHi, divisor: 1, offset: ((_: any, p: SpanProps) => p.offset * 4) as any },
      a_sLo: { buffer: spanLo, divisor: 1, offset: ((_: any, p: SpanProps) => p.offset * 4) as any },
      a_eHi: { buffer: spanHi, divisor: 1, offset: ((_: any, p: SpanProps) => (p.offset + 1) * 4) as any },
      a_eLo: { buffer: spanLo, divisor: 1, offset: ((_: any, p: SpanProps) => (p.offset + 1) * 4) as any },
      a_kind: { buffer: spanKind, divisor: 1, normalized: false, offset: ((_: any, p: SpanProps) => p.offset) as any },
    },
    uniforms: {
      ...viewUniforms,
      ...kindColors,
      u_yTop: ((_: any, p: SpanProps) => p.yTop) as any,
      u_yH: ((_: any, p: SpanProps) => p.yH) as any,
      u_openPass: openPass ? 1 : 0,
    },
    // Open spans mark their pixels in the stencil so the blocked shading only lands on open rows.
    stencil: openPass
      ? {
          enable: true,
          mask: 0xff,
          func: { cmp: 'always', ref: 1, mask: 0xff },
          op: { fail: 'keep', zfail: 'keep', zpass: 'replace' },
        }
      : { enable: false },
    depth: { enable: false },
    instances: ((_: any, p: SpanProps) => p.instances) as any,
    count: 6,
  });

  const makeIvDraw = (b: IvBuffers, stencilTest: boolean) => regl({
    vert: IV_VERT,
    frag: COLOR_FRAG,
    attributes: {
      a_quad: quad,
      a_sHi: { buffer: b.sHi, divisor: 1, offset: ((_: any, p: IvProps) => p.offset * 4) as any },
      a_sLo: { buffer: b.sLo, divisor: 1, offset: ((_: any, p: IvProps) => p.offset * 4) as any },
      a_eHi: { buffer: b.eHi, divisor: 1, offset: ((_: any, p: IvProps) => p.offset * 4) as any },
      a_eLo: { buffer: b.eLo, divisor: 1, offset: ((_: any, p: IvProps) => p.offset * 4) as any },
    },
    uniforms: {
      ...viewUniforms,
      u_yTop: ((_: any, p: IvProps) => p.yTop) as any,
      u_yBot: ((_: any, p: IvProps) => p.yBot) as any,
      u_color: ((_: any, p: IvProps) => p.color) as any,
      u_minPx: ((_: any, p: IvProps) => p.minPx) as any,
    },
    stencil: stencilTest
      ? {
          enable: true,
          mask: 0x00,
          func: { cmp: 'equal', ref: 1, mask: 0xff },
          op: { fail: 'keep', zfail: 'keep', zpass: 'keep' },
        }
      : { enable: false },
    depth: { enable: false },
    instances: ((_: any, p: IvProps) => p.instances) as any,
    count: 6,
  });

  const drawSpansBase = makeSpanDraw(false);
  const drawSpansOpen = makeSpanDraw(true);
  const drawBgMasked = makeIvDraw(ivBufs.bg, true);
  const drawBusMasked = makeIvDraw(ivBufs.bus, true);
  const drawBusLane = makeIvDraw(ivBufs.bus, false);
  const drawFawLane = makeIvDraw(ivBufs.faw, false);

  const colBus = hex2rgb(BANK_COLORS.bus);
  const colBg = hex2rgb(BANK_COLORS.bg);
  const colBusLane = hex2rgb(BANK_COLORS.busLane);
  const colFawLane = hex2rgb(BANK_COLORS.fawLane);

  let bs: BankStateData | null = null;
  let geom: BankGeometry | null = null;

  function uploadIntervals(b: IvBuffers, set: BankIntervalSet | null) {
    const s = splitHiLo(set ? set.start : new Float64Array(1));
    const e = splitHiLo(set ? set.end : new Float64Array(1));
    b.sHi({ data: s.hi, type: 'float' });
    b.sLo({ data: s.lo, type: 'float' });
    b.eHi({ data: e.hi, type: 'float' });
    b.eLo({ data: e.lo, type: 'float' });
  }

  function setData(data: BankStateData | null) {
    bs = data;
    geom = null;
    if (!data) return;
    const { hi, lo } = splitHiLo(data.spanStart);
    spanHi({ data: hi, type: 'float' });
    spanLo({ data: lo, type: 'float' });
    spanKind({ data: data.spanKind, type: 'uint8' });
    uploadIntervals(ivBufs.bus, data.bus);
    uploadIntervals(ivBufs.bg, data.bg);
    uploadIntervals(ivBufs.faw, data.faw);
  }

  function setLayout(rows: RowLayout[], canvasTop: number) {
    geom = bs ? bankGeometry(bs, rows, canvasTop) : null;
  }

  function draw(view: BankViewWindow) {
    if (!bs || !geom) return;
    const baseHi = Math.floor(view.start / HI_SPLIT) * HI_SPLIT;
    const baseLo = view.start - baseHi;
    const viewW = view.duration;
    const t0 = view.start;
    const t1 = view.start + view.duration;
    const vp = { baseHi, baseLo, viewW };

    const spanBatch: SpanProps[] = [];
    for (const lane of geom.visibleLanes) {
      const [lo, hi] = bankSpanRange(bs, lane, t0, t1);
      if (hi <= lo) continue;
      const h = geom.laneH[lane]!;
      const inset = bankLaneInset(h);
      spanBatch.push({ ...vp, offset: lo, instances: hi - lo, yTop: geom.laneTop[lane]! + inset, yH: h - 2 * inset });
    }
    if (spanBatch.length) {
      drawSpansBase(spanBatch);
      drawSpansOpen(spanBatch);
    }

    const ivBatch = (set: BankIntervalSet | null, extents: Map<number, [number, number]>, color: [number, number, number]) => {
      const out: IvProps[] = [];
      if (!set) return out;
      for (const [d, [yTop, yBot]] of extents) {
        const [lo, hi] = bankIntervalRange(set, d, t0, t1);
        if (hi > lo) out.push({ ...vp, offset: lo, instances: hi - lo, yTop, yBot, color, minPx: 0 });
      }
      return out;
    };
    // Bank-group blocking first, bus-busy on top: bank-group shading shows only where the bus is free.
    const bgB = ivBatch(bs.bg, geom.bgExtent, colBg);
    if (bgB.length) drawBgMasked(bgB);
    const busB = ivBatch(bs.bus, geom.busExtent, colBus);
    if (busB.length) drawBusMasked(busB);

    const laneBatch = (set: BankIntervalSet | null, lanes: Map<number, { top: number; height: number }>, color: [number, number, number]) => {
      const out: IvProps[] = [];
      if (!set) return out;
      for (const [d, r] of lanes) {
        const [lo, hi] = bankIntervalRange(set, d, t0, t1);
        const inset = bankLaneInset(r.height);
        if (hi > lo) out.push({ ...vp, offset: lo, instances: hi - lo, yTop: r.top + inset, yBot: r.top + r.height - inset, color, minPx: 1 });
      }
      return out;
    };
    const busL = laneBatch(bs.bus, geom.busLane, colBusLane);
    if (busL.length) drawBusLane(busL);
    const fawL = laneBatch(bs.faw, geom.fawLane, colFawLane);
    if (fawL.length) drawFawLane(fawL);
  }

  function drawOverlay(ctx: CanvasRenderingContext2D, view: BankViewWindow, width: number, height: number, opts: BankOverlayOptions) {
    if (!bs || !geom) return;
    const L = bs.layout;
    const xOf = (t: number) => ((t - view.start) / view.duration) * width;

    // Group rows (Channel … BankGroup): a light band with the domain's panel title.
    ctx.save();
    ctx.font = '11px ui-monospace, IBM Plex Mono, monospace';
    ctx.textBaseline = 'middle';
    ctx.textAlign = 'left';
    for (const h of geom.headers) {
      ctx.fillStyle = PAPER.groupBand;
      ctx.fillRect(0, h.top, width, h.height);
      const title = opts.headerTitles.get(h.coords.join(','));
      ctx.fillStyle = '#27272a';
      ctx.fillText(`${opts.headerName(h.coords)}${title ? `  —  ${title}` : ''}`, 8, h.top + h.height / 2);
    }
    // Summary lanes are ruled off from the rows below them, as in the Commands view.
    ctx.fillStyle = PAPER.summaryRule;
    for (const y of geom.summaryRules) ctx.fillRect(0, Math.round(y) - 1, width, 1.5);
    ctx.restore();

    // Command markers (ACT ▲, RD |, RDA ■, WR/WRA/PRE/REF).
    const t0 = view.start;
    const t1 = view.start + view.duration;
    const [lo, hi] = opts.commandRange(t0 - 1, t1 + 1);
    if (hi - lo > MARKER_MAX_COMMANDS) {
      ctx.save();
      ctx.font = '10px ui-monospace, IBM Plex Mono, monospace';
      ctx.textAlign = 'right';
      ctx.textBaseline = 'bottom';
      ctx.fillStyle = 'rgba(39,39,42,0.75)';
      ctx.fillText(`${(hi - lo).toLocaleString()} commands in view — zoom in to see command markers`, width - 8, height - 6);
      ctx.restore();
      return;
    }
    const targets = bankTargetResolver(L, opts.addr);
    const rules = bs.rules.cmd;
    ctx.save();
    for (let i = lo; i < hi; i++) {
      const m = rules[opts.cmdId[i]!]?.marker ?? BANK_MARKER.NONE;
      if (m === BANK_MARKER.NONE) continue;
      const x = xOf(Number(opts.clk[i]));
      if (x < -8 || x > width + 8) continue;
      const lanes = targets(i);
      ctx.fillStyle = BANK_MARKER_COLORS[m]!;
      ctx.strokeStyle = BANK_MARKER_COLORS[m]!;
      for (let j = 0; j < lanes.length; j++) {
        const lane = lanes[j]!;
        const top = geom.laneTop[lane]!;
        if (Number.isNaN(top)) continue;
        const h = geom.laneH[lane]!;
        drawMarker(ctx, m, x, top + h / 2, h);
      }
    }
    ctx.restore();
  }

  function destroy() {
    for (const b of [spanHi, spanLo, spanKind]) b.destroy();
    for (const s of Object.values(ivBufs)) for (const b of Object.values(s)) b.destroy();
    bs = null;
    geom = null;
  }

  return {
    setData,
    setLayout,
    draw,
    drawOverlay,
    destroy,
    geometry: () => geom,
  };
}

export type BankLayer = ReturnType<typeof createBankLayer>;

function drawMarker(ctx: CanvasRenderingContext2D, m: number, x: number, y: number, h: number) {
  const s = Math.max(3, Math.min(5, h * 0.22));
  switch (m) {
    case BANK_MARKER.ACT:
    case BANK_MARKER.ACT2:
      ctx.beginPath();
      ctx.moveTo(x, y - s * 1.1);
      ctx.lineTo(x + s, y + s * 0.8);
      ctx.lineTo(x - s, y + s * 0.8);
      ctx.closePath();
      if (m === BANK_MARKER.ACT) ctx.fill();
      else {
        ctx.lineWidth = 1;
        ctx.stroke();
      }
      break;
    case BANK_MARKER.RD:
    case BANK_MARKER.WR:
      ctx.lineWidth = 1.4;
      ctx.beginPath();
      ctx.moveTo(Math.round(x) + 0.5, y - h * 0.32);
      ctx.lineTo(Math.round(x) + 0.5, y + h * 0.32);
      ctx.stroke();
      break;
    case BANK_MARKER.RDA:
    case BANK_MARKER.WRA:
      ctx.fillRect(x - s * 0.8, y - s * 0.8, s * 1.6, s * 1.6);
      break;
    case BANK_MARKER.PRE:
      ctx.beginPath();
      ctx.moveTo(x, y + s * 0.9);
      ctx.lineTo(x + s * 0.75, y - s * 0.5);
      ctx.lineTo(x - s * 0.75, y - s * 0.5);
      ctx.closePath();
      ctx.fill();
      break;
    case BANK_MARKER.REF:
      ctx.beginPath();
      ctx.moveTo(x, y - s);
      ctx.lineTo(x + s * 0.8, y);
      ctx.lineTo(x, y + s);
      ctx.lineTo(x - s * 0.8, y);
      ctx.closePath();
      ctx.fill();
      break;
    default:
      break;
  }
}

/** Panel-title text: data-bus and tFAW utilisation, then the bank-time breakdown. */
export function bankTitle(
  parts: { busPct: number | null; fawPct: number | null },
  shares: { starved: number; bus: number; bg: number; act: number; pre: number; idle: number; wrrec: number; ref: number },
  opts: { hasBg: boolean; hasWrrec: boolean; hasRef: boolean },
): string {
  const f = (v: number) => `${Math.round(v)}%`;
  const head: string[] = [];
  if (parts.busPct !== null) head.push(`data bus ${f(parts.busPct)} utilized`);
  if (parts.fawPct !== null) head.push(`ACT blocked by tFAW ${f(parts.fawPct)} of the time`);
  const bt = [`open-but-starved ${f(shares.starved)}`, `bus busy ${f(shares.bus)}`];
  if (opts.hasBg) bt.push(`BG blocked ${f(shares.bg)}`);
  bt.push(`activating ${f(shares.act)}`, `precharging ${f(shares.pre)}`);
  if (opts.hasWrrec) bt.push(`write recovery ${f(shares.wrrec)}`);
  if (opts.hasRef) bt.push(`refreshing ${f(shares.ref)}`);
  bt.push(`closed-idle ${f(shares.idle)}`);
  return `${head.length ? `${head.join(', ')}  ·  ` : ''}bank-time: ${bt.join(' | ')}`;
}
