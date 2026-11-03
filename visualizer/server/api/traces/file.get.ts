// One trace file from the trace directory: GET /api/traces/file?path=<path from the list>.
// Honours a single-range Range header, so the page can read a large trace in pieces.
import type { NodeReadable } from '../../utils/nodeProcess';

export default defineEventHandler(async (event) => {
  const root = traceRoot();
  const rel = getQuery(event).path;
  const file = root && typeof rel === 'string' ? await resolveTrace(root, rel) : null;
  if (!file) throw createError({ statusCode: 404, statusMessage: 'No such trace file' });
  // Stat and a ranged read stream rather than fs.openAsBlob, whose size wraps above 4 GB on Node 22.
  const fs = nodeProcess.getBuiltinModule('node:fs');
  const { size } = await fs.promises.stat(file);
  setResponseHeader(event, 'content-type', 'application/octet-stream');
  setResponseHeader(event, 'accept-ranges', 'bytes');
  const range = /^bytes=(\d+)-(\d*)$/.exec(getHeader(event, 'range') ?? '');
  if (range) {
    const start = Number(range[1]);
    const end = range[2] ? Math.min(Number(range[2]), size - 1) : size - 1;
    if (start > end || start >= size) {
      setResponseHeader(event, 'content-range', `bytes */${size}`);
      throw createError({ statusCode: 416, statusMessage: 'Range not satisfiable' });
    }
    setResponseStatus(event, 206);
    setResponseHeader(event, 'content-range', `bytes ${start}-${end}/${size}`);
    setResponseHeader(event, 'content-length', end - start + 1);
    return send(fs.createReadStream(file, { start, end }));
  }
  setResponseHeader(event, 'content-length', size);
  return send(fs.createReadStream(file));

  /** Streams the file; a client that goes away (a cancelled read) closes it. */
  function send(stream: NodeReadable) {
    event.node.res.on('close', () => stream.destroy());
    return sendStream(event, stream as Parameters<typeof sendStream>[1]);
  }
});
