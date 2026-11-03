// Saves the index the page made on a trace's first read (server/utils/traceIndex.ts):
// PUT /api/traces/trace-index?path=<path from the list>&key=<settings key>, the index as the body.
export default defineEventHandler(async (event) => {
  const root = traceRoot();
  const { path: rel, key } = getQuery(event);
  const trace = root && typeof rel === 'string' ? await resolveTrace(root, rel) : null;
  if (!trace) throw createError({ statusCode: 404, statusMessage: 'No such trace file' });
  const index = await indexPath(trace, key);
  if (!index) throw createError({ statusCode: 400, statusMessage: 'No place to keep the index' });
  const length = Number(getHeader(event, 'content-length'));
  if (!Number.isFinite(length)) throw createError({ statusCode: 411, statusMessage: 'Content-Length is required' });
  if (length > INDEX_MAX_BYTES) throw createError({ statusCode: 413, statusMessage: 'Index too large' });
  const body = await readRawBody(event, false);
  if (!body || !looksLikeIndex(body)) throw createError({ statusCode: 400, statusMessage: 'Not a trace index' });
  await writeIndex(index, body);
  return { ok: true };
});
