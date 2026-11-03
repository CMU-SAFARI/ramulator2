/**
 * Bank State view: per-bank state reconstructed from the issued-command stream.
 *
 * Covers the JEDEC standards, including write recovery (WRA), refresh/RFM, wildcard (all-bank /
 * same-bank) and paired commands and per-standard timing names. The shading is a reconstruction
 * from the command stream plus the trace's timing table, not simulator state.
 *
 * The reconstruction itself runs in the trace engine (app/engine/bank.ts). Only type imports:
 * this module also runs in the engine's worker and under plain Node (scripts/verify-bank-state.ts).
 */
import type { TraceSpec } from '~/composables/useTrace';

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

/** Commands of processing-using-memory standards (DDR4_PUM), whose bank behaviour is not modelled. */
const PUM_COMMANDS = new Set(['AAP', 'AP', 'GB_MOV', 'LC_MOV', 'PMAJ3', 'PNOT']);

/** Why Bank State cannot show this trace, or null. It models the JEDEC standards only. */
export function bankUnsupportedReason(spec: TraceSpec): string | null {
  if (spec.levelNames.includes('Subarray') || spec.commandNames.some(n => PUM_COMMANDS.has(n)))
    return 'Bank State supports the JEDEC standards only. This trace uses processing-using-memory commands.';
  return null;
}

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
  LPDDR5: ['ACT2', 'REFpb'],
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
  /** Per command id: the partner bank of a paired command (addr[level] ^ mask), or null. */
  pairs: (BankPairRule | null)[];
  /** Human-readable notes on omitted or approximated states. */
  notes: string[];
}

export interface BankPairRule {
  level: number;
  mask: number;
}

/**
 * Paired commands of each standard: LPDDR5 REFpb refreshes banks n and n+8 together in BG and
 * 16B mode (JESD209-5C §7.5.1); the bank mode follows from the BankGroup / Bank sizes.
 */
function resolvePairs(spec: TraceSpec, family: BankFamily): (BankPairRule | null)[] {
  const pairs: (BankPairRule | null)[] = spec.commandNames.map(() => null);
  if (family !== 'LPDDR5') return pairs;
  const bgLevel = spec.levelNames.indexOf('BankGroup');
  const bankLevel = spec.levelNames.indexOf('Bank');
  const bgs = bgLevel >= 0 ? spec.levelSizes[bgLevel]! : 1;
  const banks = bankLevel >= 0 ? spec.levelSizes[bankLevel]! : 1;
  const rule = bgs === 4 && banks === 4 ? { level: bgLevel, mask: 2 }
    : bgs === 1 && banks === 16 ? { level: bankLevel, mask: 8 }
      : null;
  const ref = spec.commandNames.indexOf('REFpb');
  if (rule && ref >= 0) pairs[ref] = rule;
  return pairs;
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
    pairs: resolvePairs(spec, family),
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
 * as DRAMDevice::bank_matches), which covers all-bank and same-bank commands; `pairs` adds the
 * partner bank of paired commands. The returned array is reused for single-bank and paired
 * commands; copy it before the next call if it must be kept.
 */
export function bankTargetResolver(
  L: BankLayout,
  addr: ArrayLike<Int32Array>,
  cmdId?: Uint8Array,
  pairs?: (BankPairRule | null)[],
): (i: number) => Int32Array {
  const fanCache = new Map<number, Int32Array>();
  const single = new Int32Array(1);
  const pair = new Int32Array(2);
  return (i: number): Int32Array => {
    let lane = 0;
    let wild = false;
    for (let k = L.firstLevel; k <= L.bankLevel; k++) {
      const a = addr[k]![i]!;
      if (a < 0) wild = true;
      else lane += a * L.strides[k]!;
    }
    if (!wild) {
      const p = cmdId && pairs ? pairs[cmdId[i]!] : null;
      const a = p ? addr[p.level]![i]! : -1;
      if (p && a >= 0 && p.level >= L.firstLevel && p.level <= L.bankLevel && (a ^ p.mask) < L.levelSizes[p.level]!) {
        pair[0] = lane;
        pair[1] = lane + ((a ^ p.mask) - a) * L.strides[p.level]!;
        return pair;
      }
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

/** "PseudoChannel 0 · Sid 1" for domain coords (levels firstLevel..). */
export function bankCoordsName(L: BankLayout, coords: number[], channelName = 'Channel'): string {
  if (coords.length === 0) return channelName;
  return coords.map((v, i) => `${L.levelNames[L.firstLevel + i]} ${v}`).join(' · ');
}
