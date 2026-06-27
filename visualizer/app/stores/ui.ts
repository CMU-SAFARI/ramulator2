import { defineStore } from 'pinia';
import { useSessionStore } from '~/stores/session';
import { computeDefaultExpanded, mirrorCmdDataExpansion } from '~/utils/traceTree';

export type BusType = 'command' | 'data' | 'request' | 'tp_data' | 'tp_cmd';
export type ViewMode = 'command' | 'request' | 'throughput';

const TIMELINE_NS_STORAGE_KEY = 'ramulator2-visualizer-timeline-ns';

export interface RowLayout {
  top: number;
  height: number;
  /** Path through the hierarchy, e.g. [0, 1, 2] = level0=0, level1=1, level2=2 */
  path: number[];
  /** How deep in the hierarchy this row sits (= path.length) */
  depth: number;
  /** Which bus section this row belongs to */
  busType: BusType;
}

export const useUIStore = defineStore('ui', {
  state: () => ({
    rowLayout: [] as RowLayout[],
    expandedState: [] as string[],
    savedExpanded: {} as Record<ViewMode, string[]>,
    layoutVersion: 0,
    viewMode: 'command' as ViewMode,
    displayTimelineInNs: false,
    /** After navigation, scroll the sidebar so this hierarchy row is centered (consumed in trace layout). */
    treeScrollTarget: null as null | { busType: BusType; path: number[] },
  }),
  actions: {
    hydrateSettings() {
      if (!import.meta.client) return;
      try {
        this.displayTimelineInNs = localStorage.getItem(TIMELINE_NS_STORAGE_KEY) === 'true';
      } catch {
        /* ignore */
      }
    },
    setDisplayTimelineInNs(value: boolean) {
      this.displayTimelineInNs = value;
      if (!import.meta.client) return;
      try {
        localStorage.setItem(TIMELINE_NS_STORAGE_KEY, String(value));
      } catch {
        /* ignore */
      }
    },
    setRowLayout(layout: RowLayout[]) {
      this.rowLayout = layout;
      this.layoutVersion++;
    },
    setExpandedState(state: string[]) {
      this.expandedState = state;
      this.savedExpanded[this.viewMode] = state;
      this.layoutVersion++;
    },
    setViewMode(mode: ViewMode) {
      this.savedExpanded[this.viewMode] = this.expandedState;
      this.viewMode = mode;
      this.expandedState = this.savedExpanded[mode] ?? [];
      this.layoutVersion++;
    },
    /** Union `ids` into the current expanded set (or defaults if empty), with cmd/data mirroring. */
    mergeExpandedForNavigation(ids: string[]) {
      const session = useSessionStore();
      const spec = session.spec;
      if (!spec) return;
      const defaultExp = computeDefaultExpanded(this.viewMode, spec);
      const previous = this.expandedState.length > 0 ? this.expandedState : defaultExp;
      const merged = [...new Set([...previous, ...ids])];
      const mirrored = mirrorCmdDataExpansion(merged, previous);
      this.expandedState = mirrored;
      this.savedExpanded[this.viewMode] = mirrored;
      this.layoutVersion++;
    },
    clearTreeScrollTarget() {
      this.treeScrollTarget = null;
    },
  },
});
