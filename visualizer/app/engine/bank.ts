// Streaming form of the Bank State reconstruction (see utils/bankState.ts for the rules): per-bank
// state machines fed one command at a time, with state small enough to checkpoint.
//
// A command reaching a bank at clk c truncates whatever span is still pending at c and never
// extends one. Precharges (PRE*) only affect banks that are activating or open; a PREab reaching
// an already-closed bank is a no-op.
import { BANK_KIND, BANK_MARKER, type BankCmdRule } from '../utils/bankState';

const ACTION = { NONE: 0, ACT: 1, ACT1: 2, ACT2: 3, PRE: 4, WRA: 5, REF: 6 } as const;

export interface BankMachineState {
  curStart: number[];
  curKind: number[];
  curCause: number[];
  pendT: number[][];
  pendK: number[][];
  pendC: number[][];
  act1: number[];
}

/** A span [start, end) of `kind` ended; `cause` = trace index of the command that began it (-1 none). */
export type SpanSink = (bank: number, start: number, end: number, kind: number, cause: number) => void;

export class BankMachine {
  s: BankMachineState;

  constructor(banks: number, origin: number, private onSpan: SpanSink, state?: BankMachineState) {
    this.s = state ? BankMachine.copy(state) : {
      curStart: new Array(banks).fill(origin),
      curKind: new Array(banks).fill(BANK_KIND.IDLE),
      curCause: new Array(banks).fill(-1),
      pendT: Array.from({ length: banks }, () => []),
      pendK: Array.from({ length: banks }, () => []),
      pendC: Array.from({ length: banks }, () => []),
      act1: new Array(banks).fill(0),
    };
  }

  snapshot(): BankMachineState {
    return BankMachine.copy(this.s);
  }

  private static copy(s: BankMachineState): BankMachineState {
    return {
      curStart: [...s.curStart],
      curKind: [...s.curKind],
      curCause: [...s.curCause],
      pendT: s.pendT.map(a => [...a]),
      pendK: s.pendK.map(a => [...a]),
      pendC: s.pendC.map(a => [...a]),
      act1: [...s.act1],
    };
  }

  private push(b: number, t: number, k: number, cause: number) {
    const s = this.s;
    if (t <= s.curStart[b]!) {
      // Zero-length predecessor: replace it.
      s.curKind[b] = k;
      s.curCause[b] = cause;
      return;
    }
    this.onSpan(b, s.curStart[b]!, t, s.curKind[b]!, s.curCause[b]!);
    s.curStart[b] = t;
    s.curKind[b] = k;
    s.curCause[b] = cause;
  }

  /** Commit pending boundaries at or before c. */
  advance(b: number, c: number) {
    const tt = this.s.pendT[b]!;
    if (tt.length === 0) return;
    const kk = this.s.pendK[b]!;
    const cc = this.s.pendC[b]!;
    let i = 0;
    while (i < tt.length && tt[i]! <= c) {
      this.push(b, tt[i]!, kk[i]!, cc[i]!);
      i++;
    }
    if (i > 0) {
      tt.splice(0, i);
      kk.splice(0, i);
      cc.splice(0, i);
    }
  }

  /** Commit every pending boundary before t (all banks). */
  advanceAllBefore(t: number) {
    for (let b = 0; b < this.s.curStart.length; b++) {
      const tt = this.s.pendT[b]!;
      if (tt.length && tt[0]! < t) this.advance(b, t - 0.5);
    }
  }

  /** Whether any bank has a pending boundary before t. */
  pendingBefore(t: number): boolean {
    for (const tt of this.s.pendT) if (tt.length && tt[0]! < t) return true;
    return false;
  }

  private truncate(b: number) {
    this.s.pendT[b]!.length = 0;
    this.s.pendK[b]!.length = 0;
    this.s.pendC[b]!.length = 0;
  }

  private later(b: number, t: number, k: number, cause: number) {
    this.s.pendT[b]!.push(t);
    this.s.pendK[b]!.push(k);
    this.s.pendC[b]!.push(cause);
  }

  /** Command `cause` with rule r reaches bank b at clk c. */
  apply(r: BankCmdRule, b: number, c: number, cause: number) {
    const s = this.s;
    switch (r.action) {
      case ACTION.ACT:
        this.advance(b, c);
        this.truncate(b);
        this.push(b, c, BANK_KIND.ACT, cause);
        this.later(b, c + r.d1, BANK_KIND.OPEN, cause);
        s.act1[b] = 0;
        break;
      case ACTION.ACT1:
        this.advance(b, c);
        this.truncate(b);
        this.push(b, c, BANK_KIND.ACT, cause);
        s.act1[b] = 1;
        break;
      case ACTION.ACT2:
        this.advance(b, c);
        if (s.act1[b]) {
          this.later(b, c + r.d1, BANK_KIND.OPEN, cause);
        } else {
          this.truncate(b);
          this.push(b, c, BANK_KIND.ACT, cause);
          this.later(b, c + r.d1, BANK_KIND.OPEN, cause);
        }
        s.act1[b] = 0;
        break;
      case ACTION.PRE: {
        this.advance(b, c);
        const k = s.curKind[b]!;
        const accessing = r.bus > 0 || r.marker === BANK_MARKER.RDA || r.marker === BANK_MARKER.WRA;
        if (!accessing && k !== BANK_KIND.ACT && k !== BANK_KIND.OPEN) break;
        this.truncate(b);
        this.push(b, c, BANK_KIND.PRE, cause);
        this.later(b, c + r.d1, BANK_KIND.IDLE, cause);
        s.act1[b] = 0;
        break;
      }
      case ACTION.WRA:
        this.advance(b, c);
        this.truncate(b);
        this.push(b, c, BANK_KIND.WRREC, cause);
        this.later(b, c + r.d1, BANK_KIND.PRE, cause);
        this.later(b, c + r.d1 + r.d2, BANK_KIND.IDLE, cause);
        s.act1[b] = 0;
        break;
      case ACTION.REF:
        this.advance(b, c);
        this.truncate(b);
        this.push(b, c, BANK_KIND.REF, cause);
        this.later(b, c + r.d1, BANK_KIND.IDLE, cause);
        s.act1[b] = 0;
        break;
      default:
        // A column access proves the row is open, e.g. a write after the shorter nRCDWR.
        if (r.bus > 0) {
          this.advance(b, c);
          if (s.curKind[b] === BANK_KIND.ACT && !s.act1[b]) {
            const cs = s.curCause[b]!;
            this.truncate(b);
            this.push(b, c, BANK_KIND.OPEN, cs);
          }
        }
        break;
    }
  }
}

export interface OpenInterval {
  s: number;
  e: number;
  cause: number;
}

export interface IntervalStreamState {
  open: (OpenInterval | null)[];
}

/** Final merged interval of a domain; it will not grow any more. */
export type IntervalSink = (dom: number, s: number, e: number, cause: number) => void;

/** Online interval merge per domain: starts arrive sorted; touching intervals coalesce. */
export class IntervalStream {
  s: IntervalStreamState;

  constructor(domains: number, private onFinal: IntervalSink, state?: IntervalStreamState) {
    this.s = state ? { open: state.open.map(o => (o ? { ...o } : null)) } : { open: new Array(domains).fill(null) };
  }

  snapshot(): IntervalStreamState {
    return { open: this.s.open.map(o => (o ? { ...o } : null)) };
  }

  add(d: number, s: number, e: number, cause: number) {
    if (e <= s) return;
    const o = this.s.open[d];
    if (o && s <= o.e) {
      if (e > o.e) o.e = e;
      return;
    }
    if (o) this.onFinal(d, o.s, o.e, o.cause);
    this.s.open[d] = { s, e, cause };
  }

  /** Close intervals that no later start (≥ t) can extend. */
  closeBefore(t: number) {
    const open = this.s.open;
    for (let d = 0; d < open.length; d++) {
      const o = open[d];
      if (o && o.e < t) {
        this.onFinal(d, o.s, o.e, o.cause);
        open[d] = null;
      }
    }
  }
}
