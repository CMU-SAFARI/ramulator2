// The saved index of a trace from the trace directory (server/utils/traceIndex.ts):
// GET /api/traces/trace-index?path=<path from the list>&key=<settings key>; 204 if there is none yet.
import type { NodeReadable } from '../../utils/nodeProcess';

export default defineEventHandler(async (event) => {
  const root = traceRoot();
  const { path: rel, key } = getQuery(event);
  const trace = root && typeof rel === 'string' ? await resolveTrace(root, rel) : null;
  if (!trace) throw createError({ statusCode: 404, statusMessage: 'No such trace file' });
  const index = await indexPath(trace, key);
  const fs = nodeProcess.getBuiltinModule('node:fs');
  const st = index ? await fs.promises.stat(index).catch(() => null) : null;
  if (!index || !st) {
    setResponseStatus(event, 204);
    return null;
  }
  setResponseHeader(event, 'content-type', 'application/octet-stream');
  setResponseHeader(event, 'content-length', st.size);
  const stream: NodeReadable = fs.createReadStream(index);
  event.node.res.on('close', () => stream.destroy());
  return sendStream(event, stream as Parameters<typeof sendStream>[1]);
});
