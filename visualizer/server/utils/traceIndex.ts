/**
 * Indexes of the trace directory's traces (app/engine/indexFile.ts), kept so that a trace is read in
 * full once for everyone who opens it: in RAMULATOR_VISUALIZER_INDEX_DIR, else
 * ${XDG_CACHE_HOME:-~/.cache}/ramulator/visualizer-index.  A file's name ties it to the trace's path,
 * size and modification time and to the settings key the page sends, so a changed trace or other
 * timing overrides get a new index; the oldest files go once the directory exceeds INDEX_DIR_BYTES.
 */
export const INDEX_MAX_BYTES = 512 * 2 ** 20;
const INDEX_DIR_BYTES = 2 * 2 ** 30;
const MAGIC = 'RAMVIZIX';

export function indexDir(): string | null {
  const env = nodeProcess.env;
  if (env.RAMULATOR_VISUALIZER_INDEX_DIR) return env.RAMULATOR_VISUALIZER_INDEX_DIR;
  const cache = env.XDG_CACHE_HOME || (env.HOME ? `${env.HOME}/.cache` : '');
  return cache ? `${cache}/ramulator/visualizer-index` : null;
}

function hashPath(text: string): string {
  let h = 0x811c9dc5;
  for (let i = 0; i < text.length; i++) h = Math.imul(h ^ text.charCodeAt(i), 0x01000193);
  return (h >>> 0).toString(16).padStart(8, '0');
}

/** Where the index of trace file `trace` (an absolute path) for settings `key` is kept, or null. */
export async function indexPath(trace: string, key: unknown): Promise<string | null> {
  const dir = indexDir();
  if (!dir || typeof key !== 'string' || !/^[0-9a-f]{16}$/.test(key)) return null;
  const st = await nodeProcess.getBuiltinModule('node:fs').promises.stat(trace).catch(() => null);
  return st ? `${dir}/${hashPath(trace)}-${st.size}-${Math.round(st.mtimeMs)}-${key}.idx` : null;
}

/** Whether `bytes` starts like an index file. */
export function looksLikeIndex(bytes: Uint8Array): boolean {
  return bytes.length > 24 && new TextDecoder().decode(bytes.subarray(0, 8)) === MAGIC;
}

/** Write an index in one step (a reader never sees half of one), then trim the directory. */
export async function writeIndex(path: string, bytes: Uint8Array) {
  const fs = nodeProcess.getBuiltinModule('node:fs').promises;
  const dir = indexDir()!;
  await fs.mkdir(dir, { recursive: true });
  const tmp = `${path}.${Date.now()}-${Math.random().toString(36).slice(2)}.tmp`;
  await fs.writeFile(tmp, bytes);
  await fs.rename(tmp, path);
  const files = await Promise.all((await fs.readdir(dir)).filter(n => n.endsWith('.idx')).map(async (n) => {
    const st = await fs.stat(`${dir}/${n}`).catch(() => null);
    return { path: `${dir}/${n}`, size: st?.size ?? 0, at: st?.mtimeMs ?? 0 };
  }));
  files.sort((a, b) => b.at - a.at);
  let total = 0;
  for (const f of files) {
    total += f.size;
    if (total > INDEX_DIR_BYTES) await fs.unlink(f.path).catch(() => {});
  }
}
