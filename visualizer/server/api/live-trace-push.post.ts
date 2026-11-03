/**
 * HTTP endpoint for the simulator (LiveTraceStreamer):
 *
 *   POST /api/live-trace-push?type=init&session=<run>:<channel>    { run, channel, startedAt, header, spec }
 *   POST /api/live-trace-push?type=events&session=…&count=<n>      { columns … }  (forwarded as is)
 *   POST /api/live-trace-push?type=status&session=…                (heartbeat)
 *   POST /api/live-trace-push?type=done&session=…
 *
 * Events and status answer { ok, interrupted } so the simulator learns when a viewer stopped the
 * stream, or { ok: false, unknown: true } when the server does not know the session (it restarted)
 * and the simulator should send init again.  Bodies must be JSON and at most 32 MB.
 */
const MAX_BODY_BYTES = 32 * 2 ** 20;

function badInit(body: any): string | null {
  const h = body?.header;
  const s = body?.spec;
  if (typeof body?.run !== 'string' || !Number.isInteger(body?.channel) || typeof body?.startedAt !== 'number') {
    return 'init needs run, channel and startedAt';
  }
  if (!h || !s) return 'init needs header and spec';
  for (const k of ['levelCount', 'commandCount', 'timingCount', 'channelId', 'readLatency']) {
    if (!Number.isInteger(h[k])) return `header.${k} must be an integer`;
  }
  const lists: [string, number][] = [
    ['levelNames', h.levelCount], ['levelSizes', h.levelCount], ['commandNames', h.commandCount],
    ['commandMeta', h.commandCount], ['timingNames', h.timingCount], ['timingValues', h.timingCount],
  ];
  for (const [k, n] of lists) {
    if (!Array.isArray(s[k]) || s[k].length !== n) return `spec.${k} must have ${n} entries`;
  }
  return null;
}

export default defineEventHandler(async (event) => {
  if (!(getHeader(event, 'content-type') ?? '').startsWith('application/json')) {
    throw createError({ statusCode: 415, statusMessage: 'Send JSON (Content-Type: application/json)' });
  }
  const length = Number(getHeader(event, 'content-length'));
  if (!Number.isFinite(length)) throw createError({ statusCode: 411, statusMessage: 'Content-Length is required' });
  if (length > MAX_BODY_BYTES) throw createError({ statusCode: 413, statusMessage: 'Body is larger than 32 MB' });

  const { type, session, count } = getQuery(event);
  if (typeof session !== 'string' || !session) throw createError({ statusCode: 400, statusMessage: 'Missing session' });

  switch (type) {
    case 'init': {
      const body = await readBody(event);
      const problem = badInit(body);
      if (problem) throw createError({ statusCode: 400, statusMessage: problem });
      if (session !== `${body.run}:${body.channel}`) throw createError({ statusCode: 400, statusMessage: 'session must be <run>:<channel>' });
      liveSessionStart({ id: session, run: body.run, channel: body.channel, startedAt: body.startedAt, header: body.header, spec: body.spec });
      return { ok: true };
    }
    case 'events': {
      const n = Number(count);
      if (!Number.isInteger(n) || n < 0) throw createError({ statusCode: 400, statusMessage: 'count must be a non-negative integer' });
      const columns = (await readRawBody(event, 'utf8')) ?? '';
      if (!columns.startsWith('{')) throw createError({ statusCode: 400, statusMessage: 'events must be a JSON object of columns' });
      return liveSessionEvents(session, columns, n);
    }
    case 'status':
      return liveSessionStatus(session);
    case 'done':
      liveSessionDone(session);
      return { ok: true };
    default:
      throw createError({ statusCode: 400, statusMessage: `Unknown type: ${String(type)}` });
  }
});
