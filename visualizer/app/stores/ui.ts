import { defineStore } from 'pinia';
import { useSessionStore } from '~/stores/session';
import { computeDefaultExpanded } from '~/utils/traceTree';

export type BusType = 'command' | 'data' | 'request' | 'tp_data' | 'tp_cmd' | 'tp_node' | 'bank' | 'bk_bus' | 'bk_faw' | 'bk_hdr' | 'cmd_sum';
export type ViewMode = 'command' | 'request' | 'throughput' | 'bank';

/** Header tab order. */
export const VIEW_ORDER: ViewMode[] = ['command', 'request', 'throughput', 'bank'];
export const VIEW_LABELS: Record<ViewMode, string> = {
  command: 'Commands',
  request: 'Requests',
  throughput: 'Throughput',
  bank: 'Bank state',
};

const TIMELINE_NS_STORAGE_KEY = 'ramulator2-visualizer-timeline-ns';
const LAYOUT_STORAGE_KEY = 'ramviz-dock-layout';
const DEFAULT_STACK: ViewMode[] = ['bank', 'command'];
const DEFAULT_WEIGHTS: Record<ViewMode, number> = { command: 1, request: 1, throughput: 0.6, bank: 1 };

export interface RowLayout {
  top: number;
  height: number;
  /**
   * Path through the hierarchy, e.g. [0, 1, 2] = level0=0, level1=1, level2=2.
   * Bank view: [lane] for 'bank' rows, domain coords (levels below Channel) for 'bk_*' rows.
   */
  path: number[];
  /** How deep in the hierarchy this row sits (= path.length) */
  depth: number;
  /** Which bus section this row belongs to */
  busType: BusType;
}

/** Sidebar-tree state of one panel: its rows drive that panel's lanes. */
export interface PanelUiState {
  rowLayout: RowLayout[];
  /** Expanded tree node ids; empty means "use the view's defaults". */
  expandedState: string[];
  layoutVersion: number;
  /** After navigation, scroll the panel's tree so this row is centered. */
  treeScrollTarget: null | { busType: BusType; path: number[] };
}

function newPanel(): PanelUiState {
  return { rowLayout: [], expandedState: [], layoutVersion: 0, treeScrollTarget: null };
}

function isView(v: unknown): v is ViewMode {
  return typeof v === 'string' && (VIEW_ORDER as string[]).includes(v);
}

export const useUIStore = defineStore('ui', {
  state: () => ({
    panels: {
      command: newPanel(),
      request: newPanel(),
      throughput: newPanel(),
      bank: newPanel(),
    } as Record<ViewMode, PanelUiState>,
    /** Docked panels, top to bottom; every other view is parked as a header tab. */
    stack: [...DEFAULT_STACK] as ViewMode[],
    /** Flex weights of docked panels (relative heights). */
    weights: { ...DEFAULT_WEIGHTS } as Record<ViewMode, number>,
    /** Panel the inspector follows: last one pointed at or shown. */
    activeView: DEFAULT_STACK[0] as ViewMode,
    /** Session generation the panels' tree expansions belong to. */
    treeGeneration: -1,
    displayTimelineInNs: false,
  }),
  getters: {
    parked: (state): ViewMode[] => VIEW_ORDER.filter(v => !state.stack.includes(v)),
  },
  actions: {
    hydrateSettings() {
      if (!import.meta.client) return;
      try {
        this.displayTimelineInNs = localStorage.getItem(TIMELINE_NS_STORAGE_KEY) === 'true';
        const saved = JSON.parse(localStorage.getItem(LAYOUT_STORAGE_KEY) ?? 'null');
        if (saved && Array.isArray(saved.stack)) {
          const stack = saved.stack.filter(isView).filter((v: ViewMode, i: number, a: ViewMode[]) => a.indexOf(v) === i);
          if (stack.length > 0) this.stack = stack;
          for (const v of VIEW_ORDER) {
            const w = saved.weights?.[v];
            if (typeof w === 'number' && w > 0) this.weights[v] = w;
          }
          this.activeView = this.stack[0]!;
        }
      } catch {
        /* ignore */
      }
    },
    persistLayout() {
      if (!import.meta.client) return;
      try {
        localStorage.setItem(LAYOUT_STORAGE_KEY, JSON.stringify({ stack: this.stack, weights: this.weights }));
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

    // ── Per-panel tree state ──────────────────────────────────────
    setRowLayout(view: ViewMode, layout: RowLayout[]) {
      const p = this.panels[view];
      p.rowLayout = layout;
      p.layoutVersion++;
    },
    setExpandedState(view: ViewMode, ids: string[]) {
      const p = this.panels[view];
      p.expandedState = ids;
      p.layoutVersion++;
    },
    /** Union `ids` into the view's expanded set (or its defaults if empty). */
    mergeExpandedForNavigation(view: ViewMode, ids: string[]) {
      const spec = useSessionStore().spec;
      if (!spec) return;
      const p = this.panels[view];
      const previous = p.expandedState.length > 0 ? p.expandedState : computeDefaultExpanded(view, spec);
      p.expandedState = [...new Set([...previous, ...ids])];
      p.layoutVersion++;
    },
    setTreeScrollTarget(view: ViewMode, target: PanelUiState['treeScrollTarget']) {
      this.panels[view].treeScrollTarget = target;
    },
    /** A new trace starts from each view's default expansion. */
    resetTreesFor(generation: number) {
      if (this.treeGeneration === generation) return;
      this.treeGeneration = generation;
      for (const v of VIEW_ORDER) {
        const p = this.panels[v];
        p.expandedState = [];
        p.treeScrollTarget = null;
        p.layoutVersion++;
      }
    },

    // ── Dock ──────────────────────────────────────────────────────
    setActiveView(view: ViewMode) {
      this.activeView = view;
    },
    /** Move or insert `view` into the stack before position `index` (positions of the current stack). */
    dock(view: ViewMode, index: number) {
      const from = this.stack.indexOf(view);
      const stack = this.stack.filter(v => v !== view);
      const at = from >= 0 && from < index ? index - 1 : index;
      stack.splice(Math.max(0, Math.min(at, stack.length)), 0, view);
      this.stack = stack;
      this.activeView = view;
      this.persistLayout();
    },
    /** Park a docked panel as a header tab; the last panel stays docked. */
    park(view: ViewMode) {
      if (this.stack.length <= 1 || !this.stack.includes(view)) return;
      this.stack = this.stack.filter(v => v !== view);
      if (this.activeView === view) this.activeView = this.stack[0]!;
      this.persistLayout();
    },
    /** Header tab click: a parked view takes the place of the active panel. */
    showView(view: ViewMode) {
      if (!this.stack.includes(view)) {
        const at = Math.max(0, this.stack.indexOf(this.activeView));
        const stack = [...this.stack];
        stack.splice(at, 1, view);
        this.stack = stack;
        this.persistLayout();
      }
      this.activeView = view;
    },
    /** Navigation target: dock `view` below `after` if it is parked. */
    reveal(view: ViewMode, after?: ViewMode) {
      if (!this.stack.includes(view)) {
        const at = after ? this.stack.indexOf(after) + 1 : this.stack.length;
        const stack = [...this.stack];
        stack.splice(at > 0 ? at : stack.length, 0, view);
        this.stack = stack;
        this.persistLayout();
      }
      this.activeView = view;
    },
    /** Splitter drag between two docked panels. */
    setWeights(a: ViewMode, wa: number, b: ViewMode, wb: number) {
      this.weights[a] = wa;
      this.weights[b] = wb;
    },
    resetLayout() {
      this.stack = [...DEFAULT_STACK];
      this.weights = { ...DEFAULT_WEIGHTS };
      this.activeView = DEFAULT_STACK[0]!;
      this.persistLayout();
    },
  },
});
