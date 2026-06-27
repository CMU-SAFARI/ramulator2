import type { TraceSpec } from '~/composables/useTrace';

/** Mirrors `stores/ui` (kept local to avoid circular imports with the Pinia store). */
export type TraceViewMode = 'command' | 'request' | 'throughput';
export type TraceBusType = 'command' | 'data' | 'request' | 'tp_data' | 'tp_cmd';

export interface TraceRowHit {
  path: number[];
  top: number;
  height: number;
  busType: TraceBusType;
}

export function nodeId(prefix: string, path: number[]): string {
  return prefix + '.' + path.map((v, i) => `L${i}_${v}`).join('.');
}

export function channelBasePath(spec: TraceSpec): number[] {
  const channelLevel = spec.levelNames.indexOf('Channel');
  if (channelLevel < 0) return [];
  return Array.from({ length: channelLevel + 1 }, () => 0);
}

export function rankLevelIndex(spec: TraceSpec): number {
  const idx = spec.levelNames.indexOf('Rank');
  return idx >= 0 ? idx : 1;
}

/** Structural depth = index of `Row` in level names (matches sidebar / lane tiling). */
export function structuralDepth(spec: TraceSpec): number {
  const rowIdx = spec.levelNames.indexOf('Row');
  return rowIdx >= 0 ? rowIdx : Math.max(spec.levelNames.length - 2, 1);
}

export function bankGroupLevelIndex(spec: TraceSpec): number {
  const i = spec.levelNames.indexOf('BankGroup');
  if (i >= 0) return i;
  const r = spec.levelNames.indexOf('Rank');
  return r >= 0 ? r + 1 : 1;
}

/**
 * Tree path for `nodeId`, matching `trace.vue` walkBus: channel base (zeros) then addr indices
 * from `base.length` through `levelIdx` inclusive.
 */
export function pathFromAddrForTree(
  spec: TraceSpec,
  addr: Int32Array[],
  origIdx: number,
  levelIdx: number,
): number[] {
  const base = channelBasePath(spec);
  const path = [...base];
  for (let k = base.length; k <= levelIdx; k++) path.push(addr[k]![origIdx]!);
  return path;
}

/**
 * Keep Command Bus and Data Bus subtrees expanded in lockstep (see trace.vue).
 */
export function mirrorCmdDataExpansion(ids: string[], previous: string[]): string[] {
  const incoming = new Set(ids);
  const result = new Set(ids);

  for (const id of previous) {
    if (!incoming.has(id)) {
      if (id.startsWith('cmd.')) {
        result.delete(`data.${id.slice(4)}`);
      }
      else if (id.startsWith('data.')) {
        result.delete(`cmd.${id.slice(5)}`);
      }
    }
  }

  for (const id of [...result]) {
    if (id.startsWith('cmd.')) {
      const sib = `data.${id.slice(4)}`;
      if (!result.has(sib)) result.add(sib);
    }
    else if (id.startsWith('data.')) {
      const sib = `cmd.${id.slice(5)}`;
      if (!result.has(sib)) result.add(sib);
    }
  }

  return [...result];
}

export function computeDefaultExpanded(viewMode: TraceViewMode, spec: TraceSpec): string[] {
  const base = channelBasePath(spec);
  const rl = rankLevelIndex(spec);
  const rankCount = spec.levelSizes[rl] ?? 0;

  if (viewMode === 'throughput') {
    return ['_channel'];
  }

  if (viewMode === 'request') {
    const ids: string[] = ['_channel'];
    for (let i = 0; i < rankCount; i++) {
      ids.push(nodeId('req', [...base, i]));
    }
    return ids;
  }

  const ids: string[] = ['_channel', '_bus_cmd', '_bus_data'];
  for (let i = 0; i < rankCount; i++) {
    ids.push(nodeId('cmd', [...base, i]));
    ids.push(nodeId('data', [...base, i]));
  }

  return ids;
}

/** Deepest visible sidebar row whose path is a prefix of `targetPath` (same bus section). */
export function findDeepestRowPrefix(
  rows: TraceRowHit[],
  busType: TraceBusType,
  targetPath: number[],
): TraceRowHit | null {
  let best: TraceRowHit | null = null;
  for (const r of rows) {
    if (r.busType !== busType) continue;
    if (r.path.length > targetPath.length) continue;
    let ok = true;
    for (let i = 0; i < r.path.length; i++) {
      if (r.path[i] !== targetPath[i]) {
        ok = false;
        break;
      }
    }
    if (!ok) continue;
    if (!best || r.path.length > best.path.length) best = r;
  }
  return best;
}
