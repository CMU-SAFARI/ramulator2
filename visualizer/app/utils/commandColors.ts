import type { CommandMeta } from '~/composables/useTrace';

/** Default semantic palette for command categories (matches WebGL lookup defaults). */
export const CMD_PALETTE = {
  opening: '#22c55e',
  closing: '#ef4444',
  readAccess: '#3b82f6',
  writeAccess: '#f97316',
  refreshing: '#a855f7',
  fallback: '#6b7280',
} as const;

export function isWriteCommand(name: string): boolean {
  const u = name.toUpperCase();
  return u.includes('WR') || u.includes('WRITE');
}

export function defaultCmdColor(name: string, meta: CommandMeta): string {
  if (meta.isAccessing) {
    return isWriteCommand(name) ? CMD_PALETTE.writeAccess : CMD_PALETTE.readAccess;
  }
  if (meta.isOpening) return CMD_PALETTE.opening;
  if (meta.isClosing) return CMD_PALETTE.closing;
  if (meta.isRefreshing) return CMD_PALETTE.refreshing;
  return CMD_PALETTE.fallback;
}

export function hex2rgb(hex: string): [number, number, number] {
  hex = hex.replace('#', '');
  return [
    parseInt(hex.substring(0, 2), 16) / 255,
    parseInt(hex.substring(2, 4), 16) / 255,
    parseInt(hex.substring(4, 6), 16) / 255,
  ];
}
