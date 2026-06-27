<template>
  <div
    ref="root"
    class="w-full bg-neutral-950 border-t border-neutral-800 select-none shrink-0"
    style="height: 52px"
  >
    <canvas ref="canvas" class="block w-full h-full" />
  </div>
</template>

<script setup lang="ts">
import type { ViewState } from '~/composables/useRenderer';

const props = defineProps<{
  viewState: ViewState;
  cursorTime?: number | null;
}>();
const emit = defineEmits<{ seek: [start: number, duration: number] }>();

const sessionStore = useSessionStore();
const uiStore = useUIStore();

const root = ref<HTMLDivElement | null>(null);
const canvas = ref<HTMLCanvasElement | null>(null);
let ctx: CanvasRenderingContext2D | null = null;
let prevCW = 0;
let prevCH = 0;

const TICK_H = 20;
const EDGE_GRAB = 10;
const DENSITY_BINS = 800;
const MIN_VP_PX = 24;

let density = new Float32Array(0);
let densityMax = 0;

const timelineNsPerCycle = computed(() => {
  const tCKPs = sessionStore.getTimingValue('tCK_ps');
  return tCKPs == null ? null : tCKPs / 1000;
});

function computeDensity() {
  const header = sessionStore.header;
  const arrays = sessionStore.arrays;
  if (!arrays || !header || header.numEntries === 0) return;

  const N = header.numEntries;
  const clk = arrays.clk;
  const minT = Number(clk[0]!);
  const maxT = Number(clk[N - 1]!);
  const span = maxT - minT;
  if (span <= 0) return;

  density = new Float32Array(DENSITY_BINS);
  const binW = span / DENSITY_BINS;

  for (let i = 0; i < N; i++) {
    const bin = Math.min(DENSITY_BINS - 1, Math.floor((Number(clk[i]!) - minT) / binW));
    density[bin]!++;
  }

  densityMax = 0;
  for (let i = 0; i < DENSITY_BINS; i++)
    if (density[i]! > densityMax) densityMax = density[i]!;
}

// ── Tick helpers ────────────────────────────────────────────────

function formatTickValue(value: number, step: number): string {
  const abs = Math.abs(value);
  if (abs >= 1e9 && step >= 1e8) return (value / 1e9).toFixed(1) + 'G';
  if (abs >= 1e6 && step >= 1e5) return (value / 1e6).toFixed(1) + 'M';
  if (abs >= 1e3 && step >= 1e2) return (value / 1e3).toFixed(1) + 'k';
  const decimals = step >= 1 ? 0 : Math.ceil(-Math.log10(step));
  return value.toFixed(decimals);
}

function formatTick(value: number, step: number, nsPerCycle: number | null): string {
  if (!uiStore.displayTimelineInNs || nsPerCycle == null)
    return formatTickValue(value, step);

  return `${formatTickValue(value * nsPerCycle, step * nsPerCycle)} ns`;
}

function niceStep(duration: number, width: number): number {
  const target = duration * (90 / width);
  const pow = Math.floor(Math.log10(target));
  const base = Math.pow(10, pow);
  for (const m of [1, 2, 5, 10]) {
    if (base * m >= target) return base * m;
  }
  return base * 10;
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
  const nsPerCycle = timelineNsPerCycle.value;

  // ── Tick ruler (top TICK_H px) ────────────────────────────────

  const step = niceStep(duration, w);
  const minorStep = step / 5;
  const startTick = Math.floor(start / minorStep) * minorStep;
  const endTick = start + duration;
  const eps = minorStep / 1000;

  ctx.beginPath();
  ctx.strokeStyle = '#3f3f46';
  ctx.lineWidth = 1;

  for (let t = startTick; t <= endTick + eps; t += minorStep) {
    if (t < minTime || t > maxTime) continue;
    const x = Math.round(((t - start) / duration) * w) + 0.5;
    const isMajor = Math.abs(t / step - Math.round(t / step)) < 1e-9;
    ctx.moveTo(x, 0);
    ctx.lineTo(x, isMajor ? 8 : 4);
  }
  ctx.stroke();

  ctx.textAlign = 'left';
  ctx.textBaseline = 'top';
  ctx.fillStyle = '#71717a';
  ctx.font = '10px "JetBrains Mono", monospace';

  const majorStart = Math.ceil(start / step) * step;
  for (let t = majorStart; t <= endTick + eps; t += step) {
    if (t < minTime || t > maxTime) continue;
    const x = ((t - start) / duration) * w;
    ctx.fillText(formatTick(t, step, nsPerCycle), x + 3, 9);
  }

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

  const grad = ctx.createLinearGradient(0, seekTop, 0, seekTop + chartH);
  grad.addColorStop(0, 'rgba(59, 130, 246, 0.30)');
  grad.addColorStop(1, 'rgba(59, 130, 246, 0.04)');
  ctx.fillStyle = grad;
  ctx.fill();

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
  ctx.fillStyle = 'rgba(0, 0, 0, 0.55)';
  if (vpLeft > 0) ctx.fillRect(0, seekTop, vpLeft, seekH);
  if (vpRight < w) ctx.fillRect(vpRight, seekTop, w - vpRight, seekH);

  // Viewport border.
  ctx.strokeStyle = '#f97316';
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
    ctx.fillStyle = '#f97316';
    ctx.fill();

    const gripCount = 3;
    const gripGap = 2.5;
    const gripTotalH = (gripCount - 1) * gripGap;
    const gripStartY = handleY + (handleH - gripTotalH) / 2;
    ctx.strokeStyle = 'rgba(0, 0, 0, 0.35)';
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
      ctx.fillStyle = '#f97316';
      ctx.fill();

      const gripCount = 3;
      const gripGap = 2.5;
      const gripTotalH = (gripCount - 1) * gripGap;
      const gripStartY = handleY + (handleH - gripTotalH) / 2;
      ctx.strokeStyle = 'rgba(0, 0, 0, 0.35)';
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
  ctx.fillStyle = '#27272a';
  ctx.fillRect(0, seekTop, w, 1);

  if (props.cursorTime != null && totalSpan > 0) {
    const x = ((props.cursorTime - minTime) / totalSpan) * w;
    if (x >= 0 && x <= w) {
      const sx = Math.round(x) + 0.5;
      ctx.fillStyle = 'rgba(251, 191, 36, 0.14)';
      ctx.fillRect(Math.max(0, sx - 2.5), TICK_H, 5, h - TICK_H);
      ctx.strokeStyle = 'rgba(251, 191, 36, 0.95)';
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
