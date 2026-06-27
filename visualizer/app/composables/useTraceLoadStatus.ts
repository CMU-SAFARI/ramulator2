/**
 * Short status line while loading/parsing a trace or initializing the renderer.
 * Shared across upload and trace view so progress stays visible after navigation.
 */
export function useTraceLoadStatus() {
  return useState<string | null>('trace-load-status', () => null);
}
