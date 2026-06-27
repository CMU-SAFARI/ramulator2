/**
 * WebSocket endpoint for live trace streaming (browser subscription).
 *
 * Browsers connect here to receive real-time trace events.  Producers
 * can also send messages over this WS (init / events / done), which
 * are relayed to all other peers.  For C++ producers that prefer HTTP,
 * see the companion POST endpoint at /api/live-trace-push.
 */

export default defineWebSocketHandler({
  open(peer) {
    liveTraceAddPeer(peer);
    const session = liveTraceGetSession();
    if (session) {
      peer.send(JSON.stringify({ type: 'init', header: session.header, spec: session.spec }));
      if (session.done) {
        peer.send(JSON.stringify({ type: 'done' }));
      }
    }
  },

  message(peer, msg) {
    let data: any;
    try {
      data = JSON.parse(typeof msg === 'string' ? msg : msg.text());
    } catch {
      return;
    }

    switch (data.type) {
      case 'init':
        liveTraceSetSession(data.header, data.spec);
        liveTraceBroadcast(
          JSON.stringify({ type: 'init', header: data.header, spec: data.spec }),
          peer,
        );
        break;

      case 'events': {
        const session = liveTraceGetSession();
        if (session && !session.done && Array.isArray(data.events)) {
          liveTraceBroadcast(
            JSON.stringify({ type: 'events', events: data.events }),
            peer,
          );
        }
        break;
      }

      case 'done':
        liveTraceMarkDone();
        liveTraceBroadcast(JSON.stringify({ type: 'done' }), peer);
        break;

      case 'interrupt':
        liveTraceMarkInterrupted();
        liveTraceBroadcast(JSON.stringify({ type: 'interrupt' }), peer);
        break;
    }
  },

  close(peer) {
    liveTraceRemovePeer(peer);
  },

  error(_peer, error) {
    console.error('[live-trace] WebSocket error:', error);
  },
});
