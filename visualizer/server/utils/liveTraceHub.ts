/**
 * Shared state for the live-trace streaming bridge.
 *
 * Holds the set of connected WebSocket peers (browsers) and the current
 * session metadata.  Both the WS handler and the HTTP POST handler use
 * these functions to coordinate.
 */

interface WsPeer {
  send(data: string): void;
}

interface SessionInfo {
  header: unknown;
  spec: unknown;
  done: boolean;
  interrupted: boolean;
}

const _peers = new Set<WsPeer>();
let _session: SessionInfo | null = null;

export function liveTraceAddPeer(peer: WsPeer) {
  _peers.add(peer);
}

export function liveTraceRemovePeer(peer: WsPeer) {
  _peers.delete(peer);
  if (_peers.size === 0 && _session?.done) {
    liveTraceClearSession();
  }
}

export function liveTraceGetSession(): SessionInfo | null {
  return _session;
}

export function liveTraceSetSession(header: unknown, spec: unknown) {
  _session = { header, spec, done: false, interrupted: false };
}

export function liveTraceClearSession() {
  _session = null;
}

export function liveTraceMarkDone() {
  if (_session) _session.done = true;
}

export function liveTraceMarkInterrupted() {
  if (_session) _session.interrupted = true;
}

export function liveTraceBroadcast(text: string, exclude?: WsPeer) {
  for (const p of _peers) {
    if (p !== exclude) p.send(text);
  }
}
