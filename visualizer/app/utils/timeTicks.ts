/**
 * Ticks of the time axis, shared by the axis under the panels and the panels' grid lines so they
 * line up. Steps are round in the unit shown (clk, or ns with the ns setting), labels have
 * thousands separators, and the steps widen until the labels fit.
 */

/** Width of one label character (10 px IBM Plex Mono) and the room kept between labels. */
const CHAR_PX = 6.2;
const LABEL_GAP_PX = 16;
const MIN_TICK_PX = 90;

export interface AxisTicks {
  /** Major and minor tick times (clk). */
  major: number[];
  minor: number[];
  /** Label of each major tick. */
  labels: string[];
}

/** The smallest 1, 2 or 5 × 10^n of at least `min`. */
function roundStep(min: number): number {
  const base = Math.pow(10, Math.floor(Math.log10(min)));
  for (const m of [1, 2, 5, 10]) if (base * m >= min) return base * m;
  return base * 10;
}

/** Tick value `v` for steps of `step` (both in the unit shown); k, M and G only for whole multiples. */
export function tickLabel(v: number, step: number): string {
  const [div, suffix] = step >= 1e9 ? [1e9, 'G'] : step >= 1e6 ? [1e6, 'M'] : step >= 1e3 ? [1e3, 'k'] : [1, ''];
  const decimals = div > 1 || step >= 1 ? 0 : Math.ceil(-Math.log10(step) - 1e-9);
  return (v / div).toLocaleString('en-US', { minimumFractionDigits: decimals, maximumFractionDigits: decimals }) + suffix;
}

/**
 * Ticks for the view [start, start + duration) on `width` px, within [minTime, maxTime]; times in
 * clk, steps chosen in ns when `nsPerCycle` is given.
 */
export function axisTicks(start: number, duration: number, width: number, nsPerCycle: number | null, minTime: number, maxTime: number): AxisTicks {
  const scale = nsPerCycle ?? 1;
  const unit = nsPerCycle ? ' ns' : '';
  const s = start * scale;
  const d = duration * scale;
  const first = roundStep((d * MIN_TICK_PX) / Math.max(1, width));
  const longest = Math.max(tickLabel(s, first).length, tickLabel(s + d, first).length) + unit.length;
  const step = roundStep((d * Math.max(MIN_TICK_PX, longest * CHAR_PX + LABEL_GAP_PX)) / Math.max(1, width));
  const out: AxisTicks = { major: [], minor: [], labels: [] };
  const ticks = (every: number, each: (v: number, t: number) => void) => {
    // Multiples by index, so long axes do not accumulate rounding error.
    for (let i = Math.ceil(s / every), v = i * every; v <= s + d + every / 1000; i++, v = i * every) {
      const t = v / scale;
      if (t >= minTime && t <= maxTime) each(v, t);
    }
  };
  ticks(step, (v, t) => {
    out.major.push(t);
    out.labels.push(tickLabel(v, step) + unit);
  });
  ticks(step / 5, (_, t) => out.minor.push(t));
  return out;
}
