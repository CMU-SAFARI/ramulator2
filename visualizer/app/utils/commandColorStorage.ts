/** localStorage key for per-command-name hex overrides (stable across traces). */
export const COMMAND_COLOR_STORAGE_KEY = 'ramulator2-visualizer-command-colors';

export function readCommandColorOverrides(): Record<string, string> {
  if (!import.meta.client) return {};
  try {
    const raw = localStorage.getItem(COMMAND_COLOR_STORAGE_KEY);
    if (!raw) return {};
    const parsed = JSON.parse(raw) as unknown;
    if (parsed && typeof parsed === 'object' && !Array.isArray(parsed))
      return parsed as Record<string, string>;
  } catch {
    /* ignore */
  }
  return {};
}

export function writeCommandColorOverride(commandName: string, color: string) {
  if (!import.meta.client) return;
  try {
    const next = { ...readCommandColorOverrides(), [commandName]: color };
    localStorage.setItem(COMMAND_COLOR_STORAGE_KEY, JSON.stringify(next));
  } catch {
    /* ignore */
  }
}

/** Request view: Read / Write / Maintenance bar colors. */
export const REQUEST_TYPE_COLOR_STORAGE_KEY = 'ramulator2-visualizer-request-type-colors';

export interface RequestTypeStoredColors {
  read: string;
  write: string;
  maintenance: string;
}

export function readRequestTypeColors(): Partial<RequestTypeStoredColors> {
  if (!import.meta.client) return {};
  try {
    const raw = localStorage.getItem(REQUEST_TYPE_COLOR_STORAGE_KEY);
    if (!raw) return {};
    const parsed = JSON.parse(raw) as unknown;
    if (parsed && typeof parsed === 'object' && !Array.isArray(parsed))
      return parsed as Partial<RequestTypeStoredColors>;
  } catch {
    /* ignore */
  }
  return {};
}

export function writeRequestTypeColors(colors: RequestTypeStoredColors) {
  if (!import.meta.client) return;
  try {
    localStorage.setItem(REQUEST_TYPE_COLOR_STORAGE_KEY, JSON.stringify(colors));
  } catch {
    /* ignore */
  }
}

export function clearAllVisualizerColorStorage() {
  if (!import.meta.client) return;
  try {
    localStorage.removeItem(COMMAND_COLOR_STORAGE_KEY);
    localStorage.removeItem(REQUEST_TYPE_COLOR_STORAGE_KEY);
  } catch {
    /* ignore */
  }
}
