import { defineStore } from 'pinia';
import type { Raw } from 'vue';
import type { TraceHeader, TraceSpec } from '~/composables/useTrace';
import { TraceEngine } from '~/engine/client';
import type { EngineSource } from '~/engine/protocol';
import {
  readCommandColorOverrides,
  writeCommandColorOverride,
  readRequestTypeColors,
  writeRequestTypeColors,
  clearAllVisualizerColorStorage,
  type RequestTypeStoredColors,
} from '~/utils/commandColorStorage';
import { CMD_PALETTE, defaultCmdColor } from '~/utils/commandColors';

export interface CommandColors {
  [cmdId: number]: string;
}

/** A trace opened but not loaded yet: its header and spec (timings may still be edited). */
export interface PendingTrace {
  source: EngineSource;
  header: TraceHeader;
  spec: TraceSpec;
}

function defaultRequestTypeColors(): RequestTypeStoredColors {
  return {
    read: CMD_PALETTE.readAccess,
    write: CMD_PALETTE.writeAccess,
    maintenance: CMD_PALETTE.fallback,
  };
}

export const useSessionStore = defineStore('session', {
  state: () => ({
    header: null as TraceHeader | null,
    spec: null as TraceSpec | null,
    /** The loaded trace, held by the engine worker (not reactive). */
    engine: null as Raw<TraceEngine> | null,
    pendingTrace: null as PendingTrace | null,
    /** Display name of the loaded trace (file name, example title, or "Live session"). */
    traceName: '',
    commandColors: {} as CommandColors,
    /** Request view bar colors (Read / Write / Maintenance). */
    requestTypeColors: defaultRequestTypeColors(),
    /**
     * Per structural lane: number of stacked rows for overlapping requests (≥1).
     * Length = total lanes for structural depth; drives sidebar row min-height in request view.
     */
    requestLaneStackDepth: [] as number[],
    /** Increments on every load, so views reset their state. */
    sessionGeneration: 0,
  }),

  getters: {
    isLoaded:  (state): boolean => state.header !== null,
    numEntries: (state): number => state.header?.numEntries ?? 0,

    getCommandName: (state) => (cmdId: number): string =>
      state.spec?.commandNames[cmdId] ?? `cmd_${cmdId}`,

    getCommandColor: (state) => (cmdId: number): string =>
      state.commandColors[cmdId] ?? '#888888',

    getRequestTypeColor: (state) => (typeId: number): string => {
      if (typeId === 0) return state.requestTypeColors.read;
      if (typeId === 1) return state.requestTypeColors.write;
      return state.requestTypeColors.maintenance;
    },

    getLevelName: (state) => (level: number): string =>
      state.spec?.levelNames[level] ?? `level_${level}`,

    getTimingValue: (state) => (name: string): number | undefined => {
      if (!state.spec) return undefined;
      const idx = state.spec.timingNames.indexOf(name);
      return idx >= 0 ? state.spec.timingValues[idx] : undefined;
    },

    getRequestTypeName: () => (typeId: number): string => {
      if (typeId === 0) return 'Read';
      if (typeId === 1) return 'Write';
      return 'Maintenance';
    },
  },

  actions: {
    /** Hand the trace to a new engine (the previous one is closed). */
    loadTrace(trace: PendingTrace): TraceEngine {
      this.engine?.dispose();
      const engine = markRaw(new TraceEngine(trace.header, trace.spec, trace.source));
      this.header = trace.header;
      this.spec = trace.spec;
      this.engine = engine;
      this.sessionGeneration++;
      this.commandColors = {};
      this.requestTypeColors = { ...defaultRequestTypeColors() };
      this.requestLaneStackDepth = [];
      this.applyPersistedCommandColors();
      this.applyPersistedRequestTypeColors();
      const defaults: Record<number, string> = {};
      for (let id = 0; id < trace.spec.commandNames.length; id++) {
        if (this.commandColors[id] === undefined) defaults[id] = defaultCmdColor(trace.spec.commandNames[id]!, trace.spec.commandMeta[id]!);
      }
      this.mergeDefaultCommandColors(defaults);
      // Request rows per lane grow as requests are found.
      engine.onChange(() => {
        const rows = engine.maxRows;
        const cur = this.requestLaneStackDepth;
        if (cur.length !== rows.length || rows.some((v, i) => v !== cur[i])) this.requestLaneStackDepth = Array.from(rows);
      });
      return engine;
    },

    setPendingTrace(trace: PendingTrace, name = '') {
      this.pendingTrace = markRaw(trace);
      this.traceName = name;
    },

    clearPendingTrace() {
      this.pendingTrace = null;
    },

    loadPendingTrace() {
      if (!this.pendingTrace) return;
      const trace = this.pendingTrace;
      this.clearPendingTrace();
      this.loadTrace(trace);
    },

    setRequestLaneStackDepth(depths: number[]) {
      this.requestLaneStackDepth = depths;
    },

    /**
     * Set per-command color. Persists by command name to localStorage unless `persist` is false
     * (used when filling defaults in the renderer without overwriting stored overrides).
     */
    setCommandColor(cmdId: number, color: string, options?: { persist?: boolean }) {
      this.commandColors[cmdId] = color;
      if (options?.persist === false) return;
      const spec = this.spec;
      if (!spec) return;
      const name = spec.commandNames[cmdId];
      if (name == null) return;
      writeCommandColorOverride(name, color);
    },

    /** Merge colors from localStorage (keyed by command name) after loading a trace. */
    applyPersistedCommandColors() {
      const spec = this.spec;
      if (!spec) return;
      const stored = readCommandColorOverrides();
      for (let id = 0; id < spec.commandNames.length; id++) {
        const name = spec.commandNames[id]!;
        const c = stored[name];
        if (c) this.commandColors[id] = c;
      }
    },

    /** Batch default colors without persisting each slot (renderer init). */
    mergeDefaultCommandColors(defaults: Record<number, string>) {
      for (const [k, v] of Object.entries(defaults)) {
        const id = Number(k);
        if (this.commandColors[id] === undefined) this.commandColors[id] = v;
      }
    },

    applyPersistedRequestTypeColors() {
      const stored = readRequestTypeColors();
      if (stored.read) this.requestTypeColors.read = stored.read;
      if (stored.write) this.requestTypeColors.write = stored.write;
      if (stored.maintenance) this.requestTypeColors.maintenance = stored.maintenance;
    },

    setRequestTypeColor(
      key: keyof RequestTypeStoredColors,
      color: string,
      options?: { persist?: boolean },
    ) {
      this.requestTypeColors[key] = color;
      if (options?.persist === false) return;
      writeRequestTypeColors(this.requestTypeColors);
    },

    /** Clear persisted overrides and restore default command + request palette (for loaded trace). */
    resetColorPalette() {
      clearAllVisualizerColorStorage();
      this.requestTypeColors = { ...defaultRequestTypeColors() };
      this.commandColors = {};
      const spec = this.spec;
      if (!spec) return;
      const defaults: Record<number, string> = {};
      for (let id = 0; id < spec.commandNames.length; id++) {
        defaults[id] = defaultCmdColor(spec.commandNames[id]!, spec.commandMeta[id]!);
      }
      this.mergeDefaultCommandColors(defaults);
    },

    close() {
      this.engine?.dispose();
      this.engine = null;
      this.header = null;
      this.spec = null;
      this.pendingTrace = null;
      this.traceName = '';
      this.commandColors = {};
      this.requestTypeColors = { ...defaultRequestTypeColors() };
      this.requestLaneStackDepth = [];
      this.sessionGeneration = 0;
    },
  },
});
