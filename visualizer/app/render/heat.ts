// Zoomed-out drawing: one texture row per sidebar row and one column per pixel, filled from the
// tile summaries. Used where there are too many bars to draw, and for tiles still loading.
import type createREGL from 'regl';
import { BANK_COLORS, bankKindColor } from '~/utils/bankState';
import { hex2rgb } from '~/utils/commandColors';
import type { TraceEngine } from '~/engine/client';
import { BLOCK_KINDS, SUBBINS, TILE } from '~/engine/types';

export type HeatKind = 'cmd' | 'data' | 'cmdsum' | 'req' | 'bank' | 'bkbus' | 'bkfaw';

export interface HeatRow {
  kind: HeatKind;
  /** Canvas y of the row's band and its height. */
  y: number;
  h: number;
  /** Lanes [first, first + count) (banks for bank rows); the domain for bkbus / bkfaw. */
  first: number;
  count: number;
}

export interface HeatColors {
  /** RGB (0-1) of each command id. */
  cmd: (id: number) => [number, number, number];
  read: [number, number, number];
  write: [number, number, number];
  other: [number, number, number];
  data: [number, number, number];
  request: [number, number, number];
}

export interface HeatImage {
  t0: number;
  t1: number;
  width: number;
  rows: number;
  data: Uint8Array;
}

const KIND_RGB = Array.from({ length: BLOCK_KINDS }, (_, k) => hex2rgb(bankKindColor(k)));
const BUS_RGB = hex2rgb(BANK_COLORS.busLane);
const FAW_RGB = hex2rgb(BANK_COLORS.fawLane);

/**
 * Build the image of [t0, t1) at `width` columns from the records in `records` (indices into
 * engine.tiles). Rows of different kinds read different summaries.
 */
export function buildHeat(
  e: TraceEngine, records: number[], rows: HeatRow[], t0: number, t1: number, width: number, colors: HeatColors,
): HeatImage {
  const R = rows.length;
  const acc = new Float32Array(R * width * 5); // r, g, b, weight, alpha
  const pxPerCycle = width / (t1 - t0);
  for (const i of records) {
    const r = e.tiles[i]!;
    const subW = (r.span * TILE) / SUBBINS;
    const origin = r.k * TILE;
    for (let sb = 0; sb < SUBBINS; sb++) {
      const a = origin + sb * subW;
      const b = a + subW;
      if (b <= t0 || a >= t1) continue;
      const x0 = Math.max(0, Math.floor((a - t0) * pxPerCycle));
      const x1 = Math.min(width, Math.max(x0 + 1, Math.ceil((b - t0) * pxPerCycle)));
      for (let ri = 0; ri < R; ri++) {
        const row = rows[ri]!;
        let w = 0;
        let alpha = 0;
        let c: [number, number, number] = colors.other;
        let mix: [number, number, number] | null = null;
        switch (row.kind) {
          case 'cmd': {
            let best = -1;
            let top = 255;
            for (let l = row.first; l < row.first + row.count; l++) {
              const n = r.laneCnt[l * SUBBINS + sb]!;
              w += n;
              if (n > best) {
                best = n;
                top = r.laneTop[l * SUBBINS + sb]!;
              }
            }
            if (w > 0) {
              c = colors.cmd(top);
              alpha = 1;
            }
            break;
          }
          case 'data': {
            let busy = 0;
            for (let l = row.first; l < row.first + row.count; l++) busy += r.laneBusy[l * SUBBINS + sb]!;
            if (busy > 0) {
              w = busy;
              c = colors.data;
              alpha = 0.35 + 0.65 * Math.min(1, busy / (subW * Math.max(1, row.count) * 0.25));
            }
            break;
          }
          case 'cmdsum': {
            const rd = r.subReads[sb]!;
            const wr = r.subWrites[sb]!;
            const ot = r.subOthers[sb]!;
            w = rd + wr + ot;
            if (w > 0) {
              mix = [
                (rd * colors.read[0] + wr * colors.write[0] + ot * colors.other[0]) / w,
                (rd * colors.read[1] + wr * colors.write[1] + ot * colors.other[1]) / w,
                (rd * colors.read[2] + wr * colors.write[2] + ot * colors.other[2]) / w,
              ];
              alpha = 1;
            }
            break;
          }
          case 'req': {
            let n = 0;
            for (let l = row.first; l < row.first + row.count; l++) n += r.laneReqs[l * SUBBINS + sb]!;
            if (n > 0) {
              w = n;
              c = colors.request;
              alpha = 0.3 + 0.7 * Math.min(1, n / (4 * Math.max(1, row.count)));
            }
            break;
          }
          case 'bank': {
            const k6 = r.bankKinds;
            if (!k6) break;
            const m: [number, number, number] = [0, 0, 0];
            for (let l = row.first; l < row.first + row.count; l++) {
              for (let k = 0; k < BLOCK_KINDS; k++) {
                const v = k6[(l * SUBBINS + sb) * BLOCK_KINDS + k]!;
                if (!v) continue;
                w += v;
                m[0] += v * KIND_RGB[k]![0];
                m[1] += v * KIND_RGB[k]![1];
                m[2] += v * KIND_RGB[k]![2];
              }
            }
            if (w > 0) {
              mix = [m[0] / w, m[1] / w, m[2] / w];
              alpha = 1;
            }
            break;
          }
          case 'bkbus':
          case 'bkfaw': {
            const cov = row.kind === 'bkbus' ? r.busCovSub : r.fawCovSub;
            const v = cov ? cov[row.first * SUBBINS + sb]! / subW : 0;
            if (v > 0) {
              w = v;
              c = row.kind === 'bkbus' ? BUS_RGB : FAW_RGB;
              alpha = 0.25 + 0.75 * Math.min(1, v);
            }
            break;
          }
        }
        if (w <= 0) continue;
        const col = mix ?? c;
        for (let x = x0; x < x1; x++) {
          const o = (ri * width + x) * 5;
          acc[o]! += col[0] * w;
          acc[o + 1]! += col[1] * w;
          acc[o + 2]! += col[2] * w;
          acc[o + 3]! += w;
          if (alpha > acc[o + 4]!) acc[o + 4] = alpha;
        }
      }
    }
  }
  const data = new Uint8Array(R * width * 4);
  for (let q = 0; q < R * width; q++) {
    const w = acc[q * 5 + 3]!;
    if (w <= 0) continue;
    data[q * 4] = Math.round((acc[q * 5]! / w) * 255);
    data[q * 4 + 1] = Math.round((acc[q * 5 + 1]! / w) * 255);
    data[q * 4 + 2] = Math.round((acc[q * 5 + 2]! / w) * 255);
    data[q * 4 + 3] = Math.round(acc[q * 5 + 4]! * 255);
  }
  return { t0, t1, width, rows: R, data };
}

const HEAT_VERT = `
precision highp float;
attribute vec2 a_quad;
uniform vec2 u_res;
uniform float u_x0;
uniform float u_x1;
uniform float u_y;
uniform float u_h;
uniform float u_v;
varying vec2 v_uv;
void main() {
  float x = mix(u_x0, u_x1, a_quad.x);
  float y = u_y + a_quad.y * u_h;
  v_uv = vec2(a_quad.x, u_v);
  gl_Position = vec4(x / u_res.x * 2.0 - 1.0, 1.0 - y / u_res.y * 2.0, 0.0, 1.0);
}
`;

const HEAT_FRAG = `
precision highp float;
uniform sampler2D u_tex;
uniform float u_alpha;
varying vec2 v_uv;
void main() {
  vec4 c = texture2D(u_tex, v_uv);
  if (c.a < 0.004) discard;
  gl_FragColor = vec4(c.rgb, c.a * u_alpha);
}
`;

/** GL side of a heat image: upload once, draw every frame mapped onto the current view. */
export function createHeatLayer(regl: createREGL.Regl, pixelRatio: () => number) {
  const tex = regl.texture({ width: 1, height: 1, data: new Uint8Array(4), min: 'nearest', mag: 'nearest' });
  let img: HeatImage | null = null;
  let rows: HeatRow[] = [];
  const quad: [number, number][] = [[0, 0], [1, 0], [0, 1], [0, 1], [1, 0], [1, 1]];
  interface P { x0: number; x1: number; y: number; h: number; v: number; alpha: number }
  const draw = regl({
    vert: HEAT_VERT,
    frag: HEAT_FRAG,
    attributes: { a_quad: quad },
    uniforms: {
      u_tex: tex,
      u_res: ((ctx: any) => [ctx.viewportWidth / pixelRatio(), ctx.viewportHeight / pixelRatio()]) as any,
      u_x0: regl.prop<P, 'x0'>('x0'),
      u_x1: regl.prop<P, 'x1'>('x1'),
      u_y: regl.prop<P, 'y'>('y'),
      u_h: regl.prop<P, 'h'>('h'),
      u_v: regl.prop<P, 'v'>('v'),
      u_alpha: regl.prop<P, 'alpha'>('alpha'),
    },
    blend: {
      enable: true,
      func: { srcRGB: 'src alpha', srcAlpha: 'one', dstRGB: 'one minus src alpha', dstAlpha: 'one minus src alpha' },
    },
    depth: { enable: false },
    count: 6,
  });

  return {
    set(image: HeatImage, heatRows: HeatRow[]) {
      img = image;
      rows = heatRows;
      if (image.rows === 0 || image.width === 0) return;
      tex({ width: image.width, height: image.rows, data: image.data, min: 'nearest', mag: 'nearest' });
    },
    clear() {
      img = null;
    },
    get image() {
      return img;
    },
    /**
     * Draw the image onto the view [start, start + duration) of a canvas `width` px wide; `alpha`
     * below 1 draws it as a placeholder.
     */
    draw(start: number, duration: number, width: number, alpha = 1) {
      if (!img || img.rows === 0) return;
      const x0 = ((img.t0 - start) / duration) * width;
      const x1 = ((img.t1 - start) / duration) * width;
      draw(rows.map((r, i) => ({ x0, x1, y: r.y, h: r.h, v: (i + 0.5) / img!.rows, alpha })));
    },
    destroy() {
      tex.destroy();
    },
  };
}

export type HeatLayer = ReturnType<typeof createHeatLayer>;
