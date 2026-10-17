<template>
  <nav
    ref="root"
    class="rv-tabs"
    :class="{ 'park-zone': parkable, 'park-hot': drag.state.target?.kind === 'park' }"
    aria-label="Views"
  >
    <button
      v-for="v in VIEW_ORDER"
      :key="v"
      type="button"
      class="rv-tab"
      :class="{ docked: uiStore.stack.includes(v), active: uiStore.activeView === v, lifted: drag.state.active && drag.state.view === v }"
      :aria-pressed="uiStore.stack.includes(v)"
      :title="uiStore.stack.includes(v)
        ? 'Shown in the panel area. Drag here from a panel title to park it.'
        : 'Parked. Click to show in place of the active panel, or drag into the panel area to stack it.'"
      @pointerdown="drag.start(v, 'tab', $event)"
      @click="onClick(v)"
    >
      {{ VIEW_LABELS[v] }}
    </button>
  </nav>
</template>

<script setup lang="ts">
// Header tab strip: every view, docked ones marked. Also the drop zone that parks a panel.
import { VIEW_LABELS, VIEW_ORDER, type ViewMode } from '~/stores/ui';

const uiStore = useUIStore();
const drag = useDockDrag();
const root = ref<HTMLElement | null>(null);

/** A docked panel is being dragged by its title and could be parked here. */
const parkable = computed(() => drag.state.active && drag.state.source === 'panel' && !!drag.state.view
  && uiStore.stack.includes(drag.state.view) && uiStore.stack.length > 1);

function onClick(v: ViewMode) {
  if (drag.consumeClick()) return;
  uiStore.showView(v);
}

onMounted(() => drag.setZone('park', root.value));
onUnmounted(() => drag.setZone('park', null));
</script>

<style scoped>
.rv-tabs {
  display: flex;
  align-items: stretch;
  gap: 2px;
  margin-left: 8px;
  padding: 0 4px;
  outline: 1px dashed transparent;
  outline-offset: -6px;
}

.rv-tabs.park-zone {
  outline-color: var(--rv-rule-strong);
}

.rv-tabs.park-hot {
  outline: 1px solid var(--rv-accent);
  background: color-mix(in srgb, var(--rv-accent) 6%, transparent);
}

.rv-tab {
  padding: 0 12px;
  border: 0;
  border-bottom: 2px solid transparent;
  background: transparent;
  color: var(--rv-muted);
  font: inherit;
  font-weight: 500;
  cursor: pointer;
  white-space: nowrap;
  touch-action: none;
  user-select: none;
}

.rv-tab:hover {
  color: var(--rv-ink);
}

.rv-tab.docked {
  color: var(--rv-ink);
  border-bottom-color: var(--rv-rule-strong);
}

.rv-tab.docked.active {
  border-bottom-color: var(--rv-accent);
}

.rv-tab.lifted {
  opacity: 0.5;
}
</style>
