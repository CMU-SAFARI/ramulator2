/**
 * Browser side of live streaming.
 *
 * The server lists the live sessions (one per simulation run and channel) over a WebSocket.
 * Opening one subscribes to it: the server sends its init, the events it kept so far, then new
 * events and state changes. Event batches go to the trace engine's worker as they arrive, still as
 * text, so the page never parses them.
 */
import type { TraceHeader, TraceSpec, CommandMeta } from '~/composables/useTrace';
import type { TraceEngine } from '~/engine/client';
import { useSessionStore } from '~/stores/session';

/** idle: no session open; gone: the server no longer has the session. */
export type StreamStatus = 'idle' | 'connecting' | 'live' | 'done' | 'stopped' | 'lost' | 'gone';

export interface LiveSessionInfo {
  id: string;
  run: string;
  channel: number;
  startedAt: number;
  dramType: string;
  state: 'live' | 'done' | 'stopped' | 'lost';
  events: number;
  /** Commands the server no longer keeps for viewers that open the session now. */
  dropped: number;
}

interface StreamHeader {
  version: [number, number];
  levelCount: number;
  commandCount: number;
  timingCount: number;
  channelWidth: number;
  prefetchSize: number;
  dq: number;
  channelId: number;
  readLatency: number;
  dramType: string;
}

interface StreamSpec {
  levelNames: string[];
  levelSizes: number[];
  commandNames: string[];
  commandMeta: CommandMeta[];
  commandCycles?: number[];
  timingNames: string[];
  timingValues: number[];
}

// ── WebSocket state ───────────────────────────────────────────────────────────

let _ws: WebSocket | null = null;
/** The session this page shows, or null. */
let _subscribed: string | null = null;
let _engine: TraceEngine | null = null;
let _count = 0;
let _opened: { resolve: () => void; reject: (e: Error) => void } | null = null;
const RECONNECT_MS = 2000;
const EVENTS_PREFIX = '{"type":"events","session":';

export function useStreamSession() {
  const status = useState<StreamStatus>('stream-status', () => 'idle');
  const eventCount = useState<number>('stream-event-count', () => 0);
  const connected = useState<boolean>('stream-connected', () => false);
  const sessions = useState<LiveSessionInfo[]>('stream-sessions', () => []);
  const sessionId = useState<string | null>('stream-session-id', () => null);
  /** Events the server did not keep for this late subscriber (its replay budget was exceeded). */
  const dropped = useState<number>('stream-dropped', () => 0);
  /** Why the live view stopped growing (the browser ran out of room for it), or null. */
  const full = useState<string | null>('stream-full', () => null);
  const isLive = computed(() => status.value === 'live');
  const session = computed(() => sessions.value.find(s => s.id === sessionId.value) ?? null);

  function send(message: object) {
    if (_ws?.readyState === WebSocket.OPEN) _ws.send(JSON.stringify(message));
  }

  function startSession(header: StreamHeader, spec: StreamSpec) {
    const sessionStore = useSessionStore();
    const traceHeader: TraceHeader = { ...header, numEntries: 0, dataOffset: 0 };
    const traceSpec: TraceSpec = {
      levelNames: spec.levelNames,
      levelSizes: new Uint32Array(spec.levelSizes),
      commandNames: spec.commandNames,
      commandMeta: spec.commandMeta,
      commandCycles: new Uint8Array(spec.commandCycles ?? Array(spec.commandNames.length).fill(1)),
      timingNames: spec.timingNames,
      timingValues: new Int32Array(spec.timingValues),
    };
    sessionStore.traceName = 'Live session';
    // The server sends the session's state after the commands it kept; a finished session should
    // not look live while those load.
    const listed = sessions.value.find(s => s.id === _subscribed)?.state;
    const engine = sessionStore.loadTrace({ source: { kind: 'live' }, header: traceHeader, spec: traceSpec });
    _engine = engine;
    _count = 0;
    eventCount.value = 0;
    dropped.value = 0;
    full.value = null;
    status.value = listed ?? 'live';
    // Live data goes to the browser's file storage; without it, or once it is full, the view stops.
    engine.onChange(() => {
      const f = engine.liveFull;
      if (!f || full.value || _engine !== engine) return;
      const at = `view full at ${f.entries.toLocaleString('en-US')} commands`;
      full.value = f.reason === 'memory' ? `${at}: this browser cannot keep live data on disk` : `${at}: the browser's storage is full`;
    });
    _opened?.resolve();
    _opened = null;
  }

  /** A batch of events, forwarded without parsing: {"type":"events","session":…,"columns":{"n":…,…}} */
  function onEvents(text: string) {
    const at = text.indexOf(',"columns":');
    if (at < 0 || !_engine || _engine.liveFull) return;
    let id: unknown;
    try {
      id = JSON.parse(text.slice(EVENTS_PREFIX.length, at));
    } catch {
      return;
    }
    if (id !== _subscribed) return;
    const columns = text.slice(at + ',"columns":'.length, -1);
    const n = Number(/^\{"n":(\d+)/.exec(columns)?.[1] ?? 0);
    _engine.appendLive(columns);
    _count += n;
    eventCount.value = _count;
  }

  function onMessage(data: any) {
    switch (data?.type) {
      case 'sessions':
        if (Array.isArray(data.sessions)) sessions.value = data.sessions;
        break;
      case 'init':
        if (data.session === _subscribed) startSession(data.header, data.spec);
        break;
      case 'replay':
        if (data.session === _subscribed) dropped.value = Number(data.dropped) || 0;
        break;
      case 'state':
        if (data.session !== _subscribed) break;
        status.value = data.state;
        if (data.state === 'gone') {
          _opened?.reject(new Error('This live session has ended and is no longer kept by the visualizer.'));
          _opened = null;
        } else if (data.state === 'done' || data.state === 'stopped') {
          _engine?.endLive();
        }
        break;
    }
  }

  function connect() {
    if (!import.meta.client) return;
    if (_ws && _ws.readyState <= WebSocket.OPEN) return;
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const ws = new WebSocket(`${protocol}//${window.location.host}/api/live-trace`);
    _ws = ws;

    ws.onopen = () => {
      if (_ws !== ws) return;
      connected.value = true;
      if (_subscribed) send({ type: 'subscribe', session: _subscribed });
    };
    ws.onclose = () => {
      if (_ws !== ws) return;
      _ws = null;
      connected.value = false;
      setTimeout(() => { if (!_ws) connect(); }, RECONNECT_MS);
    };
    ws.onmessage = (event) => {
      if (_ws !== ws || typeof event.data !== 'string') return;
      if (event.data.startsWith(EVENTS_PREFIX)) {
        onEvents(event.data);
        return;
      }
      try {
        onMessage(JSON.parse(event.data));
      } catch {
        // Ignore malformed messages.
      }
    };
  }

  /** Shows a live session; resolves once its trace is loaded into the session store. */
  function open(id: string): Promise<void> {
    leave();
    _subscribed = id;
    sessionId.value = id;
    status.value = 'connecting';
    connect();
    send({ type: 'subscribe', session: id });
    return new Promise((resolve, reject) => { _opened = { resolve, reject }; });
  }

  /** Stops showing the live session (another trace is opened); its stream continues for others. */
  function leave() {
    if (_subscribed) send({ type: 'unsubscribe' });
    _subscribed = null;
    _engine = null;
    sessionId.value = null;
    _opened?.reject(new Error('Opened another trace.'));
    _opened = null;
    status.value = 'idle';
    eventCount.value = 0;
    dropped.value = 0;
    full.value = null;
  }

  /** Stops streaming the shown session's run; the simulation itself continues. */
  function stop() {
    if (_subscribed) send({ type: 'stop', session: _subscribed });
  }

  return { status, eventCount, connected, sessions, sessionId, session, dropped, full, isLive, connect, open, leave, stop };
}
