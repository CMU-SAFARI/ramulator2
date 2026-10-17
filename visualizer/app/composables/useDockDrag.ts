// Pointer-driven drag of a view between the header tab strip (parked) and the dock (stacked).
// Sources: header tabs and panel title bars. Targets: a gap between docked panels, or the tab strip.
import type { ViewMode } from '~/stores/ui';

export type DockDropTarget = { kind: 'dock'; index: number } | { kind: 'park' };
/** Where a drag started: a header tab or a panel title bar. */
export type DockDragSource = 'tab' | 'panel';

/** Pointer travel before a press becomes a drag (px). */
const DRAG_THRESHOLD = 5;

const state = reactive({
  view: null as ViewMode | null,
  source: 'tab' as DockDragSource,
  active: false,
  x: 0,
  y: 0,
  target: null as DockDropTarget | null,
});

const zones: Record<'dock' | 'park', HTMLElement | null> = { dock: null, park: null };
/** A drag just ended; the click the browser fires after pointerup must not act. */
let justDragged = false;

function inside(el: HTMLElement | null, x: number, y: number): boolean {
  if (!el) return false;
  const r = el.getBoundingClientRect();
  return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

function targetAt(view: ViewMode, source: DockDragSource, x: number, y: number): DockDropTarget | null {
  const ui = useUIStore();
  const from = ui.stack.indexOf(view);
  // Only a panel title parks; a docked tab is already on the strip, so jiggling it there does nothing.
  if (inside(zones.park, x, y)) return source === 'panel' && from >= 0 && ui.stack.length > 1 ? { kind: 'park' } : null;
  if (!inside(zones.dock, x, y)) return null;
  const panels = zones.dock!.querySelectorAll<HTMLElement>('[data-dock-view]');
  let index = panels.length;
  for (let i = 0; i < panels.length; i++) {
    const r = panels[i]!.getBoundingClientRect();
    if (y < r.top + r.height / 2) {
      index = i;
      break;
    }
  }
  // Dropping a docked panel next to itself changes nothing.
  if (from >= 0 && (index === from || index === from + 1)) return null;
  return { kind: 'dock', index };
}

/** Swallow the click the browser fires on release (after a drag, or after Escape mid-drag). */
function suppressClickUntilRelease(released: boolean) {
  justDragged = true;
  const clear = () => setTimeout(() => {
    justDragged = false;
  }, 0);
  if (released) clear();
  else window.addEventListener('pointerup', clear, { once: true, capture: true });
}

function start(view: ViewMode, source: DockDragSource, e: PointerEvent) {
  if (e.button !== 0 || state.view) return;
  const x0 = e.clientX;
  const y0 = e.clientY;
  state.view = view;
  state.source = source;

  const finish = (apply: boolean, released: boolean) => {
    window.removeEventListener('pointermove', move);
    window.removeEventListener('pointerup', up);
    window.removeEventListener('pointercancel', cancel);
    window.removeEventListener('keydown', key, true);
    document.documentElement.classList.remove('rv-dragging');
    if (state.active) {
      suppressClickUntilRelease(released);
      const t = state.target;
      const ui = useUIStore();
      if (apply && t?.kind === 'dock') ui.dock(view, t.index);
      else if (apply && t?.kind === 'park') ui.park(view);
    }
    state.view = null;
    state.active = false;
    state.target = null;
  };
  const move = (ev: PointerEvent) => {
    if (!state.active) {
      if (Math.hypot(ev.clientX - x0, ev.clientY - y0) < DRAG_THRESHOLD) return;
      state.active = true;
      document.documentElement.classList.add('rv-dragging');
    }
    state.x = ev.clientX;
    state.y = ev.clientY;
    state.target = targetAt(view, source, ev.clientX, ev.clientY);
  };
  const up = () => finish(true, true);
  const cancel = () => finish(false, true);
  const key = (ev: KeyboardEvent) => {
    if (ev.key !== 'Escape' || !state.active) return;
    ev.preventDefault();
    finish(false, false);
  };
  window.addEventListener('pointermove', move);
  window.addEventListener('pointerup', up);
  window.addEventListener('pointercancel', cancel);
  window.addEventListener('keydown', key, true);
}

export function useDockDrag() {
  return {
    state: readonly(state),
    start,
    /** Register the dock (stack) or park (tab strip) drop zone; pass null on unmount. */
    setZone(kind: 'dock' | 'park', el: HTMLElement | null) {
      zones[kind] = el;
    },
    /** True (once) right after a drag; click handlers return early on it. */
    consumeClick(): boolean {
      const was = justDragged;
      justDragged = false;
      return was;
    },
  };
}
