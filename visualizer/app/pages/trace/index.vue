<template>
  <div class="rv-page">
    <main class="rv-main">
      <div class="rv-dock-wrap">
        <TraceDock />
        <div v-if="traceLoadStatus" class="rv-loading">
          <TraceLoadingStatus :message="traceLoadStatus" />
        </div>
      </div>
      <TraceTimeline
        :view-state="viewState"
        :cursor-time="cursorTime"
        @seek="(s: number, d: number) => { viewState.start = s; viewState.duration = d; }"
      />
    </main>

    <TraceInspector />

    <Teleport defer to="#rv-header-actions">
      <div v-if="isLiveStream || streamStatus === 'done'" class="rv-stream">
        <span class="rv-stream-dot" :class="{ live: isLiveStream }" />
        <span class="rv-mono">{{ streamEventCount.toLocaleString() }} cmd</span>
        <span v-if="!isLiveStream" class="rv-muted">(done)</span>
        <label v-if="isLiveStream" class="rv-stream-follow">
          <input v-model="followLive" type="checkbox">
          Follow live
        </label>
      </div>

      <div class="rv-cursor" :class="{ open: cursorMode }">
        <UTooltip :kbds="['C']" :text="cursorMode ? 'Cursor mode: click the timeline to place it.' : 'Enable cursor mode.'">
          <UButton
            icon="i-lucide-crosshair"
            label="Cursor"
            size="sm"
            color="neutral"
            :variant="cursorMode ? 'soft' : 'outline'"
            @click="toggleCursorMode"
          />
        </UTooltip>
        <template v-if="cursorMode">
          <span class="rv-mono rv-cursor-readout">
            {{ cursorTime != null ? `${formatCycles(cursorTime)} clk` : 'click to place' }}
          </span>
          <UTooltip text="Previous event">
            <UButton icon="i-lucide-skip-back" size="xs" color="neutral" variant="ghost" :disabled="!canJumpCursor" aria-label="Previous event" @click="jumpCursor('prev')" />
          </UTooltip>
          <UTooltip text="Next event">
            <UButton icon="i-lucide-skip-forward" size="xs" color="neutral" variant="ghost" :disabled="!canJumpCursor" aria-label="Next event" @click="jumpCursor('next')" />
          </UTooltip>
          <UTooltip text="Remove cursor">
            <UButton icon="i-lucide-x" size="xs" color="neutral" variant="ghost" :disabled="cursorTime == null" aria-label="Remove cursor" @click="clearCursor" />
          </UTooltip>
        </template>
      </div>

      <TraceSettingsPopover />
    </Teleport>
  </div>
</template>

<script setup lang="ts">
definePageMeta({ layout: 'trace' });

const traceLoadStatus = useTraceLoadStatus();
const { isLive: isLiveStream, eventCount: streamEventCount, status: streamStatus } = useStreamSession();
/** When true, viewport snaps to the latest cycles as streamed data grows. */
const followLive = ref(true);

// Shared by every docked panel, the overview strip and the inspector.
const {
  viewState,
  cursorMode,
  cursorTime,
  canJumpCursor,
  setCursorMode,
  clearCursor,
  jumpCursor,
  clearSelection,
} = createTimeline(followLive);

function toggleCursorMode() {
  setCursorMode(!cursorMode.value);
}

defineShortcuts({
  c: () => toggleCursorMode(),
  escape: () => clearSelection(),
});

function formatCycles(n: number): string {
  return Math.round(n).toLocaleString('en-US');
}
</script>

<style scoped>
.rv-page {
  flex: 1;
  min-width: 0;
  display: flex;
}

.rv-main {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  background: var(--rv-bg);
}

.rv-dock-wrap {
  position: relative;
  flex: 1;
  min-height: 0;
  overflow: hidden;
}

.rv-loading {
  position: absolute;
  inset: 0;
  z-index: 40;
  display: flex;
  align-items: center;
  justify-content: center;
  background: rgba(255, 255, 255, 0.92);
}

.rv-mono {
  font-family: var(--font-mono);
  font-size: 12px;
}

.rv-muted {
  color: var(--rv-muted);
}

.rv-stream {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 12px;
  color: var(--rv-muted);
  white-space: nowrap;
}

.rv-stream-dot {
  width: 8px;
  height: 8px;
  border-radius: 999px;
  background: var(--rv-rule-strong);
}

.rv-stream-dot.live {
  background: #1a7f37;
  animation: rv-pulse 1.2s ease-in-out infinite;
}

@keyframes rv-pulse {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.35; }
}

.rv-stream-follow {
  display: flex;
  align-items: center;
  gap: 4px;
  cursor: pointer;
  user-select: none;
}

.rv-cursor {
  display: flex;
  align-items: center;
  gap: 4px;
}

.rv-cursor-readout {
  padding: 0 6px;
  color: var(--rv-ink);
  white-space: nowrap;
}
</style>
