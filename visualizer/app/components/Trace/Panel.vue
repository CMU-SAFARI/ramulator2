<template>
  <section
    class="rv-panel"
    :class="{ active: uiStore.activeView === view, lifted: drag.state.active && drag.state.view === view }"
    :data-dock-view="view"
    :aria-label="VIEW_LABELS[view]"
    @pointerenter="onEnter"
    @pointerleave="cancelEnter"
    @pointerdown="activate"
  >
    <header class="rv-panel-head" title="Drag to move this panel, or onto the tab bar to park it" @pointerdown="onHeadPointerDown">
      <svg class="rv-grip" viewBox="0 0 6 10" aria-hidden="true">
        <circle v-for="(p, i) in GRIP" :key="i" :cx="p[0]" :cy="p[1]" r="0.9" />
      </svg>
      <span class="rv-panel-title">{{ VIEW_LABELS[view] }}</span>
      <button
        v-if="uiStore.stack.length > 1"
        type="button"
        class="rv-panel-btn"
        :aria-label="`Park ${VIEW_LABELS[view]} as a tab`"
        title="Park as tab"
        @click="uiStore.park(view)"
      >
        <svg viewBox="0 0 12 12" aria-hidden="true"><path d="M2.5 6h7" /></svg>
      </button>
    </header>
    <div class="rv-panel-body">
      <TracePanelTree :view="view" />
      <div class="rv-canvas-wrap">
        <canvas ref="canvas" class="block h-full w-full" :class="clickable ? 'cursor-pointer' : 'cursor-crosshair'" />
        <canvas ref="labelCanvas" class="pointer-events-none absolute inset-0 h-full w-full" aria-hidden="true" />
      </div>
    </div>
  </section>
</template>

<script setup lang="ts">
import { VIEW_LABELS, type ViewMode } from '~/stores/ui';

const props = defineProps<{ view: ViewMode }>();

const GRIP = [[1.5, 2], [4.5, 2], [1.5, 5], [4.5, 5], [1.5, 8], [4.5, 8]] as const;

const uiStore = useUIStore();
const drag = useDockDrag();
const canvas = ref<HTMLCanvasElement | null>(null);
const labelCanvas = ref<HTMLCanvasElement | null>(null);
const { clickable } = useRenderer(canvas, labelCanvas, props.view);

/** The inspector follows the panel the pointer rests on; passing over a panel doesn't switch it. */
const HOVER_INTENT_MS = 150;
let enterTimer: ReturnType<typeof setTimeout> | null = null;

function cancelEnter() {
  if (enterTimer) clearTimeout(enterTimer);
  enterTimer = null;
}

function activate() {
  cancelEnter();
  uiStore.setActiveView(props.view);
}

function onEnter() {
  cancelEnter();
  enterTimer = setTimeout(activate, HOVER_INTENT_MS);
}

onUnmounted(cancelEnter);

function onHeadPointerDown(e: PointerEvent) {
  if ((e.target as HTMLElement).closest('button')) return;
  e.preventDefault(); // no text selection while dragging
  drag.start(props.view, 'panel', e);
}
</script>

<style scoped>
.rv-panel {
  position: relative;
  display: flex;
  flex-direction: column;
  min-height: 72px;
  flex-basis: 0;
  flex-shrink: 1;
  background: var(--rv-bg);
}

.rv-panel-head {
  flex: none;
  display: flex;
  align-items: center;
  gap: 8px;
  height: 26px;
  padding: 0 6px 0 8px;
  background: var(--rv-side);
  border-bottom: 1px solid var(--rv-rule);
  cursor: grab;
  user-select: none;
}

.rv-grip {
  width: 6px;
  height: 10px;
  fill: var(--rv-rule-strong);
}

.rv-panel-title {
  font-size: 11px;
  font-weight: 600;
  letter-spacing: 0.06em;
  text-transform: uppercase;
  color: var(--rv-muted);
}

.rv-panel.active .rv-panel-title {
  color: var(--rv-ink);
}

.rv-panel.active .rv-panel-head {
  box-shadow: inset 2px 0 0 var(--rv-accent);
}

.rv-panel.lifted {
  opacity: 0.55;
}

.rv-panel-btn {
  margin-left: auto;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 20px;
  height: 20px;
  padding: 0;
  border: 0;
  border-radius: 3px;
  background: transparent;
  color: var(--rv-muted);
  cursor: pointer;
}

.rv-panel-btn:hover {
  background: var(--rv-hover);
  color: var(--rv-ink);
}

.rv-panel-btn svg {
  width: 12px;
  height: 12px;
  fill: none;
  stroke: currentColor;
  stroke-width: 1.4;
  stroke-linecap: round;
}

.rv-panel-body {
  flex: 1;
  min-height: 0;
  display: flex;
}

.rv-canvas-wrap {
  position: relative;
  flex: 1;
  min-width: 0;
  overflow: hidden;
}
</style>
