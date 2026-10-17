<template>
  <div class="rv-app">
    <header class="rv-header">
      <NuxtLink to="/" class="rv-brand" aria-label="RamViz home"><RamVizMark :size="22" />RamViz</NuxtLink>
      <span class="rv-divider" aria-hidden="true" />
      <span class="rv-trace" :title="traceTitle">{{ traceTitle }}</span>
      <TraceDockTabs />
      <div id="rv-header-actions" class="rv-actions" />
    </header>
    <div class="rv-body">
      <slot />
    </div>
    <div
      v-if="drag.state.active && drag.state.view"
      class="rv-ghost"
      :style="{ transform: `translate(${drag.state.x + 14}px, ${drag.state.y + 12}px)` }"
    >
      {{ VIEW_LABELS[drag.state.view] }}<span v-if="ghostHint" class="rv-ghost-hint">{{ ghostHint }}</span>
    </div>
  </div>
</template>

<script setup lang="ts">
import { VIEW_LABELS } from '~/stores/ui';
import { bankDetectFamily } from '~/utils/bankState';

const uiStore = useUIStore();
const sessionStore = useSessionStore();
const drag = useDockDrag();

// Restore the saved panel layout before the dock renders (trace routes render client-side only).
uiStore.hydrateSettings();

/** Standard name: the trace's dram_type when it is a plain identifier, else detected from its names. */
const standardName = computed(() => {
  const t = sessionStore.header?.dramType ?? '';
  if (/^[A-Za-z0-9_]+$/.test(t)) return t;
  const spec = sessionStore.spec;
  if (!spec) return '';
  const f = bankDetectFamily(spec);
  return f === 'HBM3' ? 'HBM3/4' : f === 'unknown' ? '' : f;
});

const traceTitle = computed(() => {
  const parts = [sessionStore.traceName, standardName.value, `ch${sessionStore.header?.channelId ?? 0}`];
  return parts.filter(Boolean).join(' · ');
});

const ghostHint = computed(() => {
  const t = drag.state.target;
  if (!t) return '';
  return t.kind === 'park' ? 'Park as tab' : 'Dock here';
});
</script>

<style scoped>
.rv-app {
  height: 100vh;
  display: flex;
  flex-direction: column;
  background: var(--rv-bg);
  color: var(--rv-ink);
  font-size: 13px;
}

.rv-header {
  flex: none;
  display: flex;
  align-items: stretch;
  gap: 0 16px;
  min-height: 44px;
  padding: 0 12px 0 16px;
  background: var(--rv-bg);
  border-bottom: 1px solid var(--rv-rule);
}

.rv-brand {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 15px;
  font-weight: 600;
  letter-spacing: -0.01em;
  color: var(--rv-ink);
  text-decoration: none;
}

.rv-divider {
  align-self: center;
  width: 1px;
  height: 18px;
  background: var(--rv-rule);
}

.rv-trace {
  align-self: center;
  min-width: 0;
  max-width: 420px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  font-family: var(--font-mono);
  font-size: 12px;
  color: var(--rv-muted);
}

.rv-actions {
  margin-left: auto;
  display: flex;
  align-items: center;
  gap: 8px;
  min-width: 0;
}

.rv-body {
  flex: 1;
  min-height: 0;
  display: flex;
}

.rv-ghost {
  position: fixed;
  left: 0;
  top: 0;
  z-index: 100;
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 4px 10px;
  border: 1px solid var(--rv-rule-strong);
  background: var(--rv-bg);
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.12);
  font-size: 12px;
  font-weight: 500;
  pointer-events: none;
  white-space: nowrap;
}

.rv-ghost-hint {
  color: var(--rv-muted);
  font-weight: 400;
}
</style>
