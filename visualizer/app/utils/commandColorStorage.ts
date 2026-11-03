/** localStorage key for per-command-name hex overrides (stable across traces). */
export const COMMAND_COLOR_STORAGE_KEY = 'ramulator2-visualizer-command-colors';

/** Only `#rrggbb` values are kept from storage; anything else would break the color lookups. */
function validColors(parsed: unknown): Record<string, string> {
  const out: Record<string, string> = {};
  if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) return out;
  for (const [k, v] of Object.entries(parsed)) {
    if (typeof v === 'string' && /^#[0-9a-f]{6}$/i.test(v)) out[k] = v;
  }
  return out;
}

export function readCommandColorOverrides(): Record<string, string> {
  if (!import.meta.client) return {};
  try {
    const raw = localStorage.getItem(COMMAND_COLOR_STORAGE_KEY);
    if (!raw) return {};
    return validColors(JSON.parse(raw));
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
    return validColors(JSON.parse(raw)) as Partial<RequestTypeStoredColors>;
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
