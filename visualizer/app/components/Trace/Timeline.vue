<template>
  <div class="rv-timeline">
    <!-- Same width as the panels' lane labels, so the axis lines up with their plots. -->
    <div class="rv-timeline-gutter" />
    <div ref="root" class="rv-timeline-track">
      <canvas ref="canvas" class="block w-full h-full" />
      <TraceUnread :start="viewState.minTime" :end="viewState.maxTime" :style="{ top: `${TICK_H}px` }" />
    </div>
  </div>
</template>

<script setup lang="ts">
import type { ViewState } from '~/composables/useTimeline';
import { density as traceDensity } from '~/engine/stats';
import { PAPER } from '~/utils/theme';
import { axisTicks } from '~/utils/timeTicks';

const C = PAPER.overview;

const props = defineProps<{
  viewState: ViewState;
  cursorTime?: number | null;
}>();
const emit = defineEmits<{ seek: [start: number, duration: number] }>();

const sessionStore = useSessionStore();
const uiStore = useUIStore();
const tl = useTimeline();

const root = ref<HTMLDivElement | null>(null);
const canvas = ref<HTMLCanvasElement | null>(null);
let ctx: CanvasRenderingContext2D | null = null;
let prevCW = 0;
let prevCH = 0;

const TICK_H = 20;
const EDGE_GRAB = 10;
const DENSITY_BINS = 800;
const MIN_VP_PX = 24;

let density: Float32Array = new Float32Array(0);
let densityMax = 0;

const timelineNsPerCycle = computed(() => {
  const tCKPs = sessionStore.getTimingValue('tCK_ps');
  return tCKPs == null ? null : tCKPs / 1000;
});

/** Commands per strip bin over the whole trace, from the tile summaries. */
function computeDensity() {
  density = new Float32Array(0);
  densityMax = 0;
  const e = tl.engine.value;
  if (!e || e.tiles.length === 0) return;
  const { minTime, maxTime } = props.viewState;
  if (!(maxTime > minTime)) return;
  density = traceDensity(e.tiles, minTime, maxTime, DENSITY_BINS);
  for (let i = 0; i < DENSITY_BINS; i++) if (density[i]! > densityMax) densityMax = density[i]!;
}

// ── Drawing ─────────────────────────────────────────────────────

function draw() {
  if (!canvas.value || !root.value) return;
  if (!ctx) ctx = canvas.value.getContext('2d');
  if (!ctx) return;

  const dpr = window.devicePixelRatio || 1;
  const w = root.value.clientWidth;
  const h = root.value.clientHeight;
  const cw = Math.round(w * dpr);
  const ch = Math.round(h * dpr);

  if (prevCW !== cw || prevCH !== ch) {
    canvas.value.width = cw;
    canvas.value.height = ch;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    prevCW = cw;
    prevCH = ch;
  }

  ctx.clearRect(0, 0, w, h);

  const { start, duration, minTime, maxTime } = props.viewState;
  const totalSpan = maxTime - minTime;

  // ── Tick ruler (top TICK_H px) ────────────────────────────────

  const ticks = axisTicks(start, duration, w, uiStore.displayTimelineInNs ? timelineNsPerCycle.value : null, minTime, maxTime);
  const xOf = (t: number) => Math.round(((t - start) / duration) * w) + 0.5;

  ctx.beginPath();
  ctx.strokeStyle = C.tick;
  ctx.lineWidth = 1;
  for (const t of ticks.minor) {
    ctx.moveTo(xOf(t), 0);
    ctx.lineTo(xOf(t), 4);
  }
  for (const t of ticks.major) {
    ctx.moveTo(xOf(t), 0);
    ctx.lineTo(xOf(t), 8);
  }
  ctx.stroke();

  ctx.textAlign = 'left';
  ctx.textBaseline = 'top';
  ctx.fillStyle = C.label;
  ctx.font = '10px "IBM Plex Mono", monospace';

  ticks.major.forEach((t, i) => {
    const x = ((t - start) / duration) * w;
    const label = ticks.labels[i]!;
    // A label that would be cut off at the right edge is left out.
    if (x + 3 + ctx!.measureText(label).width <= w) ctx!.fillText(label, x + 3, 9);
  });

  // ── Density chart + viewport selector (below tick ruler) ──────

  const seekTop = TICK_H;
  const seekH = h - TICK_H;

  if (density.length === 0 || densityMax === 0 || totalSpan <= 0) return;

  const bins = density.length;
  const chartH = seekH - 1;

  ctx.beginPath();
  ctx.moveTo(0, seekTop + chartH);
  for (let b = 0; b < bins; b++) {
    const x = (b / bins) * w;
    const barH = (density[b]! / densityMax) * chartH * 0.85;
    ctx.lineTo(x, seekTop + chartH - barH);
  }
  ctx.lineTo(w, seekTop + chartH);
  ctx.closePath();

  ctx.fillStyle = C.fill;
  ctx.fill();
  ctx.strokeStyle = C.line;
  ctx.lineWidth = 1;
  ctx.stroke();

  // Compute viewport pixel bounds, enforcing a minimum rendered width.
  const rawVpLeft = ((start - minTime) / totalSpan) * w;
  const rawVpRight = ((start + duration - minTime) / totalSpan) * w;
  const rawVpW = rawVpRight - rawVpLeft;
  const narrow = rawVpW < MIN_VP_PX;

  let vpLeft: number, vpRight: number;
  if (narrow) {
    const center = Math.max(MIN_VP_PX / 2, Math.min(w - MIN_VP_PX / 2, (rawVpLeft + rawVpRight) / 2));
    vpLeft = center - MIN_VP_PX / 2;
    vpRight = center + MIN_VP_PX / 2;
  } else {
    vpLeft = Math.max(0, rawVpLeft);
    vpRight = Math.min(w, rawVpRight);
  }

  // Dim outside viewport.
  ctx.fillStyle = C.dim;
  if (vpLeft > 0) ctx.fillRect(0, seekTop, vpLeft, seekH);
  if (vpRight < w) ctx.fillRect(vpRight, seekTop, w - vpRight, seekH);

  // Viewport border.
  ctx.strokeStyle = C.window;
  ctx.lineWidth = 1.5;
  ctx.strokeRect(
    Math.round(vpLeft) + 0.5,
    seekTop + 0.5,
    Math.round(vpRight - vpLeft) - 1,
    seekH - 1,
  );

  const handleH = Math.min(18, seekH - 4);
  const handleY = seekTop + (seekH - handleH) / 2;
  const handleR = 3;

  if (narrow) {
    // Single centered pill — pan-only when viewport is too small for edge handles.
    const pillW = Math.min(16, Math.round(vpRight - vpLeft) - 2);
    const pillX = (vpLeft + vpRight - pillW) / 2;

    ctx.beginPath();
    ctx.roundRect(pillX, handleY, pillW, handleH, handleR);
    ctx.fillStyle = C.handle;
    ctx.fill();

    const gripCount = 3;
    const gripGap = 2.5;
    const gripTotalH = (gripCount - 1) * gripGap;
    const gripStartY = handleY + (handleH - gripTotalH) / 2;
    ctx.strokeStyle = C.grip;
    ctx.lineWidth = 1;
    ctx.beginPath();
    for (let g = 0; g < gripCount; g++) {
      const gy = Math.round(gripStartY + g * gripGap) + 0.5;
      ctx.moveTo(pillX + 2.5, gy);
      ctx.lineTo(pillX + pillW - 2.5, gy);
    }
    ctx.stroke();
  } else {
    // Two edge handles with grip lines.
    const handleW = 8;
    for (const hx of [Math.round(vpLeft) - 1, Math.round(vpRight) - handleW + 1]) {
      ctx.beginPath();
      ctx.roundRect(hx, handleY, handleW, handleH, handleR);
      ctx.fillStyle = C.handle;
      ctx.fill();

      const gripCount = 3;
      const gripGap = 2.5;
      const gripTotalH = (gripCount - 1) * gripGap;
      const gripStartY = handleY + (handleH - gripTotalH) / 2;
      ctx.strokeStyle = C.grip;
      ctx.lineWidth = 1;
      ctx.beginPath();
      for (let g = 0; g < gripCount; g++) {
        const gy = Math.round(gripStartY + g * gripGap) + 0.5;
        ctx.moveTo(hx + 2.5, gy);
        ctx.lineTo(hx + handleW - 2.5, gy);
      }
      ctx.stroke();
    }
  }

  // Separator line between tick ruler and seek area.
  ctx.fillStyle = PAPER.overview.tick;
  ctx.fillRect(0, seekTop, w, 1);

  if (props.cursorTime != null && totalSpan > 0) {
    const x = ((props.cursorTime - minTime) / totalSpan) * w;
    if (x >= 0 && x <= w) {
      const sx = Math.round(x) + 0.5;
      ctx.fillStyle = C.cursorFill;
      ctx.fillRect(Math.max(0, sx - 2.5), TICK_H, 5, h - TICK_H);
      ctx.strokeStyle = C.cursor;
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(sx, 0);
      ctx.lineTo(sx, h);
      ctx.stroke();
    }
  }
}

// ── Seek interaction ────────────────────────────────────────────

type DragMode = 'none' | 'pan' | 'resize-left' | 'resize-right';
let dragMode: DragMode = 'none';
let dragOriginX = 0;
let dragOriginStart = 0;
let dragOriginDur = 0;

function vpRender(w: number) {
  const { start, duration, minTime, maxTime } = props.viewState;
  const span = maxTime - minTime;
  if (span <= 0) return { left: 0, right: w, narrow: false };
  const rawLeft = ((start - minTime) / span) * w;
  const rawRight = ((start + duration - minTime) / span) * w;
  if (rawRight - rawLeft >= MIN_VP_PX)
    return { left: rawLeft, right: rawRight, narrow: false };
  const center = Math.max(MIN_VP_PX / 2, Math.min(w - MIN_VP_PX / 2, (rawLeft + rawRight) / 2));
  return { left: center - MIN_VP_PX / 2, right: center + MIN_VP_PX / 2, narrow: true };
}

function pxToTime(px: number, w: number): number {
  const { minTime, maxTime } = props.viewState;
  return minTime + (px / w) * (maxTime - minTime);
}

function clampStart(s: number, dur: number): number {
  return Math.max(props.viewState.minTime, Math.min(s, props.viewState.maxTime - dur));
}

function clampDur(d: number): number {
  return Math.max(props.viewState.minDuration, Math.min(d, props.viewState.maxDuration));
}

function onPointerDown(e: PointerEvent) {
  const el = root.value;
  if (!el) return;
  const rect = el.getBoundingClientRect();
  const x = e.clientX - rect.left;
  const y = e.clientY - rect.top;
  const w = rect.width;

  if (y < TICK_H) return;

  const vp = vpRender(w);

  if (vp.narrow) {
    // When zoomed in far enough that handles would overlap,
    // the whole viewport pill is a pan handle — no edge resize.
    if (x >= vp.left && x <= vp.right) {
      dragMode = 'pan';
    } else {
      const t = pxToTime(x, w);
      const dur = props.viewState.duration;
      emit('seek', clampStart(t - dur / 2, dur), dur);
      dragMode = 'pan';
    }
  } else {
    if (Math.abs(x - vp.left) <= EDGE_GRAB) {
      dragMode = 'resize-left';
    } else if (Math.abs(x - vp.right) <= EDGE_GRAB) {
      dragMode = 'resize-right';
    } else if (x >= vp.left && x <= vp.right) {
      dragMode = 'pan';
    } else {
      const t = pxToTime(x, w);
      const dur = props.viewState.duration;
      emit('seek', clampStart(t - dur / 2, dur), dur);
      dragMode = 'pan';
    }
  }

  dragOriginX = e.clientX;
  dragOriginStart = props.viewState.start;
  dragOriginDur = props.viewState.duration;

  el.setPointerCapture(e.pointerId);
  e.preventDefault();
}

function onPointerMove(e: PointerEvent) {
  const el = root.value;
  if (!el) return;
  const w = el.clientWidth;

  // Update cursor when not dragging.
  if (dragMode === 'none') {
    const rect = el.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;

    if (y < TICK_H) {
      el.style.cursor = 'default';
      return;
    }
    const vp = vpRender(w);
    if (vp.narrow) {
      el.style.cursor = (x >= vp.left && x <= vp.right) ? 'grab' : 'pointer';
    } else if (Math.abs(x - vp.left) <= EDGE_GRAB || Math.abs(x - vp.right) <= EDGE_GRAB) {
      el.style.cursor = 'ew-resize';
    } else if (x >= vp.left && x <= vp.right) {
      el.style.cursor = 'grab';
    } else {
      el.style.cursor = 'pointer';
    }
    return;
  }

  // Active drag.
  el.style.cursor = dragMode === 'pan' ? 'grabbing' : 'ew-resize';
  const totalSpan = props.viewState.maxTime - props.viewState.minTime;
  if (totalSpan <= 0) return;
  const dt = ((e.clientX - dragOriginX) / w) * totalSpan;

  if (dragMode === 'pan') {
    emit('seek', clampStart(dragOriginStart + dt, dragOriginDur), dragOriginDur);
  } else if (dragMode === 'resize-left') {
    const raw = clampDur(dragOriginDur - dt);
    const newStart = dragOriginStart + dragOriginDur - raw;
    emit('seek', Math.max(props.viewState.minTime, newStart), raw);
  } else if (dragMode === 'resize-right') {
    emit('seek', clampStart(dragOriginStart, clampDur(dragOriginDur + dt)), clampDur(dragOriginDur + dt));
  }
}

function onPointerUp() {
  dragMode = 'none';
}

// ── Lifecycle ───────────────────────────────────────────────────

watchEffect(draw);

// Tiles arrive while a trace loads or streams, and a new trace replaces them: keep the strip current.
watch(() => [tl.dataVersion.value, tl.engine.value], () => {
  computeDensity();
  draw();
});

let resizeObs: ResizeObserver | undefined;

onMounted(() => {
  computeDensity();
  if (root.value) {
    resizeObs = new ResizeObserver(draw);
    resizeObs.observe(root.value);
    root.value.addEventListener('pointerdown', onPointerDown);
    root.value.addEventListener('pointermove', onPointerMove);
    root.value.addEventListener('pointerup', onPointerUp);
    root.value.addEventListener('lostpointercapture', onPointerUp);
  }
});

onUnmounted(() => {
  resizeObs?.disconnect();
});
</script>

<style scoped>
.rv-timeline {
  display: flex;
  flex-shrink: 0;
  width: 100%;
  height: 56px;
  user-select: none;
  background: var(--rv-bg);
  border-top: 1px solid var(--rv-rule);
}

.rv-timeline-gutter {
  flex: none;
  width: var(--rv-tree-w);
  background: var(--rv-side);
  border-right: 1px solid var(--rv-rule);
}

.rv-timeline-track {
  position: relative;
  flex: 1;
  min-width: 0;
}
</style>
