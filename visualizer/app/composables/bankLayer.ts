/**
 * Bank State view rendering, per tile: bank-state spans, the stencil-masked "open ∩ bus busy" /
 * "open ∩ bank group blocked" shading, the DATA BUS / ACT-tFAW summary lanes and the command
 * markers (WebGL), and the separators and domain titles (2D overlay).
 */
import type createREGL from 'regl';
import type { RowLayout } from '~/stores/ui';
import {
  BANK_COLORS,
  BANK_MARKER,
  BANK_MARKER_COLORS,
  bankCoordsRange,
  bankDomCount,
  bankDomOf,
  type BankLayout,
  type BankRules,
} from '~/utils/bankState';
import { hex2rgb } from '~/utils/commandColors';
import { PAPER } from '~/utils/theme';
import { SUBBINS, TILE, type TileDetail } from '~/engine/types';
import { lowerBound } from '~/engine/stats';

const SPAN_VERT = `
precision highp float;
attribute vec2 a_quad;
attribute float a_s;
attribute float a_e;
attribute float a_lane;
attribute float a_kind;
uniform sampler2D u_lanes;
uniform float u_nLanes;
uniform float u_oInt;
uniform float u_oFrac;
uniform float u_viewW;
uniform vec2 u_res;
uniform float u_openPass;
uniform vec3 u_cIdle;
uniform vec3 u_cAct;
uniform vec3 u_cPre;
uniform vec3 u_cWrrec;
uniform vec3 u_cRef;
uniform vec3 u_cOpen;
varying vec3 v_color;
void main() {
  vec4 L = texture2D(u_lanes, vec2((a_lane + 0.5) / u_nLanes, 0.5));
  bool isOpen = abs(a_kind - 5.0) < 0.5;
  if (L.b < 0.5 || (u_openPass > 0.5) != isOpen) {
    v_color = vec3(0.0);
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    return;
  }
  float xs = (a_s + u_oInt) + u_oFrac;
  float xe = (a_e + u_oInt) + u_oFrac;
  float x = clamp(mix(xs, xe, a_quad.x), -u_viewW, 2.0 * u_viewW);
  float y = L.r + a_quad.y * L.g;
  v_color = a_kind < 0.5 ? u_cIdle
    : a_kind < 1.5 ? u_cAct
    : a_kind < 2.5 ? u_cPre
    : a_kind < 3.5 ? u_cWrrec
    : a_kind < 4.5 ? u_cRef
    : u_cOpen;
  gl_Position = vec4(x / u_viewW * 2.0 - 1.0, 1.0 - y / u_res.y * 2.0, 0.0, 1.0);
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
attribute float a_s;
attribute float a_e;
attribute float a_dom;
uniform sampler2D u_doms;
uniform float u_nDoms;
uniform float u_oInt;
uniform float u_oFrac;
uniform float u_viewW;
uniform vec2 u_res;
uniform float u_minPx;
uniform vec3 u_color;
varying vec3 v_color;
void main() {
  vec4 D = texture2D(u_doms, vec2((a_dom + 0.5) / u_nDoms, 0.5));
  if (D.b < 0.5) {
    v_color = vec3(0.0);
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    return;
  }
  float xs = (a_s + u_oInt) + u_oFrac;
  float xe = (a_e + u_oInt) + u_oFrac;
  // Summary lanes keep sub-pixel bursts visible when zoomed out.
  xe = max(xe, xs + u_minPx * u_viewW / u_res.x);
  float x = clamp(mix(xs, xe, a_quad.x), -u_viewW, 2.0 * u_viewW);
  float y = mix(D.r, D.g, a_quad.y);
  v_color = u_color;
  gl_Position = vec4(x / u_viewW * 2.0 - 1.0, 1.0 - y / u_res.y * 2.0, 0.0, 1.0);
}
`;

const MARKER_VERT = `
precision highp float;
attribute vec2 a_quad;
attribute float a_t;
attribute float a_lane;
attribute float a_cmd;
uniform sampler2D u_lanes;
uniform float u_nLanes;
uniform sampler2D u_markers;
uniform float u_oInt;
uniform float u_oFrac;
uniform float u_viewW;
uniform vec2 u_res;
varying vec2 v_local;
varying vec3 v_color;
varying float v_kind;
void main() {
  vec4 M = texture2D(u_markers, vec2((a_cmd + 0.5) / 256.0, 0.5));
  vec4 L = texture2D(u_lanes, vec2((a_lane + 0.5) / u_nLanes, 0.5));
  v_kind = M.a;
  v_color = M.rgb;
  if (M.a < 0.5 || L.b < 0.5) {
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    return;
  }
  float h = L.g;
  float s = clamp(h * 0.24, 3.0, 5.5);
  float x = ((a_t + u_oInt) + u_oFrac) / u_viewW * u_res.x;
  float y = L.r + h * 0.5;
  v_local = a_quad * 2.0 - 1.0;
  vec2 p = vec2(x, y) + v_local * vec2(s + 1.0, M.a > 2.5 && M.a < 4.5 ? h * 0.36 : s + 1.0);
  gl_Position = vec4(p.x / u_res.x * 2.0 - 1.0, 1.0 - p.y / u_res.y * 2.0, 0.0, 1.0);
}
`;

// Marker kinds: 1 ACT ▲, 2 ACT2 △, 3 RD |, 4 WR |, 5 RDA ■, 6 WRA ■, 7 PRE ▼, 8 REF ◆.
const MARKER_FRAG = `
precision highp float;
varying vec2 v_local;
varying vec3 v_color;
varying float v_kind;
void main() {
  vec2 p = v_local;
  float k = floor(v_kind + 0.5);
  float inside = 0.0;
  if (k == 1.0 || k == 2.0) {
    float tri = max(abs(p.x) * 1.7 + p.y * 0.85 - 0.9, -p.y - 0.8);
    inside = k == 1.0 ? step(tri, 0.0) : step(tri, 0.0) * step(-0.22, tri);
  } else if (k == 3.0 || k == 4.0) {
    inside = step(abs(p.x), 0.16);
  } else if (k == 5.0 || k == 6.0) {
    inside = step(max(abs(p.x), abs(p.y)), 0.72);
  } else if (k == 7.0) {
    inside = step(max(abs(p.x) * 1.7 - p.y * 0.85 - 0.9, p.y - 0.8), 0.0);
  } else if (k == 8.0) {
    inside = step(abs(p.x) + abs(p.y), 0.95);
  }
  if (inside < 0.5) discard;
  gl_FragColor = vec4(v_color, 1.0);
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

export function bankGeometry(L: BankLayout, rows: RowLayout[], canvasTop: number): BankGeometry {
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

/**
 * GPU buffers of one tile's bank-state data. Spans and intervals are cut at the tile's sub-bin
 * edges and grouped by sub-bin (`bins[b]` = first piece of sub-bin b), and markers are in time
 * order, so the part of a tile in view draws as one range.
 */
export interface BankTileGpu {
  detail: TileDetail;
  bytes: number;
  binW: number;
  spans: { s: createREGL.Buffer; e: createREGL.Buffer; lane: createREGL.Buffer; kind: createREGL.Buffer; n: number; bins: Uint32Array };
  iv: ({ s: createREGL.Buffer; e: createREGL.Buffer; dom: createREGL.Buffer; n: number; bins: Uint32Array } | null)[];
  marks: { t: createREGL.Buffer; lane: createREGL.Buffer; cmd: createREGL.Buffer; n: number; at: Float32Array };
  destroy(): void;
}

/** Cut [s, e) pieces at sub-bin edges and group them by sub-bin, keeping their order within one. */
function binPieces<C extends Uint8Array | Uint16Array>(s: Float32Array, e: Float32Array, cols: C[], binW: number) {
  const last = SUBBINS - 1;
  const first = (x: number) => Math.min(last, Math.max(0, Math.floor(x / binW)));
  const lastOf = (b0: number, x: number) => Math.min(last, Math.max(b0, Math.ceil(x / binW) - 1));
  const bins = new Uint32Array(SUBBINS + 1);
  for (let j = 0; j < s.length; j++) {
    const b0 = first(s[j]!);
    for (let b = b0, b1 = lastOf(b0, e[j]!); b <= b1; b++) bins[b + 1]!++;
  }
  for (let b = 0; b < SUBBINS; b++) bins[b + 1]! += bins[b]!;
  const n = bins[SUBBINS]!;
  const os = new Float32Array(n);
  const oe = new Float32Array(n);
  const oc = cols.map(c => new (c.constructor as new (n: number) => C)(n));
  const at = bins.slice(0, SUBBINS);
  for (let j = 0; j < s.length; j++) {
    const b0 = first(s[j]!);
    for (let b = b0, b1 = lastOf(b0, e[j]!); b <= b1; b++) {
      const q = at[b]!++;
      os[q] = Math.max(s[j]!, b * binW);
      oe[q] = Math.min(e[j]!, (b + 1) * binW);
      for (let k = 0; k < cols.length; k++) oc[k]![q] = cols[k]![j]!;
    }
  }
  return { s: os, e: oe, cols: oc, bins };
}

/** Pieces of the sub-bins that overlap the tile-relative window [t0, t1). */
function binRange(bins: Uint32Array, binW: number, t0: number, t1: number): [number, number] {
  const b0 = Math.min(SUBBINS - 1, Math.max(0, Math.floor(t0 / binW)));
  const b1 = Math.min(SUBBINS - 1, Math.max(b0, Math.floor(t1 / binW)));
  return [bins[b0]!, bins[b1 + 1]!];
}

/** Markers reach this far from their time (px). */
const MARKER_REACH_PX = 8;

export function createBankLayer(regl: createREGL.Regl, L: BankLayout, rules: BankRules, pixelRatio: () => number = () => 1) {
  const quad: [number, number][] = [[0, 0], [1, 0], [0, 1], [0, 1], [1, 0], [1, 1]];
  const busDoms = bankDomCount(L, L.busLevel);
  const bgDoms = L.bgLevel !== null ? bankDomCount(L, L.bgLevel) : 1;
  const fawDoms = L.fawLevel !== null ? bankDomCount(L, L.fawLevel) : 1;
  const floatTex = (w: number) => regl.texture({ width: Math.max(1, w), height: 1, data: new Float32Array(Math.max(1, w) * 4), format: 'rgba', type: 'float', min: 'nearest', mag: 'nearest' });
  const laneTex = floatTex(L.numBanks);
  const busExtTex = floatTex(busDoms);
  const bgExtTex = floatTex(bgDoms);
  const busLaneTex = floatTex(busDoms);
  const fawLaneTex = floatTex(fawDoms);
  const markerTex = regl.texture({
    width: 256, height: 1, format: 'rgba', type: 'float', min: 'nearest', mag: 'nearest',
    data: (() => {
      const d = new Float32Array(256 * 4);
      const kindOf: Record<number, number> = {
        [BANK_MARKER.ACT]: 1, [BANK_MARKER.ACT2]: 2, [BANK_MARKER.RD]: 3, [BANK_MARKER.WR]: 4,
        [BANK_MARKER.RDA]: 5, [BANK_MARKER.WRA]: 6, [BANK_MARKER.PRE]: 7, [BANK_MARKER.REF]: 8,
      };
      rules.cmd.forEach((r, id) => {
        if (!r.marker) return;
        const [cr, cg, cb] = hex2rgb(BANK_MARKER_COLORS[r.marker]!);
        d.set([cr, cg, cb, kindOf[r.marker] ?? 0], id * 4);
      });
      return d;
    })(),
  });

  const kindColors = {
    u_cIdle: hex2rgb(BANK_COLORS.idle),
    u_cAct: hex2rgb(BANK_COLORS.act),
    u_cPre: hex2rgb(BANK_COLORS.pre),
    u_cWrrec: hex2rgb(BANK_COLORS.wrrec),
    u_cRef: hex2rgb(BANK_COLORS.ref),
    u_cOpen: hex2rgb(BANK_COLORS.open),
  };

  interface ViewProps {
    oInt: number;
    oFrac: number;
    viewW: number;
    /** First instance drawn, and how many. */
    off: number;
    n: number;
  }
  /** Where a tile sits in the view: its origin relative to the view start, and cycles per CSS pixel. */
  type Place = Omit<ViewProps, 'off' | 'n'> & { px: number };
  interface SpanProps extends ViewProps { g: BankTileGpu }
  interface IvProps extends ViewProps { s: createREGL.Buffer; e: createREGL.Buffer; dom: createREGL.Buffer; doms: createREGL.Texture2D; nDoms: number; color: [number, number, number]; minPx: number }

  const viewUniforms = {
    u_oInt: ((_: any, p: ViewProps) => p.oInt) as any,
    u_oFrac: ((_: any, p: ViewProps) => p.oFrac) as any,
    u_viewW: ((_: any, p: ViewProps) => p.viewW) as any,
    u_res: ((ctx: any) => [ctx.viewportWidth / pixelRatio(), ctx.viewportHeight / pixelRatio()]) as any,
  };

  const makeSpanDraw = (openPass: boolean) => regl({
    vert: SPAN_VERT,
    frag: COLOR_FRAG,
    attributes: {
      a_quad: quad,
      a_s: ((_: any, p: SpanProps) => ({ buffer: p.g.spans.s, divisor: 1, offset: p.off * 4 })) as any,
      a_e: ((_: any, p: SpanProps) => ({ buffer: p.g.spans.e, divisor: 1, offset: p.off * 4 })) as any,
      a_lane: ((_: any, p: SpanProps) => ({ buffer: p.g.spans.lane, divisor: 1, normalized: false, offset: p.off * 2 })) as any,
      a_kind: ((_: any, p: SpanProps) => ({ buffer: p.g.spans.kind, divisor: 1, normalized: false, offset: p.off })) as any,
    },
    uniforms: {
      ...viewUniforms,
      ...kindColors,
      u_lanes: laneTex,
      u_nLanes: L.numBanks,
      u_openPass: openPass ? 1 : 0,
    },
    // Open spans mark their pixels in the stencil so the blocked shading only lands on open rows.
    stencil: openPass
      ? { enable: true, mask: 0xff, func: { cmp: 'always', ref: 1, mask: 0xff }, op: { fail: 'keep', zfail: 'keep', zpass: 'replace' } }
      : { enable: false },
    depth: { enable: false },
    instances: ((_: any, p: SpanProps) => p.n) as any,
    count: 6,
  });

  const makeIvDraw = (stencilTest: boolean) => regl({
    vert: IV_VERT,
    frag: COLOR_FRAG,
    attributes: {
      a_quad: quad,
      a_s: ((_: any, p: IvProps) => ({ buffer: p.s, divisor: 1, offset: p.off * 4 })) as any,
      a_e: ((_: any, p: IvProps) => ({ buffer: p.e, divisor: 1, offset: p.off * 4 })) as any,
      a_dom: ((_: any, p: IvProps) => ({ buffer: p.dom, divisor: 1, normalized: false, offset: p.off * 2 })) as any,
    },
    uniforms: {
      ...viewUniforms,
      u_doms: ((_: any, p: IvProps) => p.doms) as any,
      u_nDoms: ((_: any, p: IvProps) => p.nDoms) as any,
      u_color: ((_: any, p: IvProps) => p.color) as any,
      u_minPx: ((_: any, p: IvProps) => p.minPx) as any,
    },
    stencil: stencilTest
      ? { enable: true, mask: 0x00, func: { cmp: 'equal', ref: 1, mask: 0xff }, op: { fail: 'keep', zfail: 'keep', zpass: 'keep' } }
      : { enable: false },
    depth: { enable: false },
    instances: ((_: any, p: IvProps) => p.n) as any,
    count: 6,
  });

  interface MarkProps extends ViewProps { g: BankTileGpu }
  const drawMarks = regl({
    vert: MARKER_VERT,
    frag: MARKER_FRAG,
    attributes: {
      a_quad: quad,
      a_t: ((_: any, p: MarkProps) => ({ buffer: p.g.marks.t, divisor: 1, offset: p.off * 4 })) as any,
      a_lane: ((_: any, p: MarkProps) => ({ buffer: p.g.marks.lane, divisor: 1, normalized: false, offset: p.off * 2 })) as any,
      a_cmd: ((_: any, p: MarkProps) => ({ buffer: p.g.marks.cmd, divisor: 1, normalized: false, offset: p.off })) as any,
    },
    uniforms: { ...viewUniforms, u_lanes: laneTex, u_nLanes: L.numBanks, u_markers: markerTex },
    depth: { enable: false },
    instances: ((_: any, p: MarkProps) => p.n) as any,
    count: 6,
  });

  const drawSpansBase = makeSpanDraw(false);
  const drawSpansOpen = makeSpanDraw(true);
  const drawMasked = makeIvDraw(true);
  const drawLane = makeIvDraw(false);

  const colBus = hex2rgb(BANK_COLORS.bus);
  const colBg = hex2rgb(BANK_COLORS.bg);
  const colBusLane = hex2rgb(BANK_COLORS.busLane);
  const colFawLane = hex2rgb(BANK_COLORS.fawLane);

  let geom: BankGeometry | null = null;

  function setLayout(rows: RowLayout[], canvasTop: number) {
    geom = bankGeometry(L, rows, canvasTop);
    const lanes = new Float32Array(L.numBanks * 4);
    for (let b = 0; b < L.numBanks; b++) {
      const top = geom.laneTop[b]!;
      if (Number.isNaN(top)) continue;
      const h = geom.laneH[b]!;
      const inset = bankLaneInset(h);
      lanes.set([top + inset, h - 2 * inset, 1, 0], b * 4);
    }
    laneTex({ width: L.numBanks, height: 1, data: lanes, format: 'rgba', type: 'float' });
    const fill = (tex: createREGL.Texture2D, n: number, set: (out: Float32Array) => void) => {
      const d = new Float32Array(Math.max(1, n) * 4);
      set(d);
      tex({ width: Math.max(1, n), height: 1, data: d, format: 'rgba', type: 'float' });
    };
    fill(busExtTex, busDoms, d => geom!.busExtent.forEach(([a, b], k) => d.set([a, b, 1, 0], k * 4)));
    fill(bgExtTex, bgDoms, d => geom!.bgExtent.forEach(([a, b], k) => d.set([a, b, 1, 0], k * 4)));
    const laneRows = (d: Float32Array, m: Map<number, { top: number; height: number }>) => m.forEach((r, k) => {
      const inset = bankLaneInset(r.height);
      d.set([r.top + inset, r.top + r.height - inset, 1, 0], k * 4);
    });
    fill(busLaneTex, busDoms, d => laneRows(d, geom!.busLane));
    fill(fawLaneTex, fawDoms, d => laneRows(d, geom!.fawLane));
  }

  /** Upload a tile's bank data. */
  function upload(d: TileDetail): BankTileGpu {
    const bufs: createREGL.Buffer[] = [];
    const buf = (data: ArrayBufferView, type: 'float' | 'uint8' | 'uint16') => {
      const b = regl.buffer({ data: data.byteLength ? data as any : new (data.constructor as any)(1), type });
      bufs.push(b);
      return b;
    };
    const binW = (d.span * TILE) / SUBBINS;
    const sp = binPieces(d.spanS, d.spanE, [d.spanLane, d.spanKind], binW);
    const iv = [d.bus, d.bg, d.faw].map((p) => {
      if (!p || !p.s.length) return null;
      const c = binPieces(p.s, p.e, [p.dom], binW);
      return { s: buf(c.s, 'float'), e: buf(c.e, 'float'), dom: buf(c.cols[0]!, 'uint16'), n: c.s.length, bins: c.bins };
    });
    // Markers: every command instance on its bank lane, merged into time order (hidden-lane
    // commands have no marker).
    const nc = d.cmdT.length;
    const nf = d.fanT.length;
    const n = nc + nf;
    const t = new Float32Array(n);
    const lane = new Uint16Array(n);
    const cmd = new Uint8Array(n);
    for (let q = 0, i = 0, f = 0; q < n; q++) {
      if (f >= nf || (i < nc && d.cmdT[i]! <= d.fanT[f]!)) {
        t[q] = d.cmdT[i]!;
        lane[q] = d.cmdLane[i]!;
        cmd[q] = d.cmdId[i++]!;
      } else {
        t[q] = d.fanT[f]!;
        lane[q] = d.fanLane[f]!;
        cmd[q] = d.fanCmd[f++]!;
      }
      if (lane[q]! >= L.numBanks) cmd[q] = 255;
    }
    const g: BankTileGpu = {
      detail: d,
      bytes: 0,
      binW,
      spans: { s: buf(sp.s, 'float'), e: buf(sp.e, 'float'), lane: buf(sp.cols[0]!, 'uint16'), kind: buf(sp.cols[1]!, 'uint8'), n: sp.s.length, bins: sp.bins },
      iv,
      marks: { t: buf(t, 'float'), lane: buf(lane, 'uint16'), cmd: buf(cmd, 'uint8'), n, at: t },
      destroy: () => bufs.forEach(b => b.destroy()),
    };
    g.bytes = sp.s.length * 11 + n * 11 + iv.reduce((s, x) => s + (x ? x.n * 10 : 0), 0);
    return g;
  }

  /** The tile-relative window in view, widened by `reachPx` on both sides. */
  function windowOf(v: Place, reachPx: number): [number, number] {
    const t0 = -(v.oInt + v.oFrac);
    return [t0 - reachPx * v.px, t0 + v.viewW + reachPx * v.px];
  }

  /** Draw the part of one tile's spans and shading in view; `v` places the tile's origin. */
  function drawTile(g: BankTileGpu, v: Place) {
    if (!geom) return;
    const [t0, t1] = windowOf(v, 1);
    const [a, b] = binRange(g.spans.bins, g.binW, t0, t1);
    if (b > a) {
      drawSpansBase({ ...v, off: a, n: b - a, g });
      drawSpansOpen({ ...v, off: a, n: b - a, g });
    }
    const [bus, bg] = g.iv;
    // Bank-group blocking first, bus-busy on top: bank-group shading shows only where the bus is free.
    if (bg) drawIv(drawMasked, bg, g.binW, t0, t1, { ...v, doms: bgExtTex, nDoms: bgDoms, color: colBg, minPx: 0 });
    if (bus) drawIv(drawMasked, bus, g.binW, t0, t1, { ...v, doms: busExtTex, nDoms: busDoms, color: colBus, minPx: 0 });
  }

  function drawIv(
    draw: typeof drawMasked,
    p: NonNullable<BankTileGpu['iv'][number]>,
    binW: number,
    t0: number,
    t1: number,
    props: Omit<IvProps, 'off' | 'n' | 's' | 'e' | 'dom'>,
  ) {
    const [a, b] = binRange(p.bins, binW, t0, t1);
    if (b > a) draw({ ...props, off: a, n: b - a, s: p.s, e: p.e, dom: p.dom });
  }

  /** The DATA BUS and ACT / tFAW summary lanes of one tile. */
  function drawTileLanes(g: BankTileGpu, v: Place) {
    const [t0, t1] = windowOf(v, 1);
    const [bus, , faw] = g.iv;
    if (bus) drawIv(drawLane, bus, g.binW, t0, t1, { ...v, doms: busLaneTex, nDoms: busDoms, color: colBusLane, minPx: 1 });
    if (faw) drawIv(drawLane, faw, g.binW, t0, t1, { ...v, doms: fawLaneTex, nDoms: fawDoms, color: colFawLane, minPx: 1 });
  }

  function drawTileMarkers(g: BankTileGpu, v: Place) {
    const [t0, t1] = windowOf(v, MARKER_REACH_PX);
    const a = lowerBound(g.marks.at, t0);
    const b = lowerBound(g.marks.at, t1, a);
    if (b > a) drawMarks({ ...v, off: a, n: b - a, g });
  }

  /** Separators and domain titles. */
  function drawOverlay(ctx: CanvasRenderingContext2D, width: number, headerTitles: Map<string, string>, headerName: (coords: number[]) => string) {
    if (!geom) return;
    ctx.save();
    ctx.font = '11px ui-monospace, IBM Plex Mono, monospace';
    ctx.textBaseline = 'middle';
    ctx.textAlign = 'left';
    for (const h of geom.headers) {
      ctx.fillStyle = PAPER.groupBand;
      ctx.fillRect(0, h.top, width, h.height);
      const title = headerTitles.get(h.coords.join(','));
      ctx.fillStyle = '#27272a';
      ctx.fillText(fitText(ctx, `${headerName(h.coords)}${title ? `  —  ${title}` : ''}`, width - 16), 8, h.top + h.height / 2);
    }
    // Summary lanes are ruled off from the rows below them, as in the Commands view.
    ctx.fillStyle = PAPER.summaryRule;
    for (const y of geom.summaryRules) ctx.fillRect(0, Math.round(y) - 1, width, 1.5);
    ctx.restore();
  }

  function destroy() {
    for (const t of [laneTex, busExtTex, bgExtTex, busLaneTex, fawLaneTex, markerTex]) t.destroy();
    geom = null;
  }

  return {
    setLayout,
    upload,
    drawTile,
    drawTileLanes,
    drawTileMarkers,
    drawOverlay,
    destroy,
    geometry: () => geom,
  };
}

export type BankLayer = ReturnType<typeof createBankLayer>;

const fitCache = new Map<string, string>();

/** `text` shortened to fit `maxW` px: whole " | " items are dropped first, then characters. */
function fitText(ctx: CanvasRenderingContext2D, text: string, maxW: number): string {
  if (ctx.measureText(text).width <= maxW) return text;
  const key = `${ctx.font}|${Math.round(maxW)}|${text}`;
  const cached = fitCache.get(key);
  if (cached !== undefined) return cached;
  let out = text;
  const parts = text.split(' | ');
  for (let n = parts.length - 1; n >= 1; n--) {
    out = `${parts.slice(0, n).join(' | ')} | …`;
    if (ctx.measureText(out).width <= maxW) break;
  }
  while (out.length > 1 && ctx.measureText(out).width > maxW) out = `${out.slice(0, -2)}…`;
  if (fitCache.size > 1000) fitCache.clear();
  fitCache.set(key, out);
  return out;
}

/** Panel-title text: data-bus and tFAW utilisation, then the bank-time breakdown. */
export function bankTitle(
  parts: { busPct: number | null; fawPct: number | null },
  shares: { starved: number; bus: number; bg: number; act: number; pre: number; idle: number; wrrec: number; ref: number },
  opts: { hasBg: boolean; hasWrrec: boolean; hasRef: boolean; range?: boolean; unread?: 'all' | 'part' },
): string {
  // Figures for a selected range rather than for what is on screen say so.
  const scope = opts.range ? 'selected range: ' : '';
  // Past what the trace has been read up to there is nothing to count yet; a window reaching past it
  // is counted only up to there.
  if (opts.unread === 'all') return `${scope}not read yet`;
  const f = (v: number) => `${opts.unread ? '≈ ' : ''}${Math.round(v)}%`;
  const head: string[] = [];
  if (parts.busPct !== null) head.push(`data bus ${f(parts.busPct)} utilized`);
  if (parts.fawPct !== null) head.push(`ACT blocked by tFAW ${f(parts.fawPct)} of the time`);
  const bt = [`open-but-starved ${f(shares.starved)}`, `bus busy ${f(shares.bus)}`];
  if (opts.hasBg) bt.push(`BG blocked ${f(shares.bg)}`);
  bt.push(`activating ${f(shares.act)}`, `precharging ${f(shares.pre)}`);
  if (opts.hasWrrec) bt.push(`write recovery ${f(shares.wrrec)}`);
  if (opts.hasRef) bt.push(`refreshing ${f(shares.ref)}`);
  bt.push(`closed-idle ${f(shares.idle)}`);
  return `${scope}${head.length ? `${head.join(', ')}  ·  ` : ''}bank-time: ${bt.join(' | ')}`;
}
