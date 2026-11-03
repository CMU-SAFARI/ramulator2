// State shared by every docked panel of one trace page: the time axis (viewport, range,
// cursor), the trace engine, hover/statistics read by the inspector, navigation between panels,
// and the frame scheduler every panel draws through. The page creates it (createTimeline) and
// panels inject it (useTimeline).
import type { ComputedRef, InjectionKey, Ref, ShallowRef } from 'vue';
import type { BusType, ViewMode } from '~/stores/ui';
import type { TraceEngine } from '~/engine/client';
import { busCycles, commandWindow, lowerBound, recordStart } from '~/engine/stats';
import { commandBusCount, commandDuration, dataBusCount, maxCommandDuration } from '~/engine/timing';
import { NONE, REQ_FLAG, type TileDetail } from '~/engine/types';
import { createFrameScheduler, type FrameScheduler } from '~/composables/frames';
import {
  nodeId,
  pathFromAddrForTree,
  rankLevelIndex,
  structuralDepth as specStructuralDepth,
} from '~/utils/traceTree';

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
  /** Trace entry of the command; for a request, its first command. */
  index: number;
  /** Command id; for a request, its type. */
  cmdId: number;
  /** Issue clock; for a request, its arrival. */
  clk: number;
  typeId: number;
  sourceId: number;
  addr: number[];
  bus: BusType;
  duration: number;
  busClk?: number;
  queueDelay?: number;
  /** Request still in flight when the trace ends. */
  inFlight?: boolean;
  /** Request: its record (first tile index) and position there. */
  reqK?: number;
  reqJ?: number;
  /** Request id in the trace (format 1.2; for a command, its request's), -1 without one. */
  reqId?: number;
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
  /** False while an edge of the window is estimated (its tile is still loading). */
  exact: boolean;
}

export interface ThroughputHover {
  /** Channel data / command bus chart, or an organization node's data-bus share. */
  series: 'data' | 'cmd' | 'node';
  /** Node rows: tree path and its name ("PseudoChannel 0 · Sid 0 · …"). */
  path?: number[];
  label?: string;
  value: number;
  binStartTime: number;
  binEndTime: number;
}

export interface ThroughputRangeStats {
  dataMeanUtil: number;
  cmdMeanUtil: number;
  exact: boolean;
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
  exact: boolean;
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

/** A request and its commands, selected together. req = -1: a command without a request (refresh, …). */
export interface LinkedSelection {
  /** Request number, or -1. */
  req: number;
  /** Where the request's bar is: its record (first tile index) and position; -1 while not found. */
  reqK: number;
  reqJ: number;
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
  clipLeft: number;
  clipRight: number;
}

/** Published by the Commands / Requests / Bank state renderers; read by the dock's leader-line overlay. */
export interface LinkGeometry {
  cmd?: (gidx: number) => LinkAnchor | null;
  req?: (g: LinkedSelection) => LinkAnchor | null;
  /** A command's marker in its bank's row. */
  bank?: (gidx: number) => LinkAnchor | null;
}

/** Hooks a panel exposes to the dev-only automation handle. */
export interface PanelDebug {
  refreshBankStats?: () => BankStatsSummary | null;
  headerTitles?: () => Record<string, string>;
}

export interface LoadProgress {
  /** Commands read so far, and in the whole trace (0 for live sessions). */
  read: number;
  total: number;
  /** Some of the trace can be drawn. */
  ready: boolean;
  /** Clock up to which the trace is read (Infinity once read). */
  frontier: number;
  done: boolean;
  error: string | null;
}

export interface Timeline {
  viewState: ViewState;
  rangeSelection: Ref<RangeSelection | null>;
  cursorMode: Ref<boolean>;
  cursorTime: Ref<number | null>;
  followLive: Ref<boolean>;
  engine: ShallowRef<TraceEngine | null>;
  /** Bumps when the trace's tiles or extent change (live data, loading). */
  dataVersion: Ref<number>;
  /** How far the engine has read the trace. */
  load: ShallowRef<LoadProgress | null>;
  frames: FrameScheduler;
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
  linkPreview: ShallowRef<LinkedSelection | null>;
  linkGeometry: LinkGeometry;
  panelDebug: Partial<Record<ViewMode, PanelDebug>>;
  clearSelection: () => void;
  hitForCommand: (gidx: number) => HitResult | null;
  hitForRequest: (k: number, j: number) => HitResult | null;
  canJumpCursor: ComputedRef<boolean>;
  placeCursor: (time: number, view: ViewMode) => void;
  jumpCursor: (direction: CursorJumpDirection) => boolean;
  clearCursor: () => void;
  setCursorMode: (enabled: boolean) => void;
  focusCommand: (gidx: number, from?: ViewMode) => void;
  navigateToCounterpart: (hit: HitResult, from?: ViewMode) => void;
  /** Move the viewport (clamped to the trace); every viewport change goes through here. */
  setView: (start: number, duration: number) => void;
}

// ── Constants ───────────────────────────────────────────────────────

/** Viewport glide when framing a selection (ms). */
const VIEW_GLIDE_MS = 320;
/** Narrowest window a selection is framed in (cycles). */
const LINK_MIN_WINDOW = 96;
const STATS_THROTTLE_MS = 100;
/** Initial window of a newly loaded trace (cycles). */
const INITIAL_WINDOW = 500;
/** Records searched past a command for its request's bar. */
const REQUEST_SEARCH_RECORDS = 16;

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

// ── Context ─────────────────────────────────────────────────────────

export function useTimeline(): Timeline {
  const tl = inject(TIMELINE_KEY, null);
  if (!tl) throw new Error('useTimeline() needs a page that called createTimeline()');
  return tl;
}

/** Address of entry j of a tile's commands, as one-element columns (for tree paths). */
function addrColumns(d: TileDetail, j: number, levels: number): Int32Array[] {
  return Array.from({ length: levels }, (_, k) => Int32Array.of(d.rawAddr[j * levels + k]!));
}

export function createTimeline(followLive: Ref<boolean>): Timeline {
  const sessionStore = useSessionStore();
  const uiStore = useUIStore();

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

  const engine = shallowRef<TraceEngine | null>(null);
  const dataVersion = ref(0);
  const load = shallowRef<LoadProgress | null>(null);
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
  const linkPreview = shallowRef<LinkedSelection | null>(null);
  const linkGeometry: LinkGeometry = {};
  const panelDebug: Timeline['panelDebug'] = {};
  const reduceMotion = import.meta.client && window.matchMedia('(prefers-reduced-motion: reduce)').matches;

  /** The viewport has been fitted to the current trace's first data. */
  let fitted = false;
  /** Set while the viewport moves for new data rather than for the user. */
  let boundsUpdate = false;

  // ── Viewport ──────────────────────────────────────────────────────

  function setView(start: number, duration: number) {
    const w = Math.max(viewState.minDuration, Math.min(duration, viewState.maxDuration));
    viewState.duration = w;
    viewState.start = Math.max(viewState.minTime, Math.min(start, viewState.maxTime - w));
  }

  /** Viewport bounds for the trace's current extent: fit once, then follow the tail or keep the window. */
  function applyBounds() {
    const e = engine.value;
    if (!e || Number.isNaN(e.firstClk) || Number.isNaN(e.endClk)) return;
    boundsUpdate = true;
    try {
      const span = Math.max(e.endClk - e.firstClk, maxCommandDuration(e.spec)) + 1;
      viewState.minTime = e.firstClk;
      viewState.maxTime = e.firstClk + span;
      viewState.maxDuration = span;
      if (!fitted) {
        viewState.start = e.firstClk;
        viewState.duration = Math.min(span, INITIAL_WINDOW);
        fitted = true;
        return;
      }
      const dur = Math.max(viewState.minDuration, Math.min(viewState.duration, span));
      const start = followLive.value && e.live ? viewState.maxTime - dur : viewState.start;
      viewState.duration = dur;
      viewState.start = Math.max(viewState.minTime, Math.min(start, viewState.maxTime - dur));
    } finally {
      boundsUpdate = false;
    }
  }

  // ── Viewport animation ───────────────────────────────────────────

  /** Glide in progress: centre moves linearly, width on a log scale, eased in and out. */
  let viewAnim: null | { c0: number; w0: number; c1: number; w1: number; clock: (now: number) => number } = null;
  /** The window the glide last set; any other change (wheel, drag, overview, live data) cancels it. */
  let viewSet = { start: 0, duration: 0 };

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
    frames.invalidate();
  }

  /** Step the glide to this frame; true while it continues. */
  function advance(): boolean {
    const a = viewAnim;
    if (!a) return false;
    if (viewState.start !== viewSet.start || viewState.duration !== viewSet.duration) {
      viewAnim = null;
      return false;
    }
    const now = (document.timeline?.currentTime as number | null) ?? performance.now();
    const t = Math.min(1, a.clock(now) / VIEW_GLIDE_MS);
    const e = t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2;
    const w = a.w0 * Math.pow(a.w1 / a.w0, e);
    setView(a.c0 + (a.c1 - a.c0) * e - w / 2, w);
    viewSet = { start: viewState.start, duration: viewState.duration };
    if (t >= 1) viewAnim = null;
    return viewAnim !== null;
  }

  const frames = createFrameScheduler(advance);

  // ── Window statistics (shared by every panel's inspector view) ────

  let statsTimer: ReturnType<typeof setTimeout> | null = null;
  let statsAgain = false;

  function throughputWindow(e: TraceEngine, t0: number, t1: number): ThroughputRangeStats | null {
    if (!(t1 > t0) || e.tiles.length === 0) return null;
    const c = busCycles(e, e.ctx, t0, t1);
    return {
      dataMeanUtil: c.data / ((t1 - t0) * dataBusCount(e.spec)),
      cmdMeanUtil: c.cmd / ((t1 - t0) * commandBusCount(e.spec)),
      exact: c.exact,
    };
  }

  function recomputeStats() {
    const e = engine.value;
    const t0 = viewState.start;
    const t1 = viewState.start + viewState.duration;
    // A window reaching past what is read so far can only be estimated.
    const known = <T extends { exact: boolean } | null>(s: T, end: number): T => (s && e && end > e.readFrontier ? { ...s, exact: false } : s);
    viewStats.value = e ? known(commandWindow(e, e.ctx, t0, t1), t1) : null;
    viewThroughputStats.value = e ? known(throughputWindow(e, t0, t1), t1) : null;
    const sel = rangeSelection.value;
    const ok = e && sel && sel.endTime > sel.startTime;
    rangeStats.value = ok ? known(commandWindow(e, e.ctx, sel.startTime, sel.endTime), sel.endTime) : null;
    throughputRangeStats.value = ok ? known(throughputWindow(e, sel.startTime, sel.endTime), sel.endTime) : null;
  }

  /** Refresh now, then at most every STATS_THROTTLE_MS while the view keeps moving. */
  function scheduleStats() {
    if (statsTimer) {
      statsAgain = true;
      return;
    }
    recomputeStats();
    statsTimer = setTimeout(() => {
      statsTimer = null;
      if (statsAgain) {
        statsAgain = false;
        scheduleStats();
      }
    }, STATS_THROTTLE_MS);
  }

  watch(() => [viewState.start, viewState.duration] as const, () => {
    scheduleStats();
    frames.invalidate();
  });
  watch(rangeSelection, () => {
    recomputeStats();
    frames.invalidate();
  });
  watch([cursorTime, hoveredCommand, hoveredRequest, throughputHover, bankHover, selection], () => frames.invalidate());

  // ── Linked selection ──────────────────────────────────────────────

  /** Request group of trace entry gidx; null while its tile is loading. */
  function groupOfCommand(gidx: number): LinkedSelection | null {
    const e = engine.value;
    if (!e) return null;
    const i = e.recordOfEntry(gidx);
    if (i < 0) return null;
    const d = e.detail(e.tiles[i]!.k);
    if (!d) return null;
    const reqNo = d.cmdReq[gidx - d.cmdFirst]!;
    if (reqNo === NONE) return { req: -1, reqK: -1, reqJ: -1, cmds: [gidx], focus: gidx };
    for (let ii = i; ii < Math.min(e.tiles.length, i + REQUEST_SEARCH_RECORDS); ii++) {
      const dd = e.detail(e.tiles[ii]!.k);
      if (!dd) break;
      const j = dd.reqNo.indexOf(reqNo);
      if (j >= 0) return { ...groupOfRequest(dd, j), focus: gidx };
    }
    return { req: reqNo, reqK: -1, reqJ: -1, cmds: [gidx], focus: gidx };
  }

  function groupOfRequest(d: TileDetail, j: number): LinkedSelection {
    const cmds = Array.from(d.reqCmds.subarray(d.reqCmdStart[j]!, d.reqCmdStart[j + 1]!));
    return { req: d.reqNo[j]!, reqK: d.k, reqJ: j, cmds, focus: cmds[0] ?? -1 };
  }

  function refreshLinkPreview() {
    const e = engine.value;
    const hc = hoveredCommand.value;
    const hr = hoveredRequest.value;
    let g: LinkedSelection | null = null;
    if (e && hc) g = groupOfCommand(hc.index);
    else if (e && hr && hr.reqK !== undefined && hr.reqJ !== undefined) {
      const d = e.detail(hr.reqK);
      if (d && hr.reqJ < d.reqNo.length) g = groupOfRequest(d, hr.reqJ);
    }
    linkPreview.value = g;
  }
  watch([hoveredCommand, hoveredRequest], refreshLinkPreview);

  /** A selection found before its request's tile loaded: fill in the request once it is there. */
  function completeSelection() {
    const s = selection.value;
    if (!s || s.req < 0 || s.reqK >= 0) return;
    const g = groupOfCommand(s.focus);
    if (g && g.reqK >= 0) selection.value = { ...g, focus: s.focus };
  }

  /** Time extent of a group, or null while its tiles load. */
  function groupExtent(g: LinkedSelection): [number, number] | null {
    const e = engine.value;
    if (!e) return null;
    let a = Number.POSITIVE_INFINITY;
    let b = Number.NEGATIVE_INFINITY;
    if (g.reqK >= 0) {
      const d = e.detail(g.reqK);
      if (d) {
        a = d.origin + d.reqT[g.reqJ]!;
        b = a + d.reqDur[g.reqJ]!;
      }
    }
    for (const i of g.cmds) {
      const ri = e.recordOfEntry(i);
      const d = ri >= 0 ? e.detail(e.tiles[ri]!.k) : undefined;
      if (!d) continue;
      const t = d.origin + d.cmdT[i - d.cmdFirst]!;
      a = Math.min(a, t);
      b = Math.max(b, t + commandDuration(e.spec, d.cmdId[i - d.cmdFirst]!));
    }
    return Number.isFinite(a) ? [a, b] : null;
  }

  /** Glide to the request and all its commands, with margin. */
  function frameGroup(g: LinkedSelection) {
    const ext = groupExtent(g);
    if (!ext) return;
    const [a, b] = ext;
    const w = Math.max(b - a + 2 * ((b - a) * 0.18 + 12), LINK_MIN_WINDOW);
    glideTo((a + b) / 2 - w / 2, w);
  }

  /** Select a group, show `target` (docked below `from` if parked) and bring the group into view in both panels. */
  function showLinked(g: LinkedSelection, target: 'command' | 'request', from?: ViewMode) {
    const e = engine.value;
    const spec = sessionStore.spec;
    if (!e || !spec || g.cmds.length === 0) return;
    selection.value = g;
    uiStore.reveal(target, from);
    // A request's commands share its bank, so one address locates the group in both trees.
    const at = g.cmds[0]!;
    const ri = e.recordOfEntry(at);
    const d = ri >= 0 ? e.detail(e.tiles[ri]!.k) : undefined;
    if (d) {
      const addr = addrColumns(d, at - d.cmdFirst, spec.levelNames.length);
      const sd = specStructuralDepth(spec);
      const rl = rankLevelIndex(spec);
      for (const [view, prefix] of [['command', 'cmd'], ['request', 'req']] as const) {
        if (!uiStore.stack.includes(view) || (view === 'request' && g.req < 0)) continue;
        const ids = ['_channel'];
        for (let k = rl; k < sd - 1; k++) ids.push(nodeId(prefix, pathFromAddrForTree(spec, addr, 0, k)));
        uiStore.mergeExpandedForNavigation(view, ids);
        uiStore.setTreeScrollTarget(view, { busType: view, path: pathFromAddrForTree(spec, addr, 0, sd - 1) });
      }
    }
    frameGroup(g);
  }

  /** Select trace entry `gidx` with its request and show it in the Commands panel. */
  function focusCommand(gidx: number, from?: ViewMode) {
    const g = groupOfCommand(gidx);
    if (g) {
      showLinked(g, 'command', from);
      return;
    }
    // Its tile is loading: frame it once it is there.
    pendingFocus = { gidx, from };
  }
  let pendingFocus: { gidx: number; from?: ViewMode } | null = null;

  /** Clicked bar: select its group and show the other side (command → Requests, request → Commands). */
  function navigateToCounterpart(hit: HitResult, from?: ViewMode) {
    const e = engine.value;
    if (!e) return;
    if (hit.bus === 'request' && hit.reqK !== undefined && hit.reqJ !== undefined) {
      const d = e.detail(hit.reqK);
      if (d) showLinked(groupOfRequest(d, hit.reqJ), 'command', from);
      return;
    }
    const g = groupOfCommand(hit.index);
    if (g) showLinked(g, g.req >= 0 ? 'request' : 'command', from);
  }

  /** Clears the selected request and commands, and a selected range. */
  function clearSelection() {
    selection.value = null;
    rangeSelection.value = null;
  }

  /** Inspector record of a command, as a hover over its bar would produce. */
  function hitForCommand(gidx: number): HitResult | null {
    const e = engine.value;
    if (!e) return null;
    const ri = e.recordOfEntry(gidx);
    const d = ri >= 0 ? e.detail(e.tiles[ri]!.k) : undefined;
    if (!d) return null;
    const j = gidx - d.cmdFirst;
    const L = e.ctx.levels;
    return {
      index: gidx,
      cmdId: d.cmdId[j]!,
      clk: d.rawClk[j]!,
      typeId: d.rawType[j]!,
      sourceId: d.rawSrc[j]!,
      addr: Array.from(d.rawAddr.subarray(j * L, (j + 1) * L)),
      bus: 'command',
      duration: commandDuration(e.spec, d.cmdId[j]!),
      reqId: d.rawReqId[j]!,
    };
  }

  /** Inspector record of request j of record k, as a hover over its bar would produce. */
  function hitForRequest(k: number, j: number): HitResult | null {
    const e = engine.value;
    const d = e?.detail(k);
    if (!e || !d || j >= d.reqNo.length) return null;
    const L = e.ctx.levels;
    const flags = d.reqFlags[j]!;
    return {
      index: d.reqCmds[d.reqCmdStart[j]!] ?? -1,
      cmdId: flags & REQ_FLAG.WRITE ? 1 : flags & REQ_FLAG.OTHER_TYPE ? 2 : 0,
      clk: d.reqArrive[j]!,
      typeId: flags & REQ_FLAG.WRITE ? 1 : flags & REQ_FLAG.OTHER_TYPE ? 2 : 0,
      sourceId: d.reqSrc[j]!,
      addr: Array.from(d.reqAddr.subarray(j * L, (j + 1) * L)),
      bus: 'request',
      duration: Math.max(0, d.reqDur[j]! - d.reqQueue[j]!),
      queueDelay: d.reqQueue[j]!,
      inFlight: (flags & REQ_FLAG.IN_FLIGHT) !== 0,
      reqK: k,
      reqJ: j,
      reqId: d.reqId[j]!,
    };
  }

  // ── Cursor ────────────────────────────────────────────────────────

  const jumpView = computed(() => cursorView.value ?? uiStore.activeView);
  const canJumpCursor = computed(() => cursorTime.value != null && jumpView.value !== 'throughput');

  function placeCursor(time: number, view: ViewMode) {
    // Clocks are whole cycles; a fractional cursor would make Next / Previous look like no-ops.
    cursorTime.value = Math.round(Math.max(viewState.minTime, Math.min(viewState.maxTime, time)));
    cursorView.value = view;
  }

  /** The next (or previous) event start in the loaded tiles around t; null if none is loaded. */
  function nextEvent(t: number, direction: CursorJumpDirection, requests: boolean): number | null {
    const e = engine.value;
    if (!e || e.tiles.length === 0) return null;
    let i = Math.max(0, e.recordIndexAt(t));
    if (e.recordIndexAt(t) < 0) i = direction === 'next' ? Math.max(0, e.range(t, t + 1)[0]) : Math.max(0, e.range(t, t + 1)[0] - 1);
    let best: number | null = null;
    for (let steps = 0; i >= 0 && i < e.tiles.length && steps < 64; steps++, i += direction === 'next' ? 1 : -1) {
      const r = e.tiles[i]!;
      if ((requests ? r.nReq : r.nCmd) === 0) continue;
      const d = e.detail(r.k);
      if (!d) return best;
      const T = requests ? d.reqT : d.cmdT;
      for (let j = 0; j < T.length; j++) {
        const x = d.origin + T[j]!;
        if (direction === 'next' ? x > t && (best === null || x < best) : x < t && (best === null || x > best)) best = x;
      }
      // Requests sit in the record where they complete, so look one record further before stopping.
      if (best !== null && !requests) return best;
      if (best !== null && requests && steps > 0) return best;
    }
    return best;
  }

  function jumpCursor(direction: CursorJumpDirection): boolean {
    const current = cursorTime.value;
    if (current == null) return false;
    const t = nextEvent(current, direction, jumpView.value === 'request');
    if (t === null) return false;
    cursorTime.value = Math.max(viewState.minTime, Math.min(viewState.maxTime, t));
    if (t < viewState.start || t > viewState.start + viewState.duration) setView(t - viewState.duration / 2, viewState.duration);
    return true;
  }

  function clearCursor() {
    cursorTime.value = null;
    cursorView.value = null;
  }

  /** Leaving cursor mode removes the cursor, whose controls go with the mode. */
  function setCursorMode(enabled: boolean) {
    cursorMode.value = enabled;
    if (!enabled) clearCursor();
  }

  // ── Engine ────────────────────────────────────────────────────────

  let unsubscribe: (() => void) | null = null;
  let tickTimer: ReturnType<typeof setTimeout> | null = null;
  let lastTiles = -1;
  let lastEnd = Number.NaN;

  /** New summaries, extent or tile detail: redraw now, refresh the rest at most every 50 ms. */
  function onEngineChange() {
    frames.invalidate();
    if (tickTimer) return;
    tickTimer = setTimeout(() => {
      tickTimer = null;
      const e = engine.value;
      if (!e) return;
      const l = load.value;
      const ready = e.tiles.length > 0 || e.done;
      const frontier = e.readFrontier;
      if (!l || l.read !== e.entries || l.total !== e.total || l.ready !== ready || l.done !== e.done || l.error !== e.error || l.frontier !== frontier) {
        load.value = { read: e.entries, total: e.total, ready, done: e.done, error: e.error, frontier };
      }
      if (e.tiles.length !== lastTiles || e.endClk !== lastEnd) {
        lastTiles = e.tiles.length;
        lastEnd = e.endClk;
        applyBounds();
        dataVersion.value++;
      }
      scheduleStats();
      refreshLinkPreview();
      completeSelection();
      if (pendingFocus) {
        const p = pendingFocus;
        pendingFocus = null;
        focusCommand(p.gidx, p.from);
      }
    }, 50);
  }

  function adopt(e: TraceEngine | null) {
    unsubscribe?.();
    unsubscribe = null;
    engine.value = e;
    load.value = null;
    fitted = false;
    lastTiles = -1;
    lastEnd = Number.NaN;
    clearCursor();
    rangeSelection.value = null;
    selection.value = null;
    linkPreview.value = null;
    hoveredCommand.value = null;
    hoveredRequest.value = null;
    bankHover.value = null;
    throughputHover.value = null;
    viewAnim = null;
    if (e) {
      unsubscribe = e.onChange(onEngineChange);
      onEngineChange();
    }
    frames.invalidate();
  }

  onMounted(() => adopt(sessionStore.engine));
  watch(() => sessionStore.engine, e => adopt(e));

  // Enabling "follow" snaps to the tail without waiting for the next batch.
  watch(followLive, (follow) => {
    if (!follow || !engine.value?.live) return;
    boundsUpdate = true;
    const dur = Math.max(viewState.minDuration, Math.min(viewState.duration, viewState.maxDuration));
    viewState.duration = dur;
    viewState.start = Math.max(viewState.minTime, viewState.maxTime - dur);
    boundsUpdate = false;
  });

  // Zooming, panning or jumping to an item stops following the live tail.
  watch(() => [viewState.start, viewState.duration], () => {
    if (!boundsUpdate && followLive.value) followLive.value = false;
  }, { flush: 'sync' });

  onUnmounted(() => {
    unsubscribe?.();
    if (statsTimer) clearTimeout(statsTimer);
    if (tickTimer) clearTimeout(tickTimer);
    if (import.meta.dev && import.meta.client) delete (window as any).__ramulatorViz;
  });

  const tl: Timeline = {
    viewState,
    rangeSelection,
    cursorMode,
    cursorTime,
    followLive,
    engine,
    dataVersion,
    load,
    frames,
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
    clearSelection,
    hitForCommand,
    hitForRequest,
    canJumpCursor,
    placeCursor,
    jumpCursor,
    clearCursor,
    setCursorMode,
    focusCommand,
    navigateToCounterpart,
    setView,
  };
  provide(TIMELINE_KEY, tl);

  if (import.meta.dev && import.meta.client) {
    // Hook for automated checks.
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
      engineInfo: () => {
        const e = engine.value;
        return e ? { entries: e.entries, total: e.total, tiles: e.tiles.length, done: e.done, firstClk: e.firstClk, endClk: e.endClk } : null;
      },
      frameCount: () => frames.count,
      layout: () => ({ stack: [...uiStore.stack], parked: [...uiStore.parked], active: uiStore.activeView, weights: { ...uiStore.weights } }),
      selection: () => selection.value,
      anchor: (kind: 'cmd' | 'req' | 'bank', idx: number) =>
        (kind === 'cmd' ? linkGeometry.cmd?.(idx) : kind === 'bank' ? linkGeometry.bank?.(idx) : selection.value ? linkGeometry.req?.(selection.value) : null) ?? null,
      viewStats: () => viewStats.value,
    };
  }

  return tl;
}

export { lowerBound, recordStart };
