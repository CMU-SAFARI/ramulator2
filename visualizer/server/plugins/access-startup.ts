/**
 * At startup: makes the access token (and its file) and says how to open the visualizer with it,
 * unless the launcher passed its own and prints that itself; says when the server refuses
 * everything because other machines can reach it while the token is turned off.
 */
export default defineNitroPlugin(() => {
  const { token, file, problem } = accessToken();
  if (token && !nodeProcess.env.RAMULATOR_VISUALIZER_TOKEN) {
    // In Docker the browser reaches the port the container is published on.
    const port = nodeProcess.env.RAMULATOR_VISUALIZER_PUBLISHED_PORT || serverPort();
    if (port) {
      console.log(`[ramviz] Open http://localhost:${port}/?token=${token}`);
      if (file) console.log(`[ramviz] The access token is kept in ${file}.`);
    } else {
      console.log(`[ramviz] Open this server's address with ?token=${token}`);
      console.log('[ramviz] Simulations need RAMULATOR_VISUALIZER_TOKEN set to it, or start the dev server with PORT set.');
    }
    if (problem) console.warn(`[ramviz] The access token is not saved (${problem}); simulations need RAMULATOR_VISUALIZER_TOKEN.`);
  }
  const exposed = exposedWithoutToken();
  if (exposed) console.error(`[ramviz] ${exposed}`);
});
