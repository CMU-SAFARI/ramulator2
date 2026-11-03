/**
 * The trace directory (`python -m ramulator visualize --traces DIR`, RAMULATOR_VISUALIZER_TRACES):
 * its `.ram2bin` files are listed on the start page and opened straight from the server.  Only
 * `.ram2bin` files inside the directory are served.
 */
const MAX_DEPTH = 6;
const MAX_FILES = 500;
const SKIP_DIRS = new Set(['node_modules', '.git', '.output', '.nuxt', 'build', '__pycache__']);

export interface TraceFile {
  path: string;
  size: number;
  modified: number;
}

export function traceRoot(): string | null {
  const dir = nodeProcess.env.RAMULATOR_VISUALIZER_TRACES;
  return dir ? nodeProcess.getBuiltinModule('node:path').resolve(dir) : null;
}

/** `.ram2bin` files under the trace directory, newest first. */
export async function listTraces(root: string): Promise<TraceFile[]> {
  const fs = nodeProcess.getBuiltinModule('node:fs').promises;
  const path = nodeProcess.getBuiltinModule('node:path');
  const found: TraceFile[] = [];
  async function walk(dir: string, depth: number) {
    let entries;
    try {
      entries = await fs.readdir(dir, { withFileTypes: true });
    } catch {
      return;
    }
    for (const e of entries) {
      if (found.length >= MAX_FILES) return;
      const full = path.join(dir, e.name);
      if (e.isDirectory()) {
        if (depth < MAX_DEPTH && !e.name.startsWith('.') && !SKIP_DIRS.has(e.name)) await walk(full, depth + 1);
      } else if (e.isFile() && e.name.endsWith('.ram2bin')) {
        const st = await fs.stat(full).catch(() => null);
        if (st) found.push({ path: path.relative(root, full).split(path.sep).join('/'), size: st.size, modified: st.mtimeMs });
      }
    }
  }
  await walk(root, 0);
  return found.sort((a, b) => b.modified - a.modified);
}

/** Absolute path of a listed trace, or null if `rel` is not a `.ram2bin` file inside the directory. */
export async function resolveTrace(root: string, rel: string): Promise<string | null> {
  const fs = nodeProcess.getBuiltinModule('node:fs').promises;
  const path = nodeProcess.getBuiltinModule('node:path');
  if (!rel.endsWith('.ram2bin') || path.isAbsolute(rel)) return null;
  try {
    const [realRoot, real] = await Promise.all([fs.realpath(root), fs.realpath(path.resolve(root, rel))]);
    const inside = path.relative(realRoot, real);
    // The file itself, not just a link to it, must be a trace inside the directory.
    if (!inside || inside.startsWith('..') || path.isAbsolute(inside) || !real.endsWith('.ram2bin')) return null;
    return (await fs.stat(real)).isFile() ? real : null;
  } catch {
    return null;
  }
}
