import type { TraceSpec } from '~/composables/useTrace';

/** Mirrors `stores/ui` (kept local to avoid circular imports with the Pinia store). */
export type TraceViewMode = 'command' | 'request' | 'throughput' | 'bank';
export type TraceBusType = 'command' | 'data' | 'request' | 'tp_data' | 'tp_cmd' | 'tp_node' | 'bank' | 'bk_bus' | 'bk_faw' | 'bk_hdr' | 'cmd_sum';

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

/** Tree node-id prefix per view; every view shares the organization hierarchy of the trace. */
export const TREE_PREFIX: Record<TraceViewMode, string> = { command: 'cmd', request: 'req', throughput: 'tp', bank: 'bk' };

/** Default expansion: every group node down to the bank level, so each bank has its own row. */
export function computeDefaultExpanded(viewMode: TraceViewMode, spec: TraceSpec): string[] {
  const prefix = TREE_PREFIX[viewMode];
  const sd = structuralDepth(spec);
  const ids: string[] = ['_channel'];
  const walk = (level: number, path: number[]) => {
    if (level >= sd - 1) return;
    for (let i = 0; i < (spec.levelSizes[level] ?? 0); i++) {
      const p = [...path, i];
      ids.push(nodeId(prefix, p));
      walk(level + 1, p);
    }
  };
  walk(rankLevelIndex(spec), channelBasePath(spec));
  return ids;
}
