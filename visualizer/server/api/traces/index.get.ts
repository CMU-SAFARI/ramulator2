// Trace files on the server, for the start page; `enabled` is false without a trace directory.
export default defineEventHandler(async () => {
  const root = traceRoot();
  if (!root) return { enabled: false, root: '', files: [] };
  return { enabled: true, root, files: await listTraces(root) };
});
