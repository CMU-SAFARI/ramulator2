/**
 * Bank State view: per-bank state reconstructed from the issued-command stream.
 *
 * Covers every standard the visualizer can load, including write recovery (WRA), refresh/RFM,
 * wildcard (all-bank / same-bank) commands and per-standard timing names. The shading is a
 * reconstruction from the command stream plus the trace's timing table, not simulator state.
 *
 * Only type imports: this module runs in the app, in the streaming worker, and under plain Node
 * (scripts/verify-bank-state.ts).
 */
import type { TraceArrays, TraceSpec } from '~/composables/useTrace';

// ── Kinds, markers, colors ──────────────────────────────────────────

/** Mutually exclusive per-bank span kinds. OPEN is further split at draw / stats time. */
export const BANK_KIND = { IDLE: 0, ACT: 1, PRE: 2, WRREC: 3, REF: 4, OPEN: 5 } as const;
const SENTINEL = 255;

export const BANK_MARKER = { NONE: 0, ACT: 1, ACT2: 2, RD: 3, RDA: 4, WR: 5, WRA: 6, PRE: 7, REF: 8 } as const;

/** Fill color of each span kind. */
export const BANK_COLORS = {
  idle: '#ececec',
  act: '#f6c9a8',
  pre: '#b8b8b8',
  wrrec: '#e4d1f2',
  ref: '#c9e4c5',
  open: '#ffffff',
  bus: '#dbe7f6',
  bg: '#6f9fd8',
  busLane: '#1f5fb4',
  fawLane: '#d9534f',
} as const;

export const BANK_MARKER_COLORS: Record<number, string> = {
  [BANK_MARKER.ACT]: '#222222',
  [BANK_MARKER.ACT2]: '#222222',
  [BANK_MARKER.RD]: '#1f5fb4',
  [BANK_MARKER.RDA]: '#eb6834',
  [BANK_MARKER.WR]: '#0f9488',
  [BANK_MARKER.WRA]: '#8e44ad',
  [BANK_MARKER.PRE]: '#555555',
  [BANK_MARKER.REF]: '#7a4fd0',
};

/** Hex color of a span kind. */
export function bankKindColor(kind: number): string {
  switch (kind) {
    case BANK_KIND.ACT: return BANK_COLORS.act;
    case BANK_KIND.PRE: return BANK_COLORS.pre;
    case BANK_KIND.WRREC: return BANK_COLORS.wrrec;
    case BANK_KIND.REF: return BANK_COLORS.ref;
    case BANK_KIND.OPEN: return BANK_COLORS.open;
    default: return BANK_COLORS.idle;
  }
}

export function bankKindName(kind: number): string {
  switch (kind) {
    case BANK_KIND.ACT: return 'activating';
    case BANK_KIND.PRE: return 'precharging';
    case BANK_KIND.WRREC: return 'write recovery';
    case BANK_KIND.REF: return 'refreshing';
    case BANK_KIND.OPEN: return 'row open';
    default: return 'closed & idle';
  }
}

// ── Standard detection and timing resolution ────────────────────────

export type BankFamily =
  | 'DDR3' | 'DDR4' | 'DDR5' | 'LPDDR5' | 'LPDDR6' | 'GDDR6' | 'GDDR7'
  | 'HBM1' | 'HBM2' | 'HBM3' | 'HBM4' | 'unknown';

const KNOWN_FAMILIES: BankFamily[] = [
  'LPDDR5', 'LPDDR6', 'GDDR6', 'GDDR7', 'HBM1', 'HBM2', 'HBM3', 'HBM4', 'DDR3', 'DDR4', 'DDR5',
];

/** `dram_type` is often empty, so fall back to level / command / timing-name signatures. */
export function bankDetectFamily(spec: TraceSpec, dramType = ''): BankFamily {
  const t = dramType.toUpperCase();
  for (const f of KNOWN_FAMILIES) if (t.startsWith(f)) return f;
  if (t === 'HBM') return 'HBM1';
  const levels = new Set(spec.levelNames);
  const cmds = new Set(spec.commandNames);
  const tim = new Set(spec.timingNames);
  if (levels.has('PseudoChannel')) return tim.has('nRTPL') ? 'HBM2' : 'HBM3';
  if (cmds.has('RD_S')) return 'LPDDR6';
  if (cmds.has('ACT1')) return 'LPDDR5';
  if (tim.has('nRTPSB')) return 'GDDR7';
  if (!levels.has('Rank')) return tim.has('nRTPL') ? 'HBM1' : 'GDDR6';
  if (cmds.has('PREsb')) return 'DDR5';
  if (levels.has('BankGroup')) return 'DDR4';
  return 'DDR3';
}

/** Commands that occupy an nFAW window slot (preceding side of each standard's nFAW constraint). */
const FAW_COMMANDS: Record<BankFamily, string[]> = {
  DDR3: ['ACT'],
  DDR4: ['ACT'],
  DDR5: ['ACT', 'REFsb', 'RFMsb'],
  LPDDR5: ['ACT1', 'REFpb'],
  LPDDR6: ['ACT2'],
  GDDR6: ['ACT'],
  GDDR7: [],
  HBM1: ['ACT', 'REFpb'],
  HBM2: ['ACT', 'REFpb'],
  HBM3: ['ACT', 'REFpb', 'RFMpb'],
  HBM4: ['ACT', 'REFpb', 'RFMpb'],
  unknown: ['ACT', 'ACT1'],
};

export interface BankTimingRef {
  /** Timing name(s) the value came from, e.g. "nRTP + nRP". */
  expr: string;
  /** Controller ticks (same unit as the trace clock). */
  value: number;
}

const ACTION = { NONE: 0, ACT: 1, ACT1: 2, ACT2: 3, PRE: 4, WRA: 5, REF: 6 } as const;

export interface BankCmdRule {
  action: number;
  /** ACT/ACT2: tRCD. PRE: precharge duration. WRA: write recovery. REF: refresh duration. */
  d1: number;
  /** WRA only: tRP after write recovery. */
  d2: number;
  /** Data-bus occupancy from command issue (nBL), 0 if not accessing. */
  bus: number;
  /** Same-bank-group column-to-column blocking (nCCD_L), 0 if none. */
  ccd: number;
  faw: boolean;
  marker: number;
}

export interface BankRules {
  family: BankFamily;
  /** Family for display; signature-detected families say so (HBM3 and HBM4 share every name). */
  familyLabel: string;
  cmd: BankCmdRule[];
  /** Resolved timings shown in the legend (null = not available in this standard). */
  timings: {
    rcd: BankTimingRef | null;
    rdaPre: BankTimingRef | null;
    rp: BankTimingRef | null;
    rpab: BankTimingRef | null;
    wrRec: BankTimingRef | null;
    bl: BankTimingRef | null;
    ccdl: BankTimingRef | null;
    faw: BankTimingRef | null;
    ref: { cmd: string; t: BankTimingRef | null }[];
  };
  hasBgBlocking: boolean;
  hasFaw: boolean;
  /** Human-readable notes on omitted or approximated states. */
  notes: string[];
}

function timingRef(spec: TraceSpec, names: string[]): BankTimingRef | null {
  for (const n of names) {
    const i = spec.timingNames.indexOf(n);
    if (i >= 0 && spec.timingValues[i]! >= 0) return { expr: n, value: spec.timingValues[i]! };
  }
  return null;
}

function sumRefs(...refs: (BankTimingRef | null | number)[]): BankTimingRef | null {
  let value = 0;
  const parts: string[] = [];
  for (const r of refs) {
    if (r === null) return null;
    if (typeof r === 'number') {
      value += r;
      parts.push(String(r));
    } else {
      value += r.value;
      parts.push(r.expr);
    }
  }
  return { expr: parts.join(' + '), value };
}

const REF_TIMINGS: Record<string, string[]> = {
  REFab: ['nRFC', 'nRFCab'],
  REFpb: ['nRFCpb'],
  REFsb: ['nRFCsb'],
  RFMab: ['nRFMab', 'nRFM'],
  RFMpb: ['nRFMpb'],
  RFMsb: ['nRFMsb'],
  VRR: ['nVRR'],
};

/** Per-command reconstruction rules, resolved once from the trace's timing table. */
export function bankResolveRules(spec: TraceSpec, dramType = ''): BankRules {
  const family = bankDetectFamily(spec, dramType);
  const notes: string[] = [];
  const t = (...names: string[]) => timingRef(spec, names);

  const rcd = t('nRCDRD', 'nRCD', 'nRCDr');
  const rtp = t('nRTP', 'nRTPL', 'nRTPSB');
  const rtpL = t('nRTP_L') ?? rtp;
  const rp = t('nRP');
  const rpab = t('nRPab', 'nRP');
  const bl = t('nBL', 'nBL_min');
  const blL = t('nBL_min_L') ?? bl;
  const hasBgLevel = spec.levelNames.includes('BankGroup');
  const ccdl = hasBgLevel ? t('nCCDL') : null;
  const ccdlL = hasBgLevel ? (t('nCCDL_L') ?? ccdl) : null;
  const faw = t('nFAW');
  const wl = t('nCWL', 'nWL');
  const wr = t('nWR', 'nWTP');
  // WRA -> ACT is "WL + BL + WR + RP" everywhere, with the burst term differing per standard.
  const blW = family === 'LPDDR6' ? t('nBL_max') : bl;
  const blWL = family === 'LPDDR6' ? (t('nBL_max_L') ?? blW) : blW;
  const wrExtra = family === 'LPDDR5' ? 1 : 0;
  const wrRec = wrExtra ? sumRefs(wl, blW, wrExtra, wr) : sumRefs(wl, blW, wr);
  const wrRecL = wrExtra ? sumRefs(wl, blWL, wrExtra, wr) : sumRefs(wl, blWL, wr);
  const rdaPre = sumRefs(rtp, rp);
  const rdaPreL = sumRefs(rtpL, rp);

  if (!rcd) notes.push('No tRCD in this standard: ACT is drawn as an immediate row open.');
  if (!rp) notes.push('No tRP in this standard: precharges are drawn as instantaneous.');
  if (!rdaPre) notes.push('No tRTP: RDA precharge time is incomplete.');
  if (!wrRec && spec.commandNames.some(n => n.startsWith('WRA'))) {
    notes.push('Write recovery timings unavailable: WRA is drawn as a plain precharge.');
  }
  if (!hasBgLevel) notes.push('No BankGroup level: "bank group blocked" is not shown.');
  else if (!ccdl) notes.push('No nCCDL: "bank group blocked" is not shown.');
  if (!faw) notes.push('No nFAW in this standard: no ACT / tFAW lane.');
  if (family === 'DDR5') notes.push('DDR5: bank-group blocking uses nCCDL (nCCDM / write variants not modelled).');
  if (family === 'unknown') notes.push('Unrecognised standard: tFAW counts opening commands only.');

  const fawSet = new Set(FAW_COMMANDS[family]);
  const refTimings: { cmd: string; t: BankTimingRef | null }[] = [];
  const cmd: BankCmdRule[] = [];

  for (let id = 0; id < spec.commandNames.length; id++) {
    const name = spec.commandNames[id]!;
    const meta = spec.commandMeta[id]!;
    const isL = name.endsWith('_L');
    const upper = name.toUpperCase();
    const isWrite = upper.includes('WR') || upper.includes('WRITE');
    const r: BankCmdRule = {
      action: ACTION.NONE, d1: 0, d2: 0, bus: 0, ccd: 0, faw: fawSet.has(name), marker: BANK_MARKER.NONE,
    };
    if (family === 'unknown' && meta.isOpening) r.faw = true;

    if (name === 'ACT1') {
      r.action = ACTION.ACT1;
      r.marker = BANK_MARKER.ACT;
    } else if (name === 'ACT2') {
      r.action = ACTION.ACT2;
      r.d1 = rcd?.value ?? 0;
      r.marker = BANK_MARKER.ACT2;
    } else if (meta.isOpening) {
      r.action = ACTION.ACT;
      r.d1 = rcd?.value ?? 0;
      r.marker = BANK_MARKER.ACT;
    } else if (meta.isAccessing) {
      r.bus = (isL ? blL : bl)?.value ?? 0;
      r.ccd = (isL ? ccdlL : ccdl)?.value ?? 0;
      if (meta.isClosing) {
        const rec = isL ? wrRecL : wrRec;
        if (isWrite && rec) {
          r.action = ACTION.WRA;
          r.d1 = rec.value;
          r.d2 = rp?.value ?? 0;
        } else {
          // RDA: precharging from the RDA itself for tRTP + tRP.
          r.action = ACTION.PRE;
          r.d1 = isWrite ? (rp?.value ?? 0) : ((isL ? rdaPreL : rdaPre)?.value ?? rp?.value ?? 0);
        }
        r.marker = isWrite ? BANK_MARKER.WRA : BANK_MARKER.RDA;
      } else {
        r.marker = isWrite ? BANK_MARKER.WR : BANK_MARKER.RD;
      }
    } else if (meta.isClosing) {
      r.action = ACTION.PRE;
      r.d1 = (name === 'PREab' ? rpab : rp)?.value ?? 0;
      r.marker = BANK_MARKER.PRE;
    } else if (meta.isRefreshing) {
      const ref = t(...(REF_TIMINGS[name] ?? []));
      refTimings.push({ cmd: name, t: ref });
      if (!ref) notes.push(`No duration for ${name}: drawn as instantaneous.`);
      r.action = ACTION.REF;
      r.d1 = ref?.value ?? 0;
      r.marker = BANK_MARKER.REF;
    }
    cmd.push(r);
  }

  const fromType = KNOWN_FAMILIES.some(f => dramType.toUpperCase().startsWith(f));
  const familyLabel = fromType ? family : `${family === 'HBM3' ? 'HBM3/HBM4' : family}, detected from the trace's names`;

  return {
    family,
    familyLabel,
    cmd,
    timings: {
      rcd,
      rdaPre,
      rp,
      rpab: spec.commandNames.includes('PREab') ? rpab : null,
      wrRec,
      bl,
      ccdl,
      faw,
      ref: refTimings,
    },
    hasBgBlocking: !!ccdl,
    hasFaw: !!faw && fawSet.size > 0,
    notes,
  };
}

// ── Layout: flat bank lanes and domains ─────────────────────────────

export interface BankLayout {
  levelNames: string[];
  levelSizes: number[];
  /** First level contributing to the lane index (level 0 = Channel holds the global channel id). */
  firstLevel: number;
  bankLevel: number;
  /** Mixed-radix strides for levels firstLevel..bankLevel (0 elsewhere). */
  strides: number[];
  numBanks: number;
  /** Data-bus domain level (< firstLevel = the whole trace). */
  busLevel: number;
  /** tFAW domain level, null if the standard has no nFAW. */
  fawLevel: number | null;
  /** Bank-group domain level, null if no bank-group blocking is drawn. */
  bgLevel: number | null;
  sidLevel: number | null;
  /** Deepest level shown as a sidebar header; levels below it are flattened into bank labels. */
  headerMaxLevel: number;
}

export function bankBuildLayout(spec: TraceSpec, rulesIn?: BankRules): BankLayout {
  const rules = rulesIn ?? bankResolveRules(spec);
  const names = spec.levelNames;
  const sizes = Array.from(spec.levelSizes, v => Math.max(1, v));
  let bankLevel = names.indexOf('Bank');
  if (bankLevel < 0) {
    const row = names.indexOf('Row');
    bankLevel = Math.max(0, (row >= 0 ? row : names.length - 1) - 1);
  }
  const firstLevel = names[0] === 'Channel' && bankLevel >= 1 ? 1 : 0;
  const strides = new Array<number>(names.length).fill(0);
  strides[bankLevel] = 1;
  for (let k = bankLevel - 1; k >= firstLevel; k--) strides[k] = strides[k + 1]! * sizes[k + 1]!;
  const numBanks = strides[firstLevel]! * sizes[firstLevel]!;

  const idx = (n: string) => {
    const i = names.indexOf(n);
    return i >= firstLevel && i <= bankLevel ? i : -1;
  };
  const pc = idx('PseudoChannel');
  const rank = idx('Rank');
  const bg = idx('BankGroup');
  const sid = idx('Sid');
  const whole = firstLevel - 1;

  const busLevel = pc >= 0 ? pc : whole;
  const fawLevel = rules.hasFaw ? (pc >= 0 ? pc : rank >= 0 ? rank : whole) : null;
  const bgLevel = rules.hasBgBlocking && bg >= 0 ? bg : null;

  return {
    levelNames: [...names],
    levelSizes: sizes,
    firstLevel,
    bankLevel,
    strides,
    numBanks,
    busLevel,
    fawLevel,
    bgLevel,
    sidLevel: sid >= 0 ? sid : null,
    headerMaxLevel: Math.max(busLevel, fawLevel ?? whole),
  };
}

/** Number of domains at `level` (1 when level is above the lane levels). */
export function bankDomCount(L: BankLayout, level: number): number {
  return level < L.firstLevel ? 1 : L.numBanks / L.strides[level]!;
}

export function bankDomOf(L: BankLayout, lane: number, level: number): number {
  return level < L.firstLevel ? 0 : Math.floor(lane / L.strides[level]!);
}

/** Lanes [first, first + count) of the domain at `level` identified by `coords` (levels firstLevel..level). */
export function bankCoordsRange(L: BankLayout, coords: number[]): { level: number; dom: number; first: number; count: number } {
  const level = L.firstLevel + coords.length - 1;
  if (coords.length === 0) return { level, dom: 0, first: 0, count: L.numBanks };
  let first = 0;
  for (let i = 0; i < coords.length; i++) first += coords[i]! * L.strides[L.firstLevel + i]!;
  const count = L.strides[level]!;
  return { level, dom: first / count, first, count };
}

export function bankLaneCoord(L: BankLayout, lane: number, level: number): number {
  return Math.floor(lane / L.strides[level]!) % L.levelSizes[level]!;
}

/** Bank label ("S1 BG0 b3"), flattening the levels below the sidebar headers. */
export function bankLaneLabel(L: BankLayout, lane: number): string {
  const parts: string[] = [];
  for (let k = Math.max(L.firstLevel, L.headerMaxLevel + 1); k <= L.bankLevel; k++) {
    const v = bankLaneCoord(L, lane, k);
    const n = L.levelNames[k]!;
    if (n === 'Sid') {
      if (L.levelSizes[k]! > 1) parts.push(`S${v}`);
    } else if (n === 'BankGroup') parts.push(`BG${v}`);
    else if (n === 'Bank') parts.push(`b${v}`);
    else if (n === 'PseudoChannel') parts.push(`PC${v}`);
    else if (n === 'Rank') parts.push(`R${v}`);
    else parts.push(`${n}${v}`);
  }
  return parts.join(' ');
}

/** Full hierarchical path of a lane, e.g. "PseudoChannel 0 · Sid 0 · BankGroup 1 · Bank 3". */
export function bankLanePath(L: BankLayout, lane: number): string {
  const parts: string[] = [];
  for (let k = L.firstLevel; k <= L.bankLevel; k++) parts.push(`${L.levelNames[k]} ${bankLaneCoord(L, lane, k)}`);
  return parts.join(' · ');
}

/**
 * Returns (traceIndex) => target bank lanes. A -1 address level is a wildcard (same matching rule
 * as DRAMDevice::bank_matches), which covers all-bank and same-bank commands. The returned array
 * is reused for single-bank commands; copy it before the next call if it must be kept.
 */
export function bankTargetResolver(L: BankLayout, addr: ArrayLike<Int32Array>): (i: number) => Int32Array {
  const fanCache = new Map<number, Int32Array>();
  const single = new Int32Array(1);
  return (i: number): Int32Array => {
    let lane = 0;
    let wild = false;
    for (let k = L.firstLevel; k <= L.bankLevel; k++) {
      const a = addr[k]![i]!;
      if (a < 0) wild = true;
      else lane += a * L.strides[k]!;
    }
    if (!wild) {
      single[0] = lane;
      return single;
    }
    let key = 0;
    for (let k = L.firstLevel; k <= L.bankLevel; k++) {
      const a = addr[k]![i]!;
      key = key * (L.levelSizes[k]! + 1) + (a < 0 ? L.levelSizes[k]! : a);
    }
    let hit = fanCache.get(key);
    if (!hit) {
      let lanes = [0];
      for (let k = L.firstLevel; k <= L.bankLevel; k++) {
        const a = addr[k]![i]!;
        const s = L.strides[k]!;
        if (a >= 0) lanes = lanes.map(l => l + a * s);
        else {
          const next: number[] = [];
          for (const l of lanes) for (let v = 0; v < L.levelSizes[k]!; v++) next.push(l + v * s);
          lanes = next;
        }
      }
      hit = Int32Array.from(lanes.filter(l => l >= 0 && l < L.numBanks));
      fanCache.set(key, hit);
    }
    return hit;
  };
}

// ── State reconstruction ────────────────────────────────────────────

export interface BankIntervalSet {
  /** Per-domain ranges into start / end / cause; length = domains + 1. */
  offsets: Uint32Array;
  start: Float64Array;
  end: Float64Array;
  /** Trace index of the first command contributing to each merged interval. */
  cause: Int32Array;
}

export interface BankStateData {
  layout: BankLayout;
  rules: BankRules;
  numEntries: number;
  firstClk: number;
  endClk: number;
  /**
   * Per-bank spans tiling [min(0, firstClk), end]. Bank b owns indices [spanOffsets[b], spanOffsets[b+1]);
   * the last one is a sentinel whose start is the end of the bank's final span.
   */
  spanOffsets: Uint32Array;
  spanStart: Float64Array;
  spanKind: Uint8Array;
  /** Trace index of the command that started the span (-1 = initial idle). */
  spanCause: Int32Array;
  /** [clk, clk + nBL) of every column access, merged per data-bus domain. */
  bus: BankIntervalSet;
  /** [clk, clk + nCCD_L) of every column access, merged per bank-group domain. */
  bg: BankIntervalSet | null;
  /** Windows in which the trailing nFAW already holds four activations, per tFAW domain. */
  faw: BankIntervalSet | null;
  /** Whether any write-recovery / refresh span exists (titles list those categories only then). */
  hasWrrecSpans: boolean;
  hasRefSpans: boolean;
}

class IntervalBuilder {
  s: number[][];
  e: number[][];
  c: number[][];
  constructor(domains: number) {
    this.s = Array.from({ length: domains }, () => []);
    this.e = Array.from({ length: domains }, () => []);
    this.c = Array.from({ length: domains }, () => []);
  }

  /** Online interval merge: starts arrive sorted; touching intervals coalesce. */
  add(d: number, s: number, e: number, cause: number) {
    if (e <= s) return;
    const ss = this.s[d]!;
    const ee = this.e[d]!;
    const n = ss.length;
    if (n > 0 && s <= ee[n - 1]!) {
      if (e > ee[n - 1]!) ee[n - 1] = e;
    } else {
      ss.push(s);
      ee.push(e);
      this.c[d]!.push(cause);
    }
  }

  pack(): BankIntervalSet {
    const D = this.s.length;
    const offsets = new Uint32Array(D + 1);
    for (let d = 0; d < D; d++) offsets[d + 1] = offsets[d]! + this.s[d]!.length;
    const total = offsets[D]!;
    const start = new Float64Array(total);
    const end = new Float64Array(total);
    const cause = new Int32Array(total);
    for (let d = 0; d < D; d++) {
      const o = offsets[d]!;
      start.set(this.s[d]!, o);
      end.set(this.e[d]!, o);
      cause.set(this.c[d]!, o);
    }
    return { offsets, start, end, cause };
  }
}

/**
 * Reconstruct per-bank states.
 *
 * A command reaching a bank at clk c truncates whatever span is still pending at c and never
 * extends one. Precharges (PRE*) only affect banks that are activating or open;
 * a PREab reaching an already-closed bank is a no-op.
 */
export function bankBuildState(
  spec: TraceSpec,
  arrays: TraceArrays,
  numEntries: number,
  dramType = '',
): BankStateData {
  const rules = bankResolveRules(spec, dramType);
  const L = bankBuildLayout(spec, rules);
  const NB = L.numBanks;
  const n = Math.min(numEntries, arrays.clk.length);
  const clk = arrays.clk;
  const cmdId = arrays.cmdId;
  const addr = Array.from(arrays.addr);
  const firstClk = n > 0 ? Number(clk[0]) : 0;
  const endClk = n > 0 ? Number(clk[n - 1]) : 0;

  // Every bank starts closed & idle at cycle 0.
  const origin = Math.min(0, firstClk);
  const sStart: number[][] = Array.from({ length: NB }, () => [origin]);
  const sKind: number[][] = Array.from({ length: NB }, () => [BANK_KIND.IDLE]);
  const sCause: number[][] = Array.from({ length: NB }, () => [-1]);
  // Pending future boundaries per bank, ascending in time.
  const tT: number[][] = Array.from({ length: NB }, () => []);
  const tK: number[][] = Array.from({ length: NB }, () => []);
  const tC: number[][] = Array.from({ length: NB }, () => []);
  const act1Pending = new Uint8Array(NB);

  const push = (b: number, t: number, k: number, cause: number) => {
    const st = sStart[b]!;
    const last = st.length - 1;
    if (t <= st[last]!) {
      // Zero-length predecessor: replace it.
      sKind[b]![last] = k;
      sCause[b]![last] = cause;
      return;
    }
    st.push(t);
    sKind[b]!.push(k);
    sCause[b]!.push(cause);
  };

  /** Commit pending boundaries at or before c. */
  const advance = (b: number, c: number) => {
    const tt = tT[b]!;
    if (tt.length === 0) return;
    let i = 0;
    while (i < tt.length && tt[i]! <= c) {
      push(b, tt[i]!, tK[b]![i]!, tC[b]![i]!);
      i++;
    }
    if (i > 0) {
      tt.splice(0, i);
      tK[b]!.splice(0, i);
      tC[b]!.splice(0, i);
    }
  };

  const truncate = (b: number) => {
    tT[b]!.length = 0;
    tK[b]!.length = 0;
    tC[b]!.length = 0;
  };

  const later = (b: number, t: number, k: number, cause: number) => {
    tT[b]!.push(t);
    tK[b]!.push(k);
    tC[b]!.push(cause);
  };

  const currentKind = (b: number) => {
    const ks = sKind[b]!;
    return ks[ks.length - 1]!;
  };

  const targets = bankTargetResolver(L, addr);

  const busB = new IntervalBuilder(bankDomCount(L, L.busLevel));
  const bgB = L.bgLevel !== null ? new IntervalBuilder(bankDomCount(L, L.bgLevel)) : null;
  const fawB = L.fawLevel !== null ? new IntervalBuilder(bankDomCount(L, L.fawLevel)) : null;
  const fawHist: number[][] = fawB ? Array.from({ length: bankDomCount(L, L.fawLevel!) }, () => []) : [];
  const nFAW = rules.timings.faw?.value ?? 0;

  for (let i = 0; i < n; i++) {
    const r = rules.cmd[cmdId[i]!];
    if (!r) continue;
    if (r.action === ACTION.NONE && r.bus === 0 && r.ccd === 0 && !r.faw) continue;
    const c = Number(clk[i]);
    const lanes = targets(i);
    if (lanes.length === 0) continue;

    for (let j = 0; j < lanes.length; j++) {
      const b = lanes[j]!;
      switch (r.action) {
        case ACTION.ACT:
          advance(b, c);
          truncate(b);
          push(b, c, BANK_KIND.ACT, i);
          later(b, c + r.d1, BANK_KIND.OPEN, i);
          act1Pending[b] = 0;
          break;
        case ACTION.ACT1:
          advance(b, c);
          truncate(b);
          push(b, c, BANK_KIND.ACT, i);
          act1Pending[b] = 1;
          break;
        case ACTION.ACT2:
          advance(b, c);
          if (act1Pending[b]) {
            later(b, c + r.d1, BANK_KIND.OPEN, i);
          } else {
            truncate(b);
            push(b, c, BANK_KIND.ACT, i);
            later(b, c + r.d1, BANK_KIND.OPEN, i);
          }
          act1Pending[b] = 0;
          break;
        case ACTION.PRE: {
          advance(b, c);
          const k = currentKind(b);
          const accessing = r.bus > 0 || r.marker === BANK_MARKER.RDA || r.marker === BANK_MARKER.WRA;
          if (!accessing && k !== BANK_KIND.ACT && k !== BANK_KIND.OPEN) break;
          truncate(b);
          push(b, c, BANK_KIND.PRE, i);
          later(b, c + r.d1, BANK_KIND.IDLE, i);
          act1Pending[b] = 0;
          break;
        }
        case ACTION.WRA:
          advance(b, c);
          truncate(b);
          push(b, c, BANK_KIND.WRREC, i);
          later(b, c + r.d1, BANK_KIND.PRE, i);
          later(b, c + r.d1 + r.d2, BANK_KIND.IDLE, i);
          act1Pending[b] = 0;
          break;
        case ACTION.REF:
          advance(b, c);
          truncate(b);
          push(b, c, BANK_KIND.REF, i);
          later(b, c + r.d1, BANK_KIND.IDLE, i);
          act1Pending[b] = 0;
          break;
        default:
          break;
      }
    }

    const b0 = lanes[0]!;
    if (r.bus > 0) busB.add(bankDomOf(L, b0, L.busLevel), c, c + r.bus, i);
    if (bgB && r.ccd > 0) bgB.add(bankDomOf(L, b0, L.bgLevel!), c, c + r.ccd, i);
    if (fawB && r.faw) {
      const d = bankDomOf(L, b0, L.fawLevel!);
      const h = fawHist[d]!;
      if (h.length >= 3) fawB.add(d, c, h[h.length - 3]! + nFAW, i);
      h.push(c);
      if (h.length > 3) h.shift();
    }
  }

  // Pack per-bank spans with a trailing sentinel.
  let total = 0;
  for (let b = 0; b < NB; b++) {
    advance(b, Number.POSITIVE_INFINITY);
    total += sStart[b]!.length + 1;
  }
  const spanOffsets = new Uint32Array(NB + 1);
  const spanStart = new Float64Array(total);
  const spanKind = new Uint8Array(total);
  const spanCause = new Int32Array(total);
  let o = 0;
  for (let b = 0; b < NB; b++) {
    spanOffsets[b] = o;
    const st = sStart[b]!;
    spanStart.set(st, o);
    spanKind.set(sKind[b]!, o);
    spanCause.set(sCause[b]!, o);
    o += st.length;
    spanStart[o] = Math.max(endClk, st[st.length - 1]!);
    spanKind[o] = SENTINEL;
    spanCause[o] = -1;
    o++;
  }
  spanOffsets[NB] = o;
  let hasWrrecSpans = false;
  let hasRefSpans = false;
  for (let k = 0; k < total; k++) {
    if (spanKind[k] === BANK_KIND.WRREC) hasWrrecSpans = true;
    else if (spanKind[k] === BANK_KIND.REF) hasRefSpans = true;
  }

  return {
    layout: L,
    rules,
    numEntries: n,
    firstClk,
    endClk,
    spanOffsets,
    spanStart,
    spanKind,
    spanCause,
    bus: busB.pack(),
    bg: bgB ? bgB.pack() : null,
    faw: fawB ? fawB.pack() : null,
    hasWrrecSpans,
    hasRefSpans,
  };
}

/** Typed-array buffers of a BankStateData, for worker transfer lists. */
export function bankTransferables(bs: BankStateData): ArrayBuffer[] {
  const out: ArrayBuffer[] = [
    bs.spanOffsets.buffer as ArrayBuffer,
    bs.spanStart.buffer as ArrayBuffer,
    bs.spanKind.buffer as ArrayBuffer,
    bs.spanCause.buffer as ArrayBuffer,
  ];
  for (const s of [bs.bus, bs.bg, bs.faw]) {
    if (!s) continue;
    out.push(s.offsets.buffer as ArrayBuffer, s.start.buffer as ArrayBuffer, s.end.buffer as ArrayBuffer, s.cause.buffer as ArrayBuffer);
  }
  return out;
}

// ── Queries: interval arithmetic and window statistics ──────────────

/** First index in [lo, hi) with arr[idx] > x. */
function upperBound(arr: Float64Array, x: number, lo: number, hi: number): number {
  while (lo < hi) {
    const mid = (lo + hi) >>> 1;
    if (arr[mid]! <= x) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}

/** Index of the merged interval of domain d that contains t, or -1. */
export function bankIntervalAt(set: BankIntervalSet, d: number, t: number): number {
  const lo = set.offsets[d]!;
  const hi = set.offsets[d + 1]!;
  const j = upperBound(set.end, t, lo, hi);
  return j < hi && set.start[j]! <= t ? j : -1;
}

/** The interval of domain d containing t, or the gap around t (cause = -1). */
export function bankIntervalOrGap(set: BankIntervalSet, d: number, t: number, lo: number, hi: number): {
  inside: boolean;
  start: number;
  end: number;
  cause: number;
} {
  const o0 = set.offsets[d]!;
  const o1 = set.offsets[d + 1]!;
  const j = upperBound(set.end, t, o0, o1);
  if (j < o1 && set.start[j]! <= t) return { inside: true, start: set.start[j]!, end: set.end[j]!, cause: set.cause[j]! };
  return { inside: false, start: j > o0 ? set.end[j - 1]! : lo, end: j < o1 ? set.start[j]! : hi, cause: -1 };
}

/** "PseudoChannel 0 · Sid 1" for domain coords (levels firstLevel..). */
export function bankCoordsName(L: BankLayout, coords: number[], channelName = 'Channel'): string {
  if (coords.length === 0) return channelName;
  return coords.map((v, i) => `${L.levelNames[L.firstLevel + i]} ${v}`).join(' · ');
}

/** Length of [s, e) ∩ (union of domain d's intervals). */
export function bankOverlap(set: BankIntervalSet, d: number, s: number, e: number): number {
  if (e <= s) return 0;
  const hi = set.offsets[d + 1]!;
  let j = upperBound(set.end, s, set.offsets[d]!, hi);
  let sum = 0;
  for (; j < hi; j++) {
    const a = set.start[j]!;
    if (a >= e) break;
    const b = set.end[j]!;
    sum += Math.min(b, e) - Math.max(a, s);
  }
  return sum;
}

/** Index range [lo, hi) of domain d's intervals that intersect [t0, t1). */
export function bankIntervalRange(set: BankIntervalSet, d: number, t0: number, t1: number): [number, number] {
  const lo = upperBound(set.end, t0, set.offsets[d]!, set.offsets[d + 1]!);
  // Merged intervals are disjoint, so starts are sorted within a domain too.
  let a = lo;
  let b = set.offsets[d + 1]!;
  while (a < b) {
    const mid = (a + b) >>> 1;
    if (set.start[mid]! < t1) a = mid + 1;
    else b = mid;
  }
  return [lo, a];
}

/** Span index range [lo, hi) of bank b intersecting [t0, t1) (excludes the sentinel). */
export function bankSpanRange(bs: BankStateData, b: number, t0: number, t1: number): [number, number] {
  const lo0 = bs.spanOffsets[b]!;
  const hi0 = bs.spanOffsets[b + 1]! - 1;
  const lo = Math.max(lo0, upperBound(bs.spanStart, t0, lo0 + 1, hi0 + 1) - 1);
  let a = lo;
  let z = hi0;
  while (a < z) {
    const mid = (a + z) >>> 1;
    if (bs.spanStart[mid]! < t1) a = mid + 1;
    else z = mid;
  }
  return [lo, Math.max(lo, a)];
}

export interface BankWindowStats {
  t0: number;
  t1: number;
  banks: number;
  /** Bank-ticks per category inside the window; open = starved + bus + bg. */
  idle: number;
  act: number;
  pre: number;
  wrrec: number;
  ref: number;
  open: number;
  starved: number;
  bus: number;
  bg: number;
}

export function bankEmptyStats(t0: number, t1: number): BankWindowStats {
  return { t0, t1, banks: 0, idle: 0, act: 0, pre: 0, wrrec: 0, ref: 0, open: 0, starved: 0, bus: 0, bg: 0 };
}

/**
 * Accumulate the bank-time breakdown of `lanes` over [t0, t1) into `acc`.
 */
export function bankAccumulate(bs: BankStateData, lanes: Iterable<number>, t0: number, t1: number, acc: BankWindowStats) {
  const L = bs.layout;
  for (const b of lanes) {
    acc.banks++;
    const busD = bankDomOf(L, b, L.busLevel);
    const bgD = bs.bg && L.bgLevel !== null ? bankDomOf(L, b, L.bgLevel) : -1;
    const [lo, hi] = bankSpanRange(bs, b, t0, t1);
    for (let k = lo; k < hi; k++) {
      const s = Math.max(bs.spanStart[k]!, t0);
      const e = Math.min(bs.spanStart[k + 1]!, t1);
      if (e <= s) continue;
      const len = e - s;
      switch (bs.spanKind[k]) {
        case BANK_KIND.ACT: acc.act += len; break;
        case BANK_KIND.PRE: acc.pre += len; break;
        case BANK_KIND.WRREC: acc.wrrec += len; break;
        case BANK_KIND.REF: acc.ref += len; break;
        case BANK_KIND.OPEN: {
          acc.open += len;
          const busy = bankOverlap(bs.bus, busD, s, e);
          let bgOnly = 0;
          if (bgD >= 0) {
            const bg = bs.bg!;
            const [j0, j1] = bankIntervalRange(bg, bgD, s, e);
            for (let j = j0; j < j1; j++) {
              const x = Math.max(bg.start[j]!, s);
              const y = Math.min(bg.end[j]!, e);
              if (y > x) bgOnly += (y - x) - bankOverlap(bs.bus, busD, x, y);
            }
          }
          acc.bus += busy;
          acc.bg += bgOnly;
          acc.starved += len - busy - bgOnly;
          break;
        }
        default: acc.idle += len; break;
      }
    }
  }
  return acc;
}

/** Fraction of [t0, t1) covered by domain d of an interval set (bus utilisation / tFAW-blocked). */
export function bankCoverage(set: BankIntervalSet | null, d: number, t0: number, t1: number): number {
  if (!set || t1 <= t0) return 0;
  return bankOverlap(set, d, t0, t1) / (t1 - t0);
}

export interface BankSpanHit {
  kind: number;
  start: number;
  end: number;
  cause: number;
  /** For open spans: why a READ cannot issue right now. */
  sub?: 'bus' | 'bg' | 'starved';
  subStart?: number;
  subEnd?: number;
  subCause?: number;
}

/** State of bank `lane` at time t (hover). */
export function bankStateAt(bs: BankStateData, lane: number, t: number): BankSpanHit | null {
  const lo0 = bs.spanOffsets[lane]!;
  const hi0 = bs.spanOffsets[lane + 1]! - 1;
  if (hi0 <= lo0) return null;
  const k = upperBound(bs.spanStart, t, lo0 + 1, hi0 + 1) - 1;
  if (k < lo0 || k >= hi0) return null;
  const hit: BankSpanHit = { kind: bs.spanKind[k]!, start: bs.spanStart[k]!, end: bs.spanStart[k + 1]!, cause: bs.spanCause[k]! };
  if (hit.kind !== BANK_KIND.OPEN || t >= hit.end) return hit;
  const L = bs.layout;
  hit.sub = 'starved';
  const sets: ['bus' | 'bg', BankIntervalSet | null, number | null][] = [
    ['bus', bs.bus, L.busLevel],
    ['bg', bs.bg, L.bgLevel],
  ];
  for (const [sub, set, level] of sets) {
    if (!set || level === null) continue;
    const j = bankIntervalAt(set, bankDomOf(L, lane, level), t);
    if (j < 0) continue;
    hit.sub = sub;
    hit.subStart = set.start[j]!;
    hit.subEnd = set.end[j]!;
    hit.subCause = set.cause[j]!;
    break;
  }
  return hit;
}
