<template>
  <div ref="root" class="rv-dock" :class="{ dragging: drag.state.active }">
    <template v-for="(view, i) in uiStore.stack" :key="view">
      <div
        v-if="i > 0"
        class="rv-split"
        role="separator"
        aria-orientation="horizontal"
        :aria-label="`Resize ${VIEW_LABELS[uiStore.stack[i - 1]!]} and ${VIEW_LABELS[view]}`"
        @pointerdown="startResize(i, $event)"
        @dblclick="equalize(i)"
      />
      <TracePanel
        :view="view"
        :style="{ flexGrow: uiStore.weights[view] * (opening[view] ?? 1), minHeight: opening[view] !== undefined ? '0px' : undefined }"
      />
    </template>
    <canvas ref="links" class="rv-links" aria-hidden="true" />
    <div v-if="indicatorTop !== null" class="rv-drop-line" :style="{ top: `${indicatorTop}px` }" />
  </div>
</template>

<script setup lang="ts">
// Docked panels stacked top to bottom; splitters resize neighbours, and the drag controller
// (useDockDrag) inserts a view at the gap nearest the pointer. An overlay draws the leader lines
// between a selected request and its commands across panels.
import { VIEW_LABELS, type ViewMode } from '~/stores/ui';
import { steadyClock, type LinkAnchor, type LinkedSelection } from '~/composables/useTimeline';
import { PAPER } from '~/utils/theme';

/** Smallest panel height a splitter drag can leave (px). */
const MIN_PANEL_PX = 72;
/** A panel joining the stack opens over this long (ms). */
const OPEN_MS = 220;

const uiStore = useUIStore();
const drag = useDockDrag();
const tl = useTimeline();
const root = ref<HTMLElement | null>(null);
const links = ref<HTMLCanvasElement | null>(null);
const reduceMotion = import.meta.client && window.matchMedia('(prefers-reduced-motion: reduce)').matches;

/** Opening progress (0..1) of panels that just joined the stack. */
const opening = reactive<Partial<Record<ViewMode, number>>>({});

watch(() => [...uiStore.stack], (now, prev) => {
  // Only when the stack grows: a swap keeps its slot, so there is nothing to open.
  if (reduceMotion || !prev || now.length <= prev.length) return;
  for (const v of now) {
    if (prev.includes(v)) continue;
    const clock = steadyClock();
    opening[v] = 0.001;
    const step = (now: number) => {
      const t = Math.min(1, clock(now) / OPEN_MS);
      if (t >= 1) {
        delete opening[v];
        return;
      }
      opening[v] = Math.max(0.001, 1 - Math.pow(1 - t, 3));
      tl.frames.invalidate();
      requestAnimationFrame(step);
    };
    requestAnimationFrame(step);
  }
});

// ── Leader lines: a vertical line at each command's time, from its bar to its request's bar ──

let linksDrawn = false;
let removeLinks: (() => void) | null = null;

/** Drawn with the panels, after them, in every frame of the timeline's scheduler. */
function drawLinks() {
  const cv = links.value;
  const dock = root.value;
  if (!cv || !dock) return;
  const ctx = cv.getContext('2d');
  if (!ctx) return;
  const geo = tl.linkGeometry;
  const sel = tl.selection.value;
  const pv = tl.linkPreview.value;
  const groups: [LinkedSelection, string, number[]][] = [];
  if (pv && pv.req >= 0 && !(sel && sel.req === pv.req)) groups.push([pv, PAPER.muted, [3, 3]]);
  if (sel && sel.req >= 0) groups.push([sel, PAPER.accent, []]);
  // Lines need two panels to join: of Commands, Requests and Bank state.
  if ([geo.cmd, geo.req, geo.bank].filter(Boolean).length < 2 || groups.length === 0) {
    if (linksDrawn) {
      // Hide as well as clear: a clear queued in the frame that tears down a panel's WebGL context
      // can be dropped, which would leave stale leaders on screen.
      cv.style.visibility = 'hidden';
      ctx.setTransform(1, 0, 0, 1, 0, 0);
      ctx.clearRect(0, 0, cv.width, cv.height);
      linksDrawn = false;
    }
    return;
  }
  const rect = dock.getBoundingClientRect();
  const dpr = window.devicePixelRatio || 1;
  const w = Math.round(rect.width * dpr);
  const h = Math.round(rect.height * dpr);
  if (cv.width !== w || cv.height !== h) {
    cv.width = w;
    cv.height = h;
  }
  ctx.setTransform(dpr, 0, 0, dpr, -rect.left * dpr, -rect.top * dpr);
  ctx.clearRect(rect.left, rect.top, rect.width, rect.height);
  linksDrawn = true;
  cv.style.visibility = '';
  const clamp = (y: number, a: LinkAnchor) => Math.max(a.clipTop, Math.min(a.clipBottom, y));
  for (const [g, color, dash] of groups) {
    const r = geo.req?.(g) ?? null;
    for (const i of g.cmds) {
      const c = geo.cmd?.(i) ?? null;
      const b = geo.bank?.(i) ?? null;
      const at = c ?? b;
      if (!at) continue;
      const x = Math.round(at.x) + 0.5;
      // Only over the plots: a bar scrolled off the side would otherwise draw over the lane labels.
      if (x < at.clipLeft || x > at.clipRight) continue;
      // Join what is on screen of the request's bar, the command's bar and its bank marker, top to
      // bottom, at the command's time; the dot marks the request or, without it, the bank marker.
      const chain = [r, c, b].filter((a): a is LinkAnchor => a !== null).sort((p, q) => p.top - q.top);
      for (let k = 1; k < chain.length; k++) {
        const upper = chain[k - 1]!;
        const lower = chain[k]!;
        const y1 = clamp(upper.bottom + 2, upper);
        const y2 = clamp(lower.top - 2, lower);
        if (y2 - y1 < 1) continue;
        ctx.setLineDash(dash);
        ctx.lineWidth = 3;
        ctx.strokeStyle = '#ffffff';
        ctx.beginPath();
        ctx.moveTo(x, y1);
        ctx.lineTo(x, y2);
        ctx.stroke();
        ctx.lineWidth = 1;
        ctx.strokeStyle = color;
        ctx.stroke();
        ctx.setLineDash([]);
        const dotEnd = upper === r ? y1 : lower === r ? y2 : lower === b ? y2 : upper === b ? y1 : null;
        if (dotEnd === null) continue;
        ctx.fillStyle = color;
        ctx.beginPath();
        ctx.arc(x, dotEnd, 2.2, 0, Math.PI * 2);
        ctx.fill();
      }
    }
  }
}

function panelEls(): HTMLElement[] {
  return root.value ? Array.from(root.value.querySelectorAll<HTMLElement>('[data-dock-view]')) : [];
}

/** Insertion line for the current drop target (dock-relative y). */
const indicatorTop = computed(() => {
  const t = drag.state.target;
  if (!drag.state.active || t?.kind !== 'dock' || !root.value) return null;
  const top = root.value.getBoundingClientRect().top;
  const els = panelEls();
  if (els.length === 0) return 0;
  if (t.index < els.length) return Math.max(0, els[t.index]!.getBoundingClientRect().top - top - 2);
  return els[els.length - 1]!.getBoundingClientRect().bottom - top - 3;
});

/** Drag the splitter above panel `i`: trade height between panels i-1 and i, keeping their total. */
function startResize(i: number, e: PointerEvent) {
  if (e.button !== 0) return;
  const els = panelEls();
  const a = uiStore.stack[i - 1]!;
  const b = uiStore.stack[i]!;
  const ha = els[i - 1]!.getBoundingClientRect().height;
  const hb = els[i]!.getBoundingClientRect().height;
  const total = ha + hb;
  const weight = uiStore.weights[a] + uiStore.weights[b];
  if (total < 2 * MIN_PANEL_PX) return;
  const y0 = e.clientY;
  const handle = e.currentTarget as HTMLElement;
  handle.setPointerCapture(e.pointerId);
  e.preventDefault();

  const move = (ev: PointerEvent) => {
    const na = Math.max(MIN_PANEL_PX, Math.min(total - MIN_PANEL_PX, ha + ev.clientY - y0));
    uiStore.setWeights(a, (weight * na) / total, b, (weight * (total - na)) / total);
  };
  const up = () => {
    handle.removeEventListener('pointermove', move);
    handle.removeEventListener('pointerup', up);
    handle.removeEventListener('pointercancel', up);
    uiStore.persistLayout();
  };
  handle.addEventListener('pointermove', move);
  handle.addEventListener('pointerup', up);
  handle.addEventListener('pointercancel', up);
}

/** Double-click a splitter: give both neighbours the same height. */
function equalize(i: number) {
  const a = uiStore.stack[i - 1]!;
  const b = uiStore.stack[i]!;
  const w = (uiStore.weights[a] + uiStore.weights[b]) / 2;
  uiStore.setWeights(a, w, b, w);
  uiStore.persistLayout();
}

onMounted(() => {
  drag.setZone('dock', root.value);
  removeLinks = tl.frames.add(drawLinks);
});
onUnmounted(() => {
  drag.setZone('dock', null);
  removeLinks?.();
});
</script>

<style scoped>
.rv-dock {
  position: relative;
  height: 100%;
  display: flex;
  flex-direction: column;
  background: var(--rv-rule);
}

.rv-split {
  flex: none;
  height: 5px;
  background: var(--rv-rule);
  border-top: 1px solid var(--rv-rule-strong);
  cursor: row-resize;
  touch-action: none;
}

.rv-split:hover {
  background: var(--rv-rule-strong);
}

.rv-dock.dragging .rv-split {
  pointer-events: none;
}

.rv-links {
  position: absolute;
  inset: 0;
  width: 100%;
  height: 100%;
  pointer-events: none;
  z-index: 20;
}

.rv-drop-line {
  position: absolute;
  left: 0;
  right: 0;
  height: 4px;
  background: var(--rv-accent);
  pointer-events: none;
  z-index: 30;
}
</style>
