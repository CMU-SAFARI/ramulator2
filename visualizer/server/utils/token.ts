/**
 * The access token.  `python -m ramulator visualize` passes its own in RAMULATOR_VISUALIZER_TOKEN.
 * A server started otherwise (Docker, `node .output/server/index.mjs`) keeps one in a token file,
 * made on first start: RAMULATOR_VISUALIZER_TOKEN_FILE, or the launcher's file for this port
 * (${XDG_CACHE_HOME:-~/.cache}/ramulator/visualizer-<port>.token), where LiveTraceStreamer on
 * this machine finds it.  RAMULATOR_VISUALIZER_NO_TOKEN=1 turns the token off.
 */
export interface TokenSource {
  token: string;
  /** The token file, when the token is kept in one. */
  file: string | null;
  /** Why the token could not be saved to its file, or null. */
  problem: string | null;
}

/** The port the server listens on, or null for a dev server given its port on nuxi's command line only. */
export function serverPort(): string | null {
  const env = nodeProcess.env;
  return env.NUXT_PORT || env.NITRO_PORT || env.PORT || env.RAMULATOR_VISUALIZER_PORT || (import.meta.dev ? null : '3000');
}

function defaultTokenFile(): string | null {
  const env = nodeProcess.env;
  const cache = env.XDG_CACHE_HOME || (env.HOME ? `${env.HOME}/.cache` : '');
  // Without a port, a file LiveTraceStreamer does not look for; it keeps the token across restarts.
  return cache ? `${cache}/ramulator/visualizer-${serverPort() ?? 'dev'}.token` : null;
}

function newToken(): string {
  const bytes = crypto.getRandomValues(new Uint8Array(24));
  return btoa(String.fromCharCode(...bytes)).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

export function loadToken(): TokenSource {
  const env = nodeProcess.env;
  if (env.RAMULATOR_VISUALIZER_TOKEN) return { token: env.RAMULATOR_VISUALIZER_TOKEN, file: null, problem: null };
  if (env.RAMULATOR_VISUALIZER_NO_TOKEN === '1') return { token: '', file: null, problem: null };
  const fs = nodeProcess.getBuiltinModule('node:fs');
  const path = nodeProcess.getBuiltinModule('node:path');
  const explicit = env.RAMULATOR_VISUALIZER_TOKEN_FILE || null;
  const file = explicit ?? defaultTokenFile();
  if (file) {
    try {
      const kept = fs.readFileSync(file, 'utf8').trim();
      if (kept) return { token: kept, file, problem: null };
    } catch {
      // Not made yet.
    }
  }
  const token = newToken();
  if (!file) return { token, file: null, problem: 'no home directory to keep it in' };
  try {
    // A file given explicitly is shared, e.g. with the simulation's container through a Docker
    // volume, whose user may differ; the launcher's own file is for this user only.
    const mode = explicit ? 0o644 : 0o600;
    if (!explicit) fs.mkdirSync(path.dirname(file), { recursive: true, mode: 0o700 });
    fs.writeFileSync(file, `${token}\n`, { mode });
    fs.chmodSync(file, mode);
    return { token, file, problem: null };
  } catch (e) {
    return { token, file: null, problem: `could not write ${file}: ${(e as Error).message}` };
  }
}
