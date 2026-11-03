/**
 * Live-trace sessions: one per (simulation run, channel).
 *
 * The simulator (LiveTraceStreamer) posts init, events, status and done for its session.  Browsers
 * connect over WebSocket, get the session list, and subscribe to one session: they receive its
 * init, the events kept so far, then new events and state changes as they happen.  Kept events
 * share one memory budget (RAMULATOR_VISUALIZER_REPLAY_MB, default 256); the oldest events of
 * finished sessions go first.
 */

export type LiveState = 'live' | 'done' | 'stopped' | 'lost';

export interface HubPeer {
  send(data: string): unknown;
  close(code?: number, reason?: string): void;
  readonly websocket: { bufferedAmount?: number; readyState?: number };
}

interface LiveSession {
  id: string;
  run: string;
  channel: number;
  startedAt: number;
  dramType: string;
  init: string;
  batches: string[];
  batchSizes: number[];
  dropped: number;
  events: number;
  state: LiveState;
  lastSeen: number;
  subscribers: Set<HubPeer>;
}

/** A session that has not posted for this long has lost contact (the simulator crashed or was stopped). */
const LOST_AFTER_MS = 15_000;
const MAX_SESSIONS = 32;
/** Messages wait while this much is unsent on a browser's socket. */
const HIGH_WATER_BYTES = 8 * 2 ** 20;

/** Messages waiting for a browser's socket to drain: text, or an events prefix and its columns. */
interface Outbox {
  items: (string | [string, string])[];
  bytes: number;
  timer: ReturnType<typeof setTimeout> | null;
}

const sessions = new Map<string, LiveSession>();
const peers = new Set<HubPeer>();
const outboxes = new Map<HubPeer, Outbox>();
let keptBytes = 0;
let listDirty = false;
let timer: ReturnType<typeof setInterval> | null = null;

function replayBudget(): number {
  return (Number(nodeProcess.env.RAMULATOR_VISUALIZER_REPLAY_MB) || 256) * 2 ** 20;
}

function pump(peer: HubPeer, box: Outbox) {
  if ((peer.websocket.readyState ?? 1) > 1) {
    dropOutbox(peer); // closing or closed
    return;
  }
  while (box.items.length && (peer.websocket.bufferedAmount ?? 0) < HIGH_WATER_BYTES) {
    const item = box.items.shift()!;
    const text = typeof item === 'string' ? item : `${item[0]}${item[1]}}`;
    box.bytes -= typeof item === 'string' ? item.length : item[1].length;
    peer.send(text);
  }
  if (box.items.length && !box.timer) {
    box.timer = setTimeout(() => {
      box.timer = null;
      pump(peer, box);
    }, 20);
  }
}

/**
 * Queues a message for a browser.  A browser so far behind that it would hold more than the
 * replay budget is disconnected; it reconnects and replays.
 */
function sendTo(peer: HubPeer, item: string | [string, string]) {
  let box = outboxes.get(peer);
  if (!box) outboxes.set(peer, box = { items: [], bytes: 0, timer: null });
  box.items.push(item);
  box.bytes += typeof item === 'string' ? item.length : item[1].length;
  if (box.bytes > replayBudget() + 64 * 2 ** 20) {
    dropOutbox(peer);
    peer.close(4000, 'Too far behind; reconnecting');
    return;
  }
  pump(peer, box);
}

function dropOutbox(peer: HubPeer) {
  const box = outboxes.get(peer);
  if (box?.timer) clearTimeout(box.timer);
  outboxes.delete(peer);
}

export function liveSessionList() {
  return [...sessions.values()].map(s => ({
    id: s.id, run: s.run, channel: s.channel, startedAt: s.startedAt, dramType: s.dramType, state: s.state, events: s.events,
    dropped: s.dropped,
  }));
}

function broadcastList() {
  listDirty = false;
  const text = JSON.stringify({ type: 'sessions', sessions: liveSessionList() });
  for (const p of peers) sendTo(p, text);
}

function setState(s: LiveSession, state: LiveState) {
  if (s.state === state) return;
  s.state = state;
  const text = JSON.stringify({ type: 'state', session: s.id, state });
  for (const p of s.subscribers) sendTo(p, text);
  broadcastList();
}

/** Marks silent sessions as lost and sends event counts to the session list once a second. */
function ensureTimer() {
  timer ??= setInterval(() => {
    const now = Date.now();
    for (const s of sessions.values()) {
      if (s.state === 'live' && now - s.lastSeen > LOST_AFTER_MS) setState(s, 'lost');
    }
    if (listDirty) broadcastList();
  }, 1000);
}

function dropBatch(s: LiveSession) {
  keptBytes -= s.batches.shift()!.length;
  s.dropped += s.batchSizes.shift()!;
}

/** Frees kept events until `bytes` more fit: finished sessions first, oldest first. */
function makeRoom(bytes: number) {
  const budget = replayBudget();
  while (keptBytes + bytes > budget) {
    let victim: LiveSession | null = null;
    for (const s of sessions.values()) {
      if (s.batches.length === 0) continue;
      const rank = (x: LiveSession) => (x.state === 'live' ? 1 : 0);
      if (!victim || rank(s) < rank(victim) || (rank(s) === rank(victim) && s.startedAt < victim.startedAt)) victim = s;
    }
    if (!victim) return false;
    dropBatch(victim);
  }
  return true;
}

function removeSession(s: LiveSession) {
  while (s.batches.length) dropBatch(s);
  sessions.delete(s.id);
}

export function liveSessionStart(init: { id: string; run: string; channel: number; startedAt: number; header: any; spec: any }) {
  const old = sessions.get(init.id);
  const subscribers = old?.subscribers ?? new Set<HubPeer>();
  if (old) removeSession(old);
  const s: LiveSession = {
    id: init.id,
    run: init.run,
    channel: init.channel,
    startedAt: init.startedAt,
    dramType: String(init.header.dramType ?? ''),
    init: JSON.stringify({ type: 'init', session: init.id, header: init.header, spec: init.spec }),
    batches: [],
    batchSizes: [],
    dropped: 0,
    events: 0,
    state: 'live',
    lastSeen: Date.now(),
    subscribers,
  };
  sessions.set(s.id, s);
  while (sessions.size > MAX_SESSIONS) {
    const oldest = [...sessions.values()].filter(x => x.state !== 'live').sort((a, b) => a.startedAt - b.startedAt)[0];
    if (!oldest) break;
    removeSession(oldest);
  }
  for (const p of subscribers) sendTo(p, s.init);
  ensureTimer();
  broadcastList();
}

/** Result of a simulator post: whether a viewer stopped the stream, or the session is unknown. */
export type PushResult = { ok: true; interrupted: boolean } | { ok: false; unknown: true };

function touch(id: string): LiveSession | null {
  const s = sessions.get(id);
  if (!s) return null;
  s.lastSeen = Date.now();
  if (s.state === 'lost') setState(s, 'live');
  return s;
}

/** A batch of events as the simulator encoded it (a JSON object of columns; see useStreamSession). */
export function liveSessionEvents(id: string, columns: string, count: number): PushResult {
  const s = touch(id);
  if (!s) return { ok: false, unknown: true };
  if (s.state !== 'live') return { ok: true, interrupted: s.state === 'stopped' };
  s.events += count;
  if (makeRoom(columns.length)) {
    s.batches.push(columns);
    s.batchSizes.push(count);
    keptBytes += columns.length;
  } else {
    s.dropped += count;
  }
  const prefix = `{"type":"events","session":${JSON.stringify(id)},"columns":`;
  for (const p of s.subscribers) sendTo(p, [prefix, columns]);
  listDirty = true;
  return { ok: true, interrupted: false };
}

export function liveSessionStatus(id: string): PushResult {
  const s = touch(id);
  return s ? { ok: true, interrupted: s.state === 'stopped' } : { ok: false, unknown: true };
}

export function liveSessionDone(id: string) {
  const s = touch(id);
  if (s && s.state === 'live') setState(s, 'done');
}

/** A viewer stopped streaming: every channel of the run stops; the simulation itself continues. */
export function liveSessionStop(id: string) {
  const run = sessions.get(id)?.run;
  for (const s of sessions.values()) {
    if (s.run === run && (s.state === 'live' || s.state === 'lost')) setState(s, 'stopped');
  }
}

export function liveSessionSubscribe(peer: HubPeer, id: string) {
  liveSessionUnsubscribe(peer);
  const s = sessions.get(id);
  if (!s) {
    sendTo(peer, JSON.stringify({ type: 'state', session: id, state: 'gone' }));
    return;
  }
  s.subscribers.add(peer);
  sendTo(peer, s.init);
  if (s.dropped > 0) sendTo(peer, JSON.stringify({ type: 'replay', session: id, dropped: s.dropped }));
  const prefix = `{"type":"events","session":${JSON.stringify(id)},"columns":`;
  for (const b of s.batches) sendTo(peer, [prefix, b]);
  if (s.state !== 'live') sendTo(peer, JSON.stringify({ type: 'state', session: id, state: s.state }));
}

export function liveSessionUnsubscribe(peer: HubPeer) {
  for (const s of sessions.values()) s.subscribers.delete(peer);
  dropOutbox(peer);
}

export function liveHubAddPeer(peer: HubPeer) {
  peers.add(peer);
  sendTo(peer, JSON.stringify({ type: 'sessions', sessions: liveSessionList() }));
}

export function liveHubRemovePeer(peer: HubPeer) {
  peers.delete(peer);
  liveSessionUnsubscribe(peer);
}
