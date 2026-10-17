// State shared by every docked panel of one trace page: the time axis (viewport, range,
// cursor), the derived-data model, hover/statistics read by the inspector, and navigation
// between panels. The page creates it (createTimeline) and panels inject it (useTimeline).
import type { ComputedRef, InjectionKey, Ref, ShallowRef } from 'vue';
import type { BusType, ViewMode } from '~/stores/ui';
import type { BankStateData } from '~/utils/bankState';
import { bankBuildState } from '~/utils/bankState';
import {
  bisectLeft,
  bisectRight,
  buildTraceDerived,
  commandDuration,
  maxCommandDuration,
  type TraceDerived,
  type TraceDerivedInput,
} from '~/utils/tracePipeline';
import {
  nodeId,
  pathFromAddrForTree,
  rankLevelIndex,
  structuralDepth as specStructuralDepth,
} from '~/utils/traceTree';
import { defaultCmdColor } from '~/utils/commandColors';

import StreamingWorker from '~/workers/streaming.worker.ts?worker';

// ── Types ───────────────────────────────────────────────────────────

export interface ViewState {
  start: number;
  duration: number;
  minDuration: number;
  maxDuration: number;
  minTime: number;
  maxTime: number;
}

export interface HitResult {
  index: number;
  cmdId: number;
  clk: bigint;
  typeId: number;
  sourceId: number;
  addr: number[];
  bus: BusType;
  duration: number;
  busClk?: number;
  queueDelay?: number;
  /** Index into request-view `reqData` (for GPU hover pass). */
  requestGroupIndex?: number;
}

export interface RangeSelection {
  startTime: number;
  endTime: number;
}

export interface RangeStats {
  durationCycles: number;
  totalCommands: number;
  readCommands: number;
  writeCommands: number;
  otherCommands: number;
  accessingCommands: number;
  /** Commands per cycle on the command bus. */
  cmdBusThroughput: number;
}

export interface ThroughputHover {
  /** Channel data / command bus chart, or an organization node's data-bus share. */
  series: 'data' | 'cmd' | 'node';
  /** Node rows: tree path and its name ("PseudoChannel 0 · Sid 0 · …"). */
  path?: number[];
  label?: string;
  binIndex: number;
  value: number;
  binStartTime: number;
  binEndTime: number;
}

export interface ThroughputRangeStats {
  dataMeanUtil: number;
  cmdMeanUtil: number;
  binCount: number;
}

export type CursorJumpDirection = 'prev' | 'next';

export interface BankShares {
  starved: number;
  bus: number;
  bg: number;
  act: number;
  pre: number;
  idle: number;
  wrrec: number;
  ref: number;
}

/** Bank-view statistics for the viewport (or the selected range), over the visible banks. */
export interface BankStatsSummary {
  scope: 'view' | 'range';
  t0: number;
  t1: number;
  banks: number;
  /** Mean over the visible data-bus / tFAW domains; null if the standard has none. */
  busPct: number | null;
  fawPct: number | null;
  shares: BankShares;
  hasBg: boolean;
  hasWrrec: boolean;
  hasRef: boolean;
}

export interface BankHover {
  row: 'bank' | 'bus' | 'faw';
  title: string;
  path: string;
  state: string;
  color: string;
  start: number;
  end: number;
  sub?: string;
  subColor?: string;
  subStart?: number;
  subEnd?: number;
  /** Trace index of the command behind the span, -1 if none (click jumps to it). */
  cause: number;
  causeName?: string;
  causeClk?: number;
}

/** Derived arrays for one (session, dataVersion). */
export interface TraceModel extends TraceDerived {
  sessionGeneration: number;
  dataVersion: number;
}

/** A request and its commands, selected together. req = -1: a command without a request (refresh, …). */
export interface LinkedSelection {
  req: number;
  /** Trace indices, in trace order. */
  cmds: number[];
  /** The command clicked or navigated to (shown by the inspector). */
  focus: number;
}

/** Where a linked bar sits on screen (client px), for the leader lines between panels. */
export interface LinkAnchor {
  x: number;
  top: number;
  bottom: number;
  /** Visible extent of the panel's canvas: leaders end at its edge when the bar is scrolled away. */
  clipTop: number;
  clipBottom: number;
}

/** Published by the Commands / Requests renderers; read by the dock's leader-line overlay. */
export interface LinkGeometry {
  cmd?: (origIdx: number) => LinkAnchor | null;
  req?: (j: number) => LinkAnchor | null;
}

/** Hooks a panel exposes to the dev-only automation handle. */
export interface PanelDebug {
  refreshBankStats?: () => BankStatsSummary | null;
  headerTitles?: () => Record<string, string>;
}

export interface Timeline {
  viewState: ViewState;
  rangeSelection: Ref<RangeSelection | null>;
  cursorMode: Ref<boolean>;
  cursorTime: Ref<number | null>;
  followLive: Ref<boolean>;
  model: ShallowRef<TraceModel | null>;
  bankState: ShallowRef<BankStateData | null>;
  viewStats: ShallowRef<RangeStats | null>;
  viewThroughputStats: ShallowRef<ThroughputRangeStats | null>;
  rangeStats: ShallowRef<RangeStats | null>;
  throughputRangeStats: ShallowRef<ThroughputRangeStats | null>;
  hoveredCommand: ShallowRef<HitResult | null>;
  hoveredRequest: ShallowRef<HitResult | null>;
  throughputHover: ShallowRef<ThroughputHover | null>;
  bankHover: ShallowRef<BankHover | null>;
  bankStats: ShallowRef<BankStatsSummary | null>;
  /** The selected request + commands, outlined in every panel and joined by leader lines. */
  selection: ShallowRef<LinkedSelection | null>;
  /** Group of the hovered command or request, previewed in the other panel. */
  linkPreview: ComputedRef<LinkedSelection | null>;
  linkGeometry: LinkGeometry;
  panelDebug: Partial<Record<ViewMode, PanelDebug>>;
  /** Step the viewport animation to this animation frame; every frame loop calls it before drawing. */
  advance: () => void;
  clearSelection: () => void;
  hitForCommand: (origIdx: number) => HitResult | null;
  hitForRequest: (j: number) => HitResult | null;
  canJumpCursor: ComputedRef<boolean>;
  rawAddr: () => Int32Array[];
  ensureBankState: () => Promise<void>;
  placeCursor: (time: number, view: ViewMode) => void;
  jumpCursor: (direction: CursorJumpDirection) => boolean;
  clearCursor: () => void;
  setCursorMode: (enabled: boolean) => void;
  focusCommand: (origIdx: number, from?: ViewMode) => void;
  navigateToCounterpart: (hit: HitResult, from?: ViewMode) => void;
}

// ── Constants ───────────────────────────────────────────────────────

/** Viewport glide when framing a selection (ms). */
const VIEW_GLIDE_MS = 320;
/** Narrowest window a selection is framed in (cycles). */
const LINK_MIN_WINDOW = 96;
const STATS_THROTTLE_MS = 100;
/** Initial window of a newly loaded trace (cycles). */
const INITIAL_WINDOW = 500;
const BANK_STATUS = 'Reconstructing bank states…';

const TIMELINE_KEY: InjectionKey<Timeline> = Symbol('ramviz-timeline');

/**
 * Animation clock for UI transitions: returns elapsed ms, advancing at most 50 ms per frame, so a
 * stall (a panel mounting, shaders compiling) pauses the motion instead of skipping it. Calls with
 * the same frame timestamp return the same value.
 */
export function steadyClock(): (now: number) => number {
  let last = -1;
  let elapsed = 0;
  return (now) => {
    if (last >= 0) elapsed += Math.min(Math.max(0, now - last), 50);
    last = now;
    return elapsed;
  };
}

/** Last model, kept across page remounts (e.g. back from the setup page). */
let modelCache: TraceModel | null = null;

// ── Pure window statistics ──────────────────────────────────────────

function commandWindow(m: TraceModel, t0: number, t1: number): RangeStats | null {
  const duration = t1 - t0;
  if (m.N === 0 || duration <= 0) return null;
  const lo = bisectLeft(m.cmdRelClk, t0 - m.referenceTime, m.N);
  const hi = bisectRight(m.cmdRelClk, t1 - m.referenceTime, m.N);
  const readCommands = m.prefixRead[hi]! - m.prefixRead[lo]!;
  const writeCommands = m.prefixWrite[hi]! - m.prefixWrite[lo]!;
  const accessingCommands = readCommands + writeCommands;
  const totalCommands = hi - lo;
  return {
    durationCycles: duration,
    totalCommands,
    readCommands,
    writeCommands,
    otherCommands: totalCommands - accessingCommands,
    accessingCommands,
    cmdBusThroughput: totalCommands / duration,
  };
}

/** Mean throughput-bin utilisation over [t0, t1) (absolute clocks), or null. */
function throughputWindow(m: TraceModel, t0: number, t1: number): ThroughputRangeStats | null {
  if (m.tpBinCount === 0 || m.tpBinSize <= 0) return null;
  const relStart = t0 - m.referenceTime;
  const relEnd = t1 - m.referenceTime;
  if (relEnd <= relStart) return null;
  const startBin = Math.max(0, Math.floor(relStart / m.tpBinSize));
  const endBin = Math.min(m.tpBinCount - 1, Math.floor(Math.max(relStart, relEnd - 1e-9) / m.tpBinSize));
  if (endBin < startBin) return null;
  let sumData = 0;
  let sumCmd = 0;
  for (let b = startBin; b <= endBin; b++) {
    sumData += m.tpDataUtil[b]!;
    sumCmd += m.tpCmdUtil[b]!;
  }
  const binCount = endBin - startBin + 1;
  return { dataMeanUtil: sumData / binCount, cmdMeanUtil: sumCmd / binCount, binCount };
}

// ── Context ─────────────────────────────────────────────────────────

export function useTimeline(): Timeline {
  const tl = inject(TIMELINE_KEY, null);
  if (!tl) throw new Error('useTimeline() needs a page that called createTimeline()');
  return tl;
}

export function createTimeline(followLive: Ref<boolean>): Timeline {
  const sessionStore = useSessionStore();
  const uiStore = useUIStore();
  const traceLoadStatus = useTraceLoadStatus();

  const viewState: ViewState = reactive({
    start: 0,
    duration: 100,
    minDuration: 10,
    maxDuration: 10_000,
    minTime: 0,
    maxTime: 1000,
  });
  const rangeSelection = ref<RangeSelection | null>(null);
  const cursorMode = ref(false);
  const cursorTime = ref<number | null>(null);
  /** Panel the cursor was placed in; Prev / Next step through that view's events. */
  const cursorView = ref<ViewMode | null>(null);

  const model = shallowRef<TraceModel | null>(null);
  const bankState = shallowRef<BankStateData | null>(null);
  const viewStats = shallowRef<RangeStats | null>(null);
  const viewThroughputStats = shallowRef<ThroughputRangeStats | null>(null);
  const rangeStats = shallowRef<RangeStats | null>(null);
  const throughputRangeStats = shallowRef<ThroughputRangeStats | null>(null);
  const hoveredCommand = shallowRef<HitResult | null>(null);
  const hoveredRequest = shallowRef<HitResult | null>(null);
  const throughputHover = shallowRef<ThroughputHover | null>(null);
  const bankHover = shallowRef<BankHover | null>(null);
  const bankStats = shallowRef<BankStatsSummary | null>(null);
  const selection = shallowRef<LinkedSelection | null>(null);
  const linkGeometry: LinkGeometry = {};
  const panelDebug: Timeline['panelDebug'] = {};
  const reduceMotion = import.meta.client && window.matchMedia('(prefers-reduced-motion: reduce)').matches;

  let seenGeneration = -1;
  /** The viewport has been fitted to the current session's first data. */
  let fitted = false;
  let busy = false;
  let pending = false;
  let worker: Worker | null = null;
  let bankVersion = -1;
  /** Set once a Bank state panel has been shown, so live refreshes keep the reconstruction current. */
  let bankWanted = false;
  let bankBuilding = false;
  let bankWorkerUpdates = 0;
  let addrVersion = -1;
  let addrCache: Int32Array[] = [];

  async function bumpLoadStatus(msg: string) {
    traceLoadStatus.value = msg;
    await nextTick();
    await new Promise<void>(r => requestAnimationFrame(() => r()));
  }

  /** Raw (non-reactive) address columns, cached per dataVersion. */
  function rawAddr(): Int32Array[] {
    if (addrVersion !== sessionStore.dataVersion) {
      const a = sessionStore.arrays;
      addrCache = a ? toRaw(toRaw(a).addr).map(x => toRaw(x)) : [];
      addrVersion = sessionStore.dataVersion;
    }
    return addrCache;
  }

  function derivedInput(wantBankState: boolean): TraceDerivedInput | null {
    const { arrays, header, spec } = sessionStore;
    if (!arrays || !header || !spec) return null;
    const a = toRaw(arrays);
    const s = toRaw(spec);
    return {
      spec: {
        levelNames: toRaw(s.levelNames),
        levelSizes: toRaw(s.levelSizes),
        commandNames: toRaw(s.commandNames),
        commandMeta: toRaw(s.commandMeta).map(m => toRaw(m)),
        commandCycles: toRaw(s.commandCycles),
        timingNames: toRaw(s.timingNames),
        timingValues: toRaw(s.timingValues),
      },
      arrays: {
        clk: a.clk,
        arrive: a.arrive,
        cmdId: a.cmdId,
        typeId: a.typeId,
        sourceId: a.sourceId,
        addr: rawAddr(),
      },
      N: header.numEntries,
      readLatency: header.readLatency,
      dramType: header.dramType,
      wantBankState,
    };
  }

  function computeInWorker(input: TraceDerivedInput): Promise<TraceDerived> {
    worker ??= new StreamingWorker();
    const w = worker;
    return new Promise((resolve, reject) => {
      w.onmessage = (e: MessageEvent<TraceDerived | { error: string }>) => {
        if ('error' in e.data) reject(new Error(`streaming worker: ${e.data.error}`));
        else resolve(e.data);
      };
      w.onerror = e => reject(new Error(`streaming worker: ${e.message}`));
      w.onmessageerror = () => reject(new Error('streaming worker: result could not be deserialized'));
      w.postMessage(input);
    });
  }

  /** Viewport bounds for new data: fit once per session, then follow the tail or keep the window. */
  function applyBounds(m: TraceModel) {
    if (m.N === 0) {
      Object.assign(viewState, { minTime: m.referenceTime, maxTime: m.referenceTime + 1000, start: m.referenceTime, duration: 100, maxDuration: 1000 });
      return;
    }
    const lastCmd = m.cmdRelClk[m.N - 1]!;
    const lastData = m.dataBus.count > 0 ? m.dataBus.relClk[m.dataBus.count - 1]! : 0;
    const span = Math.max(lastCmd + maxCommandDuration(sessionStore.spec), lastData + m.nBL) + 1;
    const savedStart = viewState.start;
    viewState.minTime = m.referenceTime;
    viewState.maxTime = m.referenceTime + span;
    viewState.maxDuration = span;
    if (!fitted) {
      viewState.start = m.referenceTime;
      viewState.duration = Math.min(span, INITIAL_WINDOW);
      fitted = true;
      return;
    }
    const dur = Math.max(viewState.minDuration, Math.min(viewState.duration, span));
    viewState.duration = dur;
    const start = followLive.value ? viewState.maxTime - dur : savedStart;
    viewState.start = Math.max(viewState.minTime, Math.min(start, viewState.maxTime - dur));
  }

  /** Recompute the model for the session store's current data (serialized; coalesces bursts). */
  async function refresh() {
    if (busy) {
      pending = true;
      return;
    }
    busy = true;
    let ownsStatus = false;
    try {
      const { arrays, header, spec } = sessionStore;
      if (!arrays || !header || !spec) return;
      const gen = sessionStore.sessionGeneration;
      const ver = sessionStore.dataVersion;
      if (gen !== seenGeneration) {
        seenGeneration = gen;
        uiStore.resetTreesFor(gen);
        fitted = false;
        clearCursor();
        rangeSelection.value = null;
        bankState.value = null;
        bankVersion = -1;
        selection.value = null;
        viewAnim = null;
      }
      const cur = model.value;
      if (cur && cur.sessionGeneration === gen && cur.dataVersion === ver) return;

      let m: TraceModel;
      if (modelCache && modelCache.sessionGeneration === gen && modelCache.dataVersion === ver) {
        m = modelCache;
      } else {
        const input = derivedInput(bankWanted || uiStore.stack.includes('bank'));
        if (!input) return;
        // Live updates of a session already on screen go to the worker; the first load runs here with progress.
        const streaming = cur !== null && cur.sessionGeneration === gen && cur.N > 0;
        let d: TraceDerived;
        if (streaming) {
          d = await computeInWorker(input);
          if (sessionStore.sessionGeneration !== gen) {
            pending = true;
            return;
          }
          d.cmdId = input.arrays.cmdId.subarray(0, d.N);
          if (d.bankState) bankWorkerUpdates++;
        } else {
          ownsStatus = true;
          d = await buildTraceDerived(input, bumpLoadStatus);
          if (sessionStore.sessionGeneration !== gen) {
            pending = true; // a new session arrived meanwhile; rebuild for it
            return;
          }
          const defaults: Record<number, string> = {};
          for (let id = 0; id < spec.commandNames.length; id++) {
            if (sessionStore.commandColors[id] === undefined)
              defaults[id] = defaultCmdColor(spec.commandNames[id]!, spec.commandMeta[id]!);
          }
          if (Object.keys(defaults).length > 0) sessionStore.mergeDefaultCommandColors(defaults);
        }
        m = { ...d, sessionGeneration: gen, dataVersion: ver };
        modelCache = m;
      }

      if (m.bankState) {
        bankState.value = m.bankState;
        bankVersion = m.dataVersion;
      }
      sessionStore.setRequestLaneStackDepth(Array.from(m.reqData.maxStackPerLane));
      applyBounds(m);
      model.value = m;
      recomputeRangeStats();
      scheduleViewStats();
      if (bankWanted && bankVersion !== m.dataVersion) void ensureBankState();
    } catch (e) {
      // Keep the last model on screen; the next data update retries.
      console.error('Trace refresh failed:', e);
    } finally {
      busy = false;
      if (ownsStatus) traceLoadStatus.value = null;
      if (pending) {
        pending = false;
        void refresh();
      }
    }
  }

  /** Build the bank reconstruction for the current model if it is missing or stale. */
  async function ensureBankState() {
    bankWanted = true;
    const m = model.value;
    const { header, spec } = sessionStore;
    const input = derivedInput(true);
    if (!m || !header || !spec || !input) return;
    if (bankState.value && bankVersion === m.dataVersion) return;
    if (bankBuilding) return;
    bankBuilding = true;
    try {
      if (m.N > 200_000) await bumpLoadStatus(BANK_STATUS);
      const bs = bankBuildState(input.spec, input.arrays, m.N, header.dramType);
      if (bankVersion < m.dataVersion) {
        bankVersion = m.dataVersion;
        bankState.value = bs;
        if (modelCache === m) m.bankState = bs;
      }
    } finally {
      bankBuilding = false;
      if (traceLoadStatus.value === BANK_STATUS) traceLoadStatus.value = null;
    }
  }

  // ── Window statistics (shared by every panel's inspector view) ────

  let statsTimer: ReturnType<typeof setTimeout> | null = null;

  function recomputeViewStats() {
    if (statsTimer) {
      clearTimeout(statsTimer);
      statsTimer = null;
    }
    const m = model.value;
    const t0 = viewState.start;
    const t1 = viewState.start + viewState.duration;
    viewStats.value = m ? commandWindow(m, t0, t1) : null;
    viewThroughputStats.value = m ? throughputWindow(m, t0, t1) : null;
  }

  function scheduleViewStats() {
    if (!statsTimer) statsTimer = setTimeout(recomputeViewStats, STATS_THROTTLE_MS);
  }

  function recomputeRangeStats() {
    const m = model.value;
    const sel = rangeSelection.value;
    const ok = m && sel && sel.endTime > sel.startTime;
    rangeStats.value = ok ? commandWindow(m, sel.startTime, sel.endTime) : null;
    throughputRangeStats.value = ok ? throughputWindow(m, sel.startTime, sel.endTime) : null;
  }

  watch(() => [viewState.start, viewState.duration] as const, scheduleViewStats);
  watch(rangeSelection, recomputeRangeStats);

  // ── Cursor ────────────────────────────────────────────────────────

  /** Event start times the cursor steps through in `view` (commands, or request arrivals). */
  function cursorStarts(view: ViewMode): { relClk: Float32Array; count: number } | null {
    const m = model.value;
    if (!m) return null;
    if ((view === 'command' || view === 'bank') && m.N > 0) return { relClk: m.cmdRelClk, count: m.N };
    if (view === 'request' && m.reqData.count > 0) return { relClk: m.reqData.relClk, count: m.reqData.count };
    return null;
  }

  const jumpView = computed(() => cursorView.value ?? uiStore.activeView);
  const canJumpCursor = computed(() => cursorTime.value != null && jumpView.value !== 'throughput');

  function placeCursor(time: number, view: ViewMode) {
    // Clocks are whole cycles; a fractional cursor would make Next / Previous look like no-ops.
    cursorTime.value = Math.round(Math.max(viewState.minTime, Math.min(viewState.maxTime, time)));
    cursorView.value = view;
  }

  function jumpCursor(direction: CursorJumpDirection): boolean {
    const m = model.value;
    const current = cursorTime.value;
    const starts = cursorStarts(jumpView.value);
    if (!m || current == null || !starts) return false;
    const rel = current - m.referenceTime;
    const idx = direction === 'next'
      ? bisectRight(starts.relClk, rel, starts.count)
      : bisectLeft(starts.relClk, rel, starts.count) - 1;
    if (idx < 0 || idx >= starts.count) return false;
    const t = Math.max(viewState.minTime, Math.min(viewState.maxTime, starts.relClk[idx]! + m.referenceTime));
    cursorTime.value = t;
    if (t < viewState.start || t > viewState.start + viewState.duration) {
      viewState.start = Math.max(viewState.minTime, Math.min(t - viewState.duration / 2, viewState.maxTime - viewState.duration));
    }
    return true;
  }

  function clearCursor() {
    cursorTime.value = null;
    cursorView.value = null;
  }

  function setCursorMode(enabled: boolean) {
    cursorMode.value = enabled;
  }

  // ── Viewport animation ───────────────────────────────────────────

  /** Glide in progress: centre moves linearly, width on a log scale, eased in and out. */
  let viewAnim: null | { c0: number; w0: number; c1: number; w1: number; clock: (now: number) => number } = null;
  /** The window the glide last set; any other change (wheel, drag, overview, live data) cancels it. */
  let viewSet = { start: 0, duration: 0 };

  function setView(start: number, duration: number) {
    const w = Math.max(viewState.minDuration, Math.min(duration, viewState.maxDuration));
    viewState.duration = w;
    viewState.start = Math.max(viewState.minTime, Math.min(start, viewState.maxTime - w));
    viewSet = { start: viewState.start, duration: viewState.duration };
  }

  function glideTo(start: number, duration: number) {
    if (reduceMotion) {
      viewAnim = null;
      setView(start, duration);
      return;
    }
    viewAnim = {
      c0: viewState.start + viewState.duration / 2,
      w0: viewState.duration,
      c1: start + duration / 2,
      w1: duration,
      clock: steadyClock(),
    };
    viewSet = { start: viewState.start, duration: viewState.duration };
  }

  function advance() {
    const a = viewAnim;
    if (!a) return;
    if (viewState.start !== viewSet.start || viewState.duration !== viewSet.duration) {
      viewAnim = null;
      return;
    }
    // The animation frame's own timestamp, identical for every panel drawing this frame.
    const now = (document.timeline?.currentTime as number | null) ?? performance.now();
    const t = Math.min(1, a.clock(now) / VIEW_GLIDE_MS);
    const e = t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2;
    const w = a.w0 * Math.pow(a.w1 / a.w0, e);
    setView(a.c0 + (a.c1 - a.c0) * e - w / 2, w);
    if (t >= 1) viewAnim = null;
  }

  // ── Linked selection and navigation ─────────────────────────────

  function groupOfRequest(m: TraceModel, j: number): LinkedSelection {
    const { cmdStart, cmds } = m.reqData;
    return { req: j, cmds: Array.from(cmds.subarray(cmdStart[j]!, cmdStart[j + 1]!)), focus: cmds[cmdStart[j]!]! };
  }

  function groupOfCommand(m: TraceModel, origIdx: number): LinkedSelection {
    const j = m.reqData.cmdReq[origIdx]!;
    return j >= 0 ? { ...groupOfRequest(m, j), focus: origIdx } : { req: -1, cmds: [origIdx], focus: origIdx };
  }

  const linkPreview = computed<LinkedSelection | null>(() => {
    const m = model.value;
    if (!m) return null;
    const hc = hoveredCommand.value;
    if (hc && hc.index < m.N) return groupOfCommand(m, hc.index);
    const hr = hoveredRequest.value;
    if (hr?.requestGroupIndex != null && hr.requestGroupIndex < m.reqData.count) return groupOfRequest(m, hr.requestGroupIndex);
    return null;
  });

  /** Glide to the request and all its commands, with margin. */
  function frameGroup(m: TraceModel, g: LinkedSelection) {
    const spec = sessionStore.spec;
    let a = Number.POSITIVE_INFINITY;
    let b = Number.NEGATIVE_INFINITY;
    if (g.req >= 0) {
      a = m.reqData.relClk[g.req]!;
      b = a + m.reqData.duration[g.req]!;
    }
    for (const i of g.cmds) {
      a = Math.min(a, m.cmdRelClk[i]!);
      b = Math.max(b, m.cmdRelClk[i]! + commandDuration(spec, m.cmdId[i]!));
    }
    const w = Math.max(b - a + 2 * ((b - a) * 0.18 + 12), LINK_MIN_WINDOW);
    glideTo(m.referenceTime + (a + b) / 2 - w / 2, w);
  }

  /** Select a group, show `target` (docked below `from` if parked) and bring the group into view in both panels. */
  function showLinked(g: LinkedSelection, target: 'command' | 'request', from?: ViewMode) {
    const m = model.value;
    const { arrays, spec } = sessionStore;
    if (!m || !arrays || !spec || g.cmds.length === 0) return;
    selection.value = g;
    uiStore.reveal(target, from);
    // A request's commands share its bank, so one address locates the group in both trees.
    const at = g.cmds[0]!;
    const sd = specStructuralDepth(spec);
    const rl = rankLevelIndex(spec);
    for (const [view, prefix] of [['command', 'cmd'], ['request', 'req']] as const) {
      if (!uiStore.stack.includes(view) || (view === 'request' && g.req < 0)) continue;
      const ids = ['_channel'];
      for (let k = rl; k < sd - 1; k++) ids.push(nodeId(prefix, pathFromAddrForTree(spec, arrays.addr, at, k)));
      uiStore.mergeExpandedForNavigation(view, ids);
      uiStore.setTreeScrollTarget(view, { busType: view, path: pathFromAddrForTree(spec, arrays.addr, at, sd - 1) });
    }
    frameGroup(m, g);
  }

  /** Select trace entry `origIdx` with its request and show it in the Commands panel. */
  function focusCommand(origIdx: number, from?: ViewMode) {
    const m = model.value;
    if (!m || origIdx < 0 || origIdx >= m.N) return;
    showLinked(groupOfCommand(m, origIdx), 'command', from);
  }

  /** Clicked bar: select its group and show the other side (command → Requests, request → Commands). */
  function navigateToCounterpart(hit: HitResult, from?: ViewMode) {
    const m = model.value;
    if (!m) return;
    if (hit.bus === 'request' && hit.requestGroupIndex != null) {
      showLinked(groupOfRequest(m, hit.requestGroupIndex), 'command', from);
      return;
    }
    if (hit.index < 0 || hit.index >= m.N) return;
    const g = groupOfCommand(m, hit.index);
    showLinked(g, g.req >= 0 ? 'request' : 'command', from);
  }

  function clearSelection() {
    selection.value = null;
  }

  /** Inspector record of a command, as a hover over its bar would produce. */
  function hitForCommand(i: number): HitResult | null {
    const { arrays, spec } = sessionStore;
    if (!arrays || i < 0 || i >= (model.value?.N ?? 0)) return null;
    return {
      index: i,
      cmdId: arrays.cmdId[i]!,
      clk: arrays.clk[i]!,
      typeId: arrays.typeId[i]!,
      sourceId: arrays.sourceId[i]!,
      addr: arrays.addr.map(a => a[i]!),
      bus: 'command',
      duration: commandDuration(spec, arrays.cmdId[i]!),
    };
  }

  /** Inspector record of request j, as a hover over its bar would produce. */
  function hitForRequest(j: number): HitResult | null {
    const m = model.value;
    const { arrays } = sessionStore;
    if (!m || !arrays || j < 0 || j >= m.reqData.count) return null;
    const i = m.reqData.representativeIdx[j]!;
    const qd = m.reqData.queueDelay[j]!;
    return {
      index: i,
      cmdId: m.reqData.typeId[j]!,
      clk: arrays.arrive[i]!,
      typeId: arrays.typeId[i]!,
      sourceId: arrays.sourceId[i]!,
      addr: arrays.addr.map(a => a[i]!),
      bus: 'request',
      duration: Math.max(0, m.reqData.duration[j]! - qd),
      queueDelay: qd,
      requestGroupIndex: j,
    };
  }

  // ── Lifecycle ─────────────────────────────────────────────────────

  onMounted(() => {
    void refresh();
  });
  watch(() => sessionStore.dataVersion, () => {
    void refresh();
  });

  // Enabling "follow" snaps to the tail without waiting for the next batch.
  watch(followLive, (follow) => {
    if (!follow || !model.value?.N) return;
    const dur = Math.max(viewState.minDuration, Math.min(viewState.duration, viewState.maxDuration));
    viewState.duration = dur;
    viewState.start = Math.max(viewState.minTime, viewState.maxTime - dur);
  });

  onUnmounted(() => {
    if (statsTimer) clearTimeout(statsTimer);
    worker?.terminate();
    worker = null;
    traceLoadStatus.value = null;
    if (import.meta.dev && import.meta.client) delete (window as any).__ramulatorViz;
  });

  const tl: Timeline = {
    viewState,
    rangeSelection,
    cursorMode,
    cursorTime,
    followLive,
    model,
    bankState,
    viewStats,
    viewThroughputStats,
    rangeStats,
    throughputRangeStats,
    hoveredCommand,
    hoveredRequest,
    throughputHover,
    bankHover,
    bankStats,
    selection,
    linkPreview,
    linkGeometry,
    panelDebug,
    advance,
    clearSelection,
    hitForCommand,
    hitForRequest,
    canJumpCursor,
    rawAddr,
    ensureBankState,
    placeCursor,
    jumpCursor,
    clearCursor,
    setCursorMode,
    focusCommand,
    navigateToCounterpart,
  };
  provide(TIMELINE_KEY, tl);

  if (import.meta.dev && import.meta.client) {
    // Hook for automated checks (scripts/verify-bank-state.ts counterpart in the browser).
    (window as any).__ramulatorViz = {
      viewState,
      bankStats,
      setView(start: number, duration: number) {
        viewState.start = start;
        viewState.duration = duration;
      },
      setViewNs(t0: number, t1: number) {
        const tck = sessionStore.getTimingValue('tCK_ps');
        const nsPerTick = tck ? tck / 1000 : 1;
        viewState.start = t0 / nsPerTick;
        viewState.duration = (t1 - t0) / nsPerTick;
      },
      refreshBankStats: () => panelDebug.bank?.refreshBankStats?.() ?? null,
      headerTitles: () => panelDebug.bank?.headerTitles?.() ?? {},
      bankInfo: () => ({ numEntries: bankState.value?.numEntries ?? 0, N: model.value?.N ?? 0, workerUpdates: bankWorkerUpdates }),
      layout: () => ({ stack: [...uiStore.stack], parked: [...uiStore.parked], active: uiStore.activeView, weights: { ...uiStore.weights } }),
      referenceTime: () => model.value?.referenceTime ?? 0,
      selection: () => selection.value,
      anchor: (kind: 'cmd' | 'req', idx: number) => linkGeometry[kind]?.(idx) ?? null,
    };
  }

  return tl;
}
