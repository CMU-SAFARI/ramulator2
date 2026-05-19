const MAGIC = "RAM2BIN\0";
const HEADER_SIZE = 64;

export interface TraceHeader {
  version: [number, number];
  levelCount: number;
  commandCount: number;
  timingCount: number;
  channelWidth: number;
  prefetchSize: number;
  dq: number;
  channelId: number;
  readLatency: number;
  numEntries: number;
  dataOffset: number;
  dramType: string;
}

export interface CommandMeta {
  isOpening: boolean;
  isClosing: boolean;
  isAccessing: boolean;
  isRefreshing: boolean;
  isRowCommand: boolean;
  isColumnCommand: boolean;
}

export interface TraceSpec {
  levelNames: string[];
  levelSizes: Uint32Array;
  commandNames: string[];
  commandMeta: CommandMeta[];
  commandCycles: Uint8Array;
  timingNames: string[];
  timingValues: Int32Array;
}

export interface TraceArrays {
  clk: BigInt64Array;
  arrive: BigInt64Array;
  cmdId: Uint8Array;
  typeId: Int8Array;
  sourceId: Int16Array;
  addr: Int32Array[];
}

export interface ParsedTrace {
  buffer: ArrayBuffer;
  header: TraceHeader;
  spec: TraceSpec;
  arrays: TraceArrays;
}

function readNullTerminatedStrings(view: DataView, offset: number, count: number): { strings: string[]; bytesRead: number } {
  const strings: string[] = [];
  let pos = offset;
  for (let i = 0; i < count; i++) {
    let end = pos;
    while (view.getUint8(end) !== 0) end++;
    const bytes = new Uint8Array(view.buffer, pos, end - pos);
    strings.push(new TextDecoder().decode(bytes));
    pos = end + 1;
  }
  return { strings, bytesRead: pos - offset };
}

function readNullTerminatedString(view: DataView, offset: number, maxLen: number): string {
  let end = 0;
  while (end < maxLen && view.getUint8(offset + end) !== 0) end++;
  return new TextDecoder().decode(new Uint8Array(view.buffer, offset, end));
}

function parseHeader(view: DataView): TraceHeader {
  const magicBytes = new Uint8Array(view.buffer, 0, 8);
  const magic = new TextDecoder().decode(magicBytes.slice(0, 7));
  if (magic !== MAGIC.slice(0, 7)) {
    throw new Error(`Invalid magic: expected "RAM2BIN", got "${magic}"`);
  }

  return {
    version: [view.getUint8(8), view.getUint8(9)],
    levelCount: view.getUint16(12, true),
    commandCount: view.getUint16(14, true),
    timingCount: view.getUint16(16, true),
    channelWidth: view.getUint16(18, true),
    prefetchSize: view.getUint16(20, true),
    dq: view.getUint16(22, true),
    channelId: view.getUint32(24, true),
    readLatency: view.getInt32(28, true),
    numEntries: Number(view.getBigUint64(32, true)),
    dataOffset: Number(view.getBigUint64(40, true)),
    dramType: readNullTerminatedString(view, 48, 16),
  };
}

function parseSpec(buf: ArrayBuffer, header: TraceHeader): TraceSpec {
  const view = new DataView(buf);
  let offset = HEADER_SIZE;

  const { strings: levelNames, bytesRead: lnBytes } =
    readNullTerminatedStrings(view, offset, header.levelCount);
  offset += lnBytes;

  const levelSizes = new Uint32Array(header.levelCount);
  for (let i = 0; i < header.levelCount; i++)
    levelSizes[i] = view.getUint32(offset + i * 4, true);
  offset += header.levelCount * 4;

  const { strings: commandNames, bytesRead: cnBytes } =
    readNullTerminatedStrings(view, offset, header.commandCount);
  offset += cnBytes;

  const commandMeta: CommandMeta[] = [];
  for (let i = 0; i < header.commandCount; i++) {
    const bits = view.getUint8(offset + i);
    commandMeta.push({
      isOpening:       !!(bits & (1 << 0)),
      isClosing:       !!(bits & (1 << 1)),
      isAccessing:     !!(bits & (1 << 2)),
      isRefreshing:    !!(bits & (1 << 3)),
      isRowCommand:    !!(bits & (1 << 4)),
      isColumnCommand: !!(bits & (1 << 5)),
    });
  }
  offset += header.commandCount;

  const commandCycles = new Uint8Array(header.commandCount);
  const hasCommandCycles = header.version[0] > 1 || (header.version[0] === 1 && header.version[1] >= 1);
  if (hasCommandCycles) {
    for (let i = 0; i < header.commandCount; i++)
      commandCycles[i] = view.getUint8(offset + i);
    offset += header.commandCount;
  } else {
    commandCycles.fill(1);
  }

  const { strings: timingNames, bytesRead: tnBytes } =
    readNullTerminatedStrings(view, offset, header.timingCount);
  offset += tnBytes;

  const timingValues = new Int32Array(header.timingCount);
  for (let i = 0; i < header.timingCount; i++)
    timingValues[i] = view.getInt32(offset + i * 4, true);

  return { levelNames, levelSizes, commandNames, commandMeta, commandCycles, timingNames, timingValues };
}

function parseArrays(buf: ArrayBuffer, header: TraceHeader): TraceArrays {
  const D = header.dataOffset;
  const N = header.numEntries;
  const L = header.levelCount;

  const clk      = new BigInt64Array(buf, D,            N);
  const arrive    = new BigInt64Array(buf, D + 8 * N,    N);
  const cmdId     = new Uint8Array(buf,   D + 16 * N,   N);
  const typeId    = new Int8Array(buf,    D + 17 * N,   N);
  const sourceId  = new Int16Array(buf,   D + 18 * N,   N);

  const addr: Int32Array[] = [];
  for (let k = 0; k < L; k++) {
    addr.push(new Int32Array(buf, D + (20 + 4 * k) * N, N));
  }

  return { clk, arrive, cmdId, typeId, sourceId, addr };
}

export function parseTrace(buf: ArrayBuffer): ParsedTrace {
  const view = new DataView(buf);
  const header = parseHeader(view);
  const spec = parseSpec(buf, header);
  const arrays = parseArrays(buf, header);
  return { buffer: buf, header, spec, arrays };
}

function yieldToMain(): Promise<void> {
  return new Promise((resolve) => requestAnimationFrame(() => resolve()));
}

export interface ParseTraceAsyncOptions {
  /** Called before each parse step (after the preceding yield) so the UI can show progress. */
  onProgress?: (message: string) => void;
}

/** Same as {@link parseTrace}, but yields so the UI can paint between phases. */
export async function parseTraceAsync(
  buffer: ArrayBuffer,
  options?: ParseTraceAsyncOptions,
): Promise<ParsedTrace> {
  const report = options?.onProgress;
  report?.('Reading header…');
  await yieldToMain();
  const view = new DataView(buffer);
  const header = parseHeader(view);
  report?.('Reading spec (levels, commands, timings)…');
  await yieldToMain();
  const spec = parseSpec(buffer, header);
  report?.('Mapping event arrays…');
  await yieldToMain();
  const arrays = parseArrays(buffer, header);
  return { buffer, header, spec, arrays };
}

export function useTrace() {
  return { parseTrace, parseTraceAsync };
}
