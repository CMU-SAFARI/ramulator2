/**
 * WebSocket endpoint for browsers.  The server sends the session list (`sessions`) on connect and
 * whenever it changes.  A browser sends:
 *
 *   { type: 'subscribe', session }   receive that session's init, kept events, then new events
 *   { type: 'unsubscribe' }
 *   { type: 'stop', session }        stop streaming that session's run (the simulation continues)
 *
 * Trace data only comes from the simulator, through /api/live-trace-push.
 */
import type { HubPeer } from '../utils/liveTraceHub';

export default defineWebSocketHandler({
  upgrade(request) {
    const denied = accessDenied(request.headers);
    if (denied) return new Response(denied.reason, { status: denied.status });
  },

  open(peer) {
    liveHubAddPeer(peer as unknown as HubPeer);
  },

  message(peer, msg) {
    let data: any;
    try {
      data = JSON.parse(msg.text());
    } catch {
      return;
    }
    const hubPeer = peer as unknown as HubPeer;
    switch (data?.type) {
      case 'subscribe':
        if (typeof data.session === 'string') liveSessionSubscribe(hubPeer, data.session);
        break;
      case 'unsubscribe':
        liveSessionUnsubscribe(hubPeer);
        break;
      case 'stop':
        if (typeof data.session === 'string') liveSessionStop(data.session);
        break;
    }
  },

  close(peer) {
    liveHubRemovePeer(peer as unknown as HubPeer);
  },

  error(_peer, error) {
    console.error('[live-trace] WebSocket error:', error);
  },
});
