// Lanes of the Commands, Requests and Throughput views: one per structural address (down to the
// level above Row). Level 0 is skipped when it is Channel: it holds the global channel id while a
// trace covers one channel.
import type { TraceSpec } from '../composables/useTrace';
import type { BankPairRule } from '../utils/bankState';

export interface LaneInfo {
  /** First level contributing to the lane index. */
  from: number;
  /** Levels [from, depth) form the lane index. */
  depth: number;
  /** Mixed-radix strides per level (index < depth). */
  strides: number[];
  totalLanes: number;
}

export function structuralDepthOf(spec: TraceSpec): number {
  const rowIdx = spec.levelNames.indexOf('Row');
  return rowIdx >= 0 ? rowIdx : Math.max(spec.levelNames.length - 2, 1);
}

export function laneFrom(spec: TraceSpec): number {
  return spec.levelNames[0] === 'Channel' ? 1 : 0;
}

export function laneInfo(spec: TraceSpec): LaneInfo {
  const depth = structuralDepthOf(spec);
  const from = laneFrom(spec);
  if (depth === 0) return { from, depth, strides: [], totalLanes: 1 };
  const strides = new Array<number>(depth);
  strides[depth - 1] = 1;
  for (let k = depth - 2; k >= 0; k--) strides[k] = strides[k + 1]! * spec.levelSizes[k + 1]!;
  return { from, depth, strides, totalLanes: spec.levelSizes[0]! * strides[0]! };
}

/**
 * Lane index past the last bank lane: commands on it appear only in the CMD BUS summary row.
 * Equal to totalLanes, so lane lookups hold totalLanes + 1 entries.
 */
export function hiddenLane(totalLanes: number): number {
  return totalLanes;
}

/** Lane of entry i (wildcard levels count as 0). */
export function laneOf(li: LaneInfo, addr: Int32Array[], i: number): number {
  let lane = 0;
  for (let k = li.from; k < li.depth; k++) {
    const a = addr[k]![i]!;
    if (a >= 0) lane += a * li.strides[k]!;
  }
  return lane;
}

/**
 * Lanes a command is drawn on. A -1 address level is a wildcard (all-bank / same-bank commands);
 * with a pair rule, a paired command also targets its partner bank. Wildcard commands that act on
 * no bank (no open / close / access / refresh) go to the hidden lane. The first lane is the
 * command's own; the returned array is reused between calls.
 */
export function commandLanes(
  li: LaneInfo,
  spec: TraceSpec,
  pairs: (BankPairRule | null)[],
): (addr: Int32Array[], cmd: number, i: number) => number[] {
  const out: number[] = [];
  const fanCache = new Map<number, number[]>();
  return (addr, cmd, i) => {
    out.length = 0;
    let lane = 0;
    let wild = false;
    for (let k = li.from; k < li.depth; k++) {
      const a = addr[k]![i]!;
      if (a < 0) wild = true;
      else lane += a * li.strides[k]!;
    }
    const pair = pairs[cmd];
    if (!wild && !pair) {
      out.push(lane);
      return out;
    }
    const meta = spec.commandMeta[cmd];
    if (wild && !(meta?.isOpening || meta?.isClosing || meta?.isAccessing || meta?.isRefreshing)) {
      out.push(hiddenLane(li.totalLanes));
      return out;
    }
    if (!wild && pair) {
      out.push(lane);
      if (pair.level >= li.from && pair.level < li.depth) {
        const a = addr[pair.level]![i]!;
        if ((a ^ pair.mask) < spec.levelSizes[pair.level]!) out.push(lane + ((a ^ pair.mask) - a) * li.strides[pair.level]!);
      }
      return out;
    }
    let key = 0;
    for (let k = li.from; k < li.depth; k++) {
      const a = addr[k]![i]!;
      key = key * (spec.levelSizes[k]! + 1) + (a < 0 ? spec.levelSizes[k]! : a);
    }
    let lanes = fanCache.get(key);
    if (!lanes) {
      lanes = [0];
      for (let k = li.from; k < li.depth; k++) {
        const a = addr[k]![i]!;
        const st = li.strides[k]!;
        if (a >= 0) lanes = lanes.map(l => l + a * st);
        else {
          const next: number[] = [];
          for (const l of lanes) for (let v = 0; v < spec.levelSizes[k]!; v++) next.push(l + v * st);
          lanes = next;
        }
      }
      fanCache.set(key, lanes);
    }
    for (const l of lanes) out.push(l);
    return out;
  };
}
