/**
 * Exits when the process that launched the server is gone, so a killed launcher does not leave the
 * server running and holding its port.  `python -m ramulator visualize` passes its PID in
 * RAMULATOR_VISUALIZER_PARENT_PID and starts the server in its own process group.
 */
export default defineNitroPlugin(() => {
  const parent = Number(nodeProcess.env.RAMULATOR_VISUALIZER_PARENT_PID);
  if (!Number.isInteger(parent) || parent <= 0) return;

  setInterval(() => {
    try {
      nodeProcess.kill(parent, 0);
    } catch (e) {
      if ((e as { code?: string }).code !== 'ESRCH') return;
      // In dev mode the server runs under npx and nuxi; end the whole group.
      if (nodeProcess.platform !== 'win32') nodeProcess.kill(0, 'SIGTERM');
      nodeProcess.exit(0);
    }
  }, 2000);
});
