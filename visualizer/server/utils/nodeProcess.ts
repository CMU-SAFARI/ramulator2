/** Node's `process`, typed for the calls the server makes (the project does not depend on @types/node). */
export interface NodeProcess {
  env: Record<string, string | undefined>;
  platform: string;
  kill(pid: number, signal?: string | number): true;
  exit(code?: number): never;
  getBuiltinModule(id: 'node:fs'): NodeFs;
  getBuiltinModule(id: 'node:path'): NodePath;
}

interface NodeEntry {
  name: string;
  isFile(): boolean;
  isDirectory(): boolean;
}

interface NodeStats {
  size: number;
  mtimeMs: number;
  isFile(): boolean;
}

/** The parts of node:fs the server uses. */
export interface NodeFs {
  promises: {
    readdir(path: string, options: { withFileTypes: true }): Promise<NodeEntry[]>;
    stat(path: string): Promise<NodeStats>;
    realpath(path: string): Promise<string>;
    readdir(path: string): Promise<string[]>;
    readFile(path: string): Promise<Uint8Array>;
    writeFile(path: string, data: Uint8Array): Promise<void>;
    rename(from: string, to: string): Promise<void>;
    unlink(path: string): Promise<void>;
    mkdir(path: string, options: { recursive: true }): Promise<unknown>;
  };
  /** A node:stream Readable of bytes [start, end] (inclusive). */
  createReadStream(path: string, options?: { start?: number; end?: number }): NodeReadable;
  readFileSync(path: string, encoding: 'utf8'): string;
  writeFileSync(path: string, data: string, options: { mode: number }): void;
  chmodSync(path: string, mode: number): void;
  mkdirSync(path: string, options: { recursive: true; mode: number }): void;
}

/** A node:stream Readable, as h3's sendStream takes it. */
export interface NodeReadable {
  pipe(destination: unknown): unknown;
  on(event: string, listener: (...args: unknown[]) => void): NodeReadable;
  destroy(): void;
}

/** The parts of node:path the server uses. */
export interface NodePath {
  sep: string;
  join(...parts: string[]): string;
  resolve(...parts: string[]): string;
  relative(from: string, to: string): string;
  isAbsolute(path: string): boolean;
  dirname(path: string): string;
}

export const nodeProcess = (globalThis as unknown as { process: NodeProcess }).process;
