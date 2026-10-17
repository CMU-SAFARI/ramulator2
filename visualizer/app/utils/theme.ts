/**
 * Canvas colours of the RamViz paper theme. Chrome colours are CSS tokens (`--rv-*`) in
 * assets/css/main.css; the WebGL and 2D canvases cannot read those, so they use these.
 */
export const PAPER = {
  /** WebGL clear colour. */
  bg: [1, 1, 1, 1] as [number, number, number, number],
  ink: '#1f2328',
  muted: '#59636e',
  accent: '#1f5fb4',
  accentRgb: [31 / 255, 95 / 255, 180 / 255] as [number, number, number],
  /** Expanded group rows (PseudoChannel, Rank, BankGroup...) in the Commands / Requests views. */
  groupBand: '#f6f7f8',
  /** Rule between summary lanes and per-bank lanes (as in the Bank state view). */
  summaryRule: '#444444',
  /** Vertical gridlines at major ticks. */
  vgrid: 'rgba(31, 35, 40, 0.06)',
  rangeFill: 'rgba(31, 95, 180, 0.07)',
  rangeEdge: 'rgba(31, 95, 180, 0.45)',
  cursor: 'rgba(191, 90, 0, 0.95)',
  cursorFill: 'rgba(191, 90, 0, 0.10)',
  /** Text drawn on top of command bars. */
  barLabel: 'rgba(255, 255, 255, 0.95)',
  barLabelShadow: 'rgba(0, 0, 0, 0.35)',
  tp: {
    data: 'rgba(31, 95, 180, 0.95)',
    dataFill: 'rgba(31, 95, 180, 0.10)',
    cmd: 'rgba(194, 65, 12, 0.95)',
    cmdFill: 'rgba(194, 65, 12, 0.07)',
    axis: '#59636e',
    grid: 'rgba(31, 35, 40, 0.07)',
    rangeFill: 'rgba(31, 95, 180, 0.06)',
    markerFill: '#ffffff',
    markerHalo: 'rgba(31, 95, 180, 0.18)',
  },
  overview: {
    tick: '#d0d7de',
    label: '#59636e',
    fill: 'rgba(31, 95, 180, 0.16)',
    line: 'rgba(31, 95, 180, 0.55)',
    dim: 'rgba(246, 247, 248, 0.72)',
    window: '#1f2328',
    handle: '#1f2328',
    grip: 'rgba(255, 255, 255, 0.8)',
    cursorFill: 'rgba(191, 90, 0, 0.12)',
    cursor: 'rgba(191, 90, 0, 0.95)',
  },
} as const;
