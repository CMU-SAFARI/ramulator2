// Timing values the views need from a trace's spec, resolved once per trace.
import type { TraceSpec } from '../composables/useTrace';
import { isWriteCommand } from '../utils/commandColors';

/** Bar length (cycles) of a command without a commandCycles entry. */
export const CMD_VIEW_DURATION = 1;

/** First of `names` set in the trace's timing table (standards name some timings differently), else `fallback`. */
export function specTiming(spec: TraceSpec, names: string[], fallback: number): number {
  for (const name of names) {
    const idx = spec.timingNames.indexOf(name);
    if (idx >= 0 && spec.timingValues[idx]! >= 0) return spec.timingValues[idx]!;
  }
  return fallback;
}

/** Read / write latency from a column command to its data on the bus, and the burst length (cycles). */
export function dataBusTiming(spec: TraceSpec): { readLatency: number; writeLatency: number; burst: number } {
  return {
    readLatency: specTiming(spec, ['nCL', 'nRL'], 16),
    writeLatency: specTiming(spec, ['nCWL', 'nWL'], 12),
    burst: specTiming(spec, ['nBL', 'nBL_min'], 4),
  };
}

/** Data-bus burst length of each command id: LPDDR6 `_L` commands use the long burst (nBL_min_L). */
export function burstCycles(spec: TraceSpec): Uint16Array {
  const { burst } = dataBusTiming(spec);
  const long = specTiming(spec, ['nBL_min_L'], burst);
  return Uint16Array.from(spec.commandNames, n => (n.endsWith('_L') ? long : burst));
}

/** Delay from issue to the data bus of each command id (0 for commands without data). */
export function busLatencyByCmd(spec: TraceSpec): Uint16Array {
  const { readLatency, writeLatency } = dataBusTiming(spec);
  return Uint16Array.from(spec.commandNames, (n, id) =>
    (spec.commandMeta[id]?.isAccessing ? (isWriteCommand(n) ? writeLatency : readLatency) : 0));
}

/** Independent data buses in one channel's trace: one per pseudo-channel (HBM), else one. */
export function dataBusCount(spec: TraceSpec): number {
  const pc = spec.levelNames.indexOf('PseudoChannel');
  let n = 1;
  for (let k = spec.levelNames[0] === 'Channel' ? 1 : 0; k <= pc; k++) n *= Math.max(1, spec.levelSizes[k]!);
  return n;
}

/** Command buses: separate row and column buses on dual-bus standards (HBM, GDDR7), else one. */
export function commandBusCount(spec: TraceSpec): number {
  return spec.commandMeta.some(m => m.isRowCommand || m.isColumnCommand) ? 2 : 1;
}

export function commandDuration(spec: TraceSpec | null | undefined, cmdId: number): number {
  return spec?.commandCycles?.[cmdId] ?? CMD_VIEW_DURATION;
}

export function maxCommandDuration(spec: TraceSpec | null | undefined): number {
  if (!spec || spec.commandCycles.length === 0) return CMD_VIEW_DURATION;
  let maxDuration = CMD_VIEW_DURATION;
  for (let i = 0; i < spec.commandCycles.length; i++)
    maxDuration = Math.max(maxDuration, spec.commandCycles[i] ?? CMD_VIEW_DURATION);
  return maxDuration;
}

/** Command classes counted by the window statistics. */
export const CMD_CLASS = { OTHER: 0, READ: 1, WRITE: 2 } as const;

export function commandClasses(spec: TraceSpec): Uint8Array {
  return Uint8Array.from(spec.commandNames, (n, id) =>
    (spec.commandMeta[id]?.isAccessing ? (isWriteCommand(n) ? CMD_CLASS.WRITE : CMD_CLASS.READ) : CMD_CLASS.OTHER));
}
