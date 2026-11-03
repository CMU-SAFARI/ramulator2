// Requests from the command stream: commands are grouped by request id (format 1.2), or by
// (arrival, lane) in older traces; a request completes with its column command. Completed
// requests take the first row of their lane that is free since their arrival.
import type { Chunk } from './chunk';

export interface OpenRequest {
  reqNo: number;
  /** Request id in the trace (format 1.2), -1 without one. */
  id: number;
  arrive: number;
  /** Clock of its first command. */
  first: number;
  last: number;
  lane: number;
  type: number;
  src: number;
  addr: number[];
  cmds: number[];
}

export interface RequestMachineState {
  nextReqNo: number;
  byId: [number, OpenRequest][];
  byKey: [number, OpenRequest][];
  /** Per lane: departure of the last request in each row. */
  rowEnds: number[][];
  maxRows: number[];
}

export interface CompletedRequest extends OpenRequest {
  depart: number;
  row: number;
  inFlight: boolean;
}

export class RequestMachine {
  nextReqNo: number;
  byId: Map<number, OpenRequest>;
  byKey: Map<number, OpenRequest>;
  rowEnds: number[][];
  maxRows: number[];

  constructor(lanes: number, private totalLanes: number, state?: RequestMachineState) {
    this.nextReqNo = state?.nextReqNo ?? 0;
    this.byId = new Map(state?.byId.map(([k, r]) => [k, cloneReq(r)]));
    this.byKey = new Map(state?.byKey.map(([k, r]) => [k, cloneReq(r)]));
    this.rowEnds = state ? state.rowEnds.map(a => [...a]) : Array.from({ length: lanes }, () => []);
    this.maxRows = state ? [...state.maxRows] : new Array(lanes).fill(1);
  }

  snapshot(): RequestMachineState {
    return {
      nextReqNo: this.nextReqNo,
      byId: [...this.byId].map(([k, r]) => [k, cloneReq(r)]),
      byKey: [...this.byKey].map(([k, r]) => [k, cloneReq(r)]),
      rowEnds: this.rowEnds.map(a => [...a]),
      maxRows: [...this.maxRows],
    };
  }

  /** Request number of the last command passed to `command`. */
  lastReqNo = -1;

  /**
   * Command i of chunk c belongs to a request (arrive ≥ 0). Returns the request when this is its
   * column command, else null; lastReqNo holds its number either way.
   */
  command(c: Chunk, i: number, arrive: number, clk: number, lane: number, gidx: number, accessing: boolean, readLatency: number): CompletedRequest | null {
    const id = c.reqId ? c.reqId[i]! : -1;
    const useId = id >= 0;
    const map = useId ? this.byId : this.byKey;
    // Key arrive * totalLanes + lane: injective because lane < totalLanes, exact in doubles for
    // arrive < 2^37 with totalLanes < 2^16.
    const key = useId ? id : arrive * this.totalLanes + lane;
    let r = map.get(key);
    if (!r) {
      const addr = new Array<number>(c.addr.length);
      for (let k = 0; k < addr.length; k++) addr[k] = c.addr[k]![i]!;
      r = { reqNo: this.nextReqNo++, id, arrive, first: clk, last: clk, lane, type: Math.max(0, c.type[i]!), src: c.src[i]!, addr, cmds: [] };
      map.set(key, r);
    }
    r.cmds.push(gidx);
    r.last = clk;
    this.lastReqNo = r.reqNo;
    if (!accessing) return null;
    map.delete(key);
    return this.place(r, clk + (r.type === 0 ? readLatency : 1), false);
  }

  /** First-fit row from the request's arrival; `rows` defaults to the live row state. */
  place(r: OpenRequest, depart: number, inFlight: boolean, rowEnds = this.rowEnds, maxRows = this.maxRows): CompletedRequest {
    const ends = rowEnds[r.lane]!;
    let row = ends.findIndex(e => e <= r.arrive);
    if (row < 0) {
      row = ends.length;
      ends.push(depart);
    } else {
      ends[row] = depart;
    }
    if (row + 1 > maxRows[r.lane]!) maxRows[r.lane] = row + 1;
    // The request has left the open set, so it becomes the completed record.
    const done = r as CompletedRequest;
    done.depart = depart;
    done.row = row;
    done.inFlight = inFlight;
    return done;
  }

  /** Requests still open, oldest first. */
  open(): OpenRequest[] {
    return [...this.byId.values(), ...this.byKey.values()].sort((a, b) => a.reqNo - b.reqNo);
  }

  /** Close every open request at the end of the trace. */
  finish(traceEnd: number, done: (r: CompletedRequest) => void) {
    for (const r of this.open()) {
      const unfinished = r.type <= 1;
      done(this.place(r, unfinished ? Math.max(traceEnd, r.last + 1) : r.last + 1, unfinished));
    }
    this.byId.clear();
    this.byKey.clear();
  }
}

function cloneReq(r: OpenRequest): OpenRequest {
  return { ...r, addr: [...r.addr], cmds: [...r.cmds] };
}
