// `instance` lets the launcher tell its own server from one already running on the port.
export default defineEventHandler(() => ({
  status: "ok",
  instance: nodeProcess.env.RAMULATOR_VISUALIZER_INSTANCE ?? "",
}));
