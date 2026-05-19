/**
 * HTTP POST endpoint for live trace streaming (producer ingestion).
 *
 * C++ or any HTTP client POSTs JSON messages here:
 *   { type: "init",   header: {...}, spec: {...} }
 *   { type: "events", events: [...] }
 *   { type: "done" }
 *
 * Each message is forwarded to all connected WebSocket peers (browsers).
 */

export default defineEventHandler(async (event) => {
  const body = await readBody(event);
  if (!body || typeof body.type !== 'string') {
    throw createError({ statusCode: 400, statusMessage: 'Missing "type" field' });
  }

  switch (body.type) {
    case 'init':
      liveTraceSetSession(body.header, body.spec);
      liveTraceBroadcast(JSON.stringify({ type: 'init', header: body.header, spec: body.spec }));
      break;

    case 'events':
      if (Array.isArray(body.events)) {
        const session = liveTraceGetSession();
        if (session && !session.done) {
          liveTraceBroadcast(JSON.stringify({ type: 'events', events: body.events }));
        }
      }
      break;

    case 'done':
      liveTraceMarkDone();
      liveTraceBroadcast(JSON.stringify({ type: 'done' }));
      break;

    case 'status':
      // Endpoint for producer to check if the session was interrupted
      const session = liveTraceGetSession();
      return { ok: true, interrupted: session?.interrupted ?? false };

    default:
      throw createError({ statusCode: 400, statusMessage: `Unknown type: ${body.type}` });
  }

  return { ok: true };
});
