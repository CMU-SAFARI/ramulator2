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

    <TraceTutorial />

    <Teleport defer to="#rv-header-actions">
      <div v-if="readingNote" class="rv-stream" role="status">
        <span class="rv-mono">{{ readingNote }}</span>
      </div>
      <div v-if="stream.sessionId.value" class="rv-stream" data-tour="live">
        <span class="rv-stream-dot" :class="stream.connected.value ? stream.status.value : 'lost'" />
        <span class="rv-mono">{{ stream.eventCount.value.toLocaleString() }} cmd</span>
        <span class="rv-muted">{{ streamNote }}</span>
        <label v-if="isLiveStream" class="rv-stream-follow">
          <input v-model="followLive" type="checkbox">
          Follow live
        </label>
        <UTooltip v-if="isLiveStream" text="Stop streaming this run. The simulation keeps running.">
          <UButton size="xs" color="neutral" variant="outline" icon="i-lucide-square" label="Stop" @click="stream.stop()" />
        </UTooltip>
      </div>
      <UButton
        v-if="newerSession"
        size="xs"
        color="neutral"
        variant="soft"
        icon="i-lucide-radio"
        :label="`New live session: ${newerSession.dramType} channel ${newerSession.channel}`"
        @click="openNewerSession"
      />

      <UTooltip :kbds="['G']" text="Go to a clock (also 123 ns, 1.5 us). Keys: W/S zoom, A/D pan, Home/End.">
        <UInput
          id="rv-goto"
          v-model="goToText"
          size="sm"
          icon="i-lucide-locate"
          placeholder="Go to clock"
          aria-label="Go to clock"
          class="rv-goto"
          @keydown.enter="goToClock"
          @keydown.escape="($event.target as HTMLInputElement).blur()"
        />
      </UTooltip>

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
            {{ cursorTime != null ? cursorLabel(cursorTime) : 'click to place' }}
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

      <UButton
        icon="i-lucide-graduation-cap"
        label="Tutorial"
        size="sm"
        color="neutral"
        :variant="tutorial.open.value ? 'soft' : 'outline'"
        @click="tutorial.open.value ? tutorial.close() : tutorial.start()"
      />

      <TraceSettingsPopover />
    </Teleport>
  </div>
</template>

<script setup lang="ts">
definePageMeta({ layout: 'trace' });

// Opened directly or reloaded with nothing loaded: the start page loads a trace or joins a live session.
const sessionStore = useSessionStore();
if (!sessionStore.isLoaded) void navigateTo('/', { replace: true });

const traceLoadStatus = useTraceLoadStatus();
const stream = useStreamSession();
const isLiveStream = stream.isLive;
/** When true, viewport snaps to the latest cycles as streamed data grows; navigating turns it off. */
const followLive = ref(true);
const toast = useToast();
const tutorial = useTutorial();

const streamNote = computed(() => {
  // Without the connection to the visualizer, the session's own state is out of date.
  const notes = [stream.connected.value ? {
    connecting: 'connecting', live: 'live', done: 'finished', stopped: 'stopped', lost: 'no contact with the simulation',
    gone: 'ended', idle: '',
  }[stream.status.value] : 'no connection to the visualizer, reconnecting'];
  if (stream.full.value) notes.push(stream.full.value);
  if (stream.dropped.value) notes.push(`first ${stream.dropped.value.toLocaleString()} commands not kept`);
  return notes.filter(Boolean).join(' · ');
});

/** A live session that started after the one (or the file) shown here. */
const shownSince = Date.now();
const newerSession = computed(() => {
  const since = stream.session.value?.startedAt ?? shownSince;
  return stream.sessions.value
    .filter(s => s.state === 'live' && s.id !== stream.sessionId.value && s.startedAt > since)
    .sort((a, b) => b.startedAt - a.startedAt || a.channel - b.channel)[0] ?? null;
});

async function openNewerSession() {
  const s = newerSession.value;
  if (!s) return;
  try {
    await stream.open(s.id);
    followLive.value = true;
  } catch (e: any) {
    toast.add({ color: 'error', title: 'Could not open the live session', description: e.message ?? String(e) });
  }
}

onMounted(() => stream.connect());

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
  load,
  setView,
  placeCursor,
} = createTimeline(followLive);

// The viewer opens on the first tiles; the rest of the file is read in the background.
const readingNote = computed(() => {
  const l = load.value;
  if (!l || l.done || l.error || !l.total || stream.sessionId.value) return '';
  return `Reading ${Math.floor((100 * l.read) / l.total)}% · ${l.read.toLocaleString()} of ${l.total.toLocaleString()} commands`;
});

watch(load, (l, prev) => {
  if (!l) return;
  if (l.ready || l.error) traceLoadStatus.value = null;
  if (l.error && l.error !== prev?.error) {
    toast.add({ color: 'error', title: 'The trace could not be read', description: l.error });
  }
});

function toggleCursorMode() {
  setCursorMode(!cursorMode.value);
}

// Keyboard navigation, as in other trace viewers: W/S (or +/−) zoom around the cursor or the middle
// of the view, A/D (or arrows) pan, Home/End jump to the start or end, G goes to a clock.
const ZOOM_STEP = 1.5;
const PAN_STEP = 0.2;

function zoomBy(factor: number) {
  const { start, duration } = viewState;
  const c = cursorTime.value;
  const at = c != null && c >= start && c <= start + duration ? c : start + duration / 2;
  const d = duration * factor;
  setView(at - ((at - start) / duration) * d, d);
}

function panBy(fraction: number) {
  setView(viewState.start + fraction * viewState.duration, viewState.duration);
}

defineShortcuts({
  c: () => toggleCursorMode(),
  w: () => zoomBy(1 / ZOOM_STEP),
  s: () => zoomBy(ZOOM_STEP),
  '+': () => zoomBy(1 / ZOOM_STEP),
  '=': () => zoomBy(1 / ZOOM_STEP),
  '-': () => zoomBy(ZOOM_STEP),
  a: () => panBy(-PAN_STEP),
  d: () => panBy(PAN_STEP),
  arrowleft: () => panBy(-PAN_STEP),
  arrowright: () => panBy(PAN_STEP),
  home: () => setView(viewState.minTime, viewState.duration),
  end: () => setView(viewState.maxTime - viewState.duration, viewState.duration),
  g: () => document.getElementById('rv-goto')?.focus(),
});

// ── Go to a clock ───────────────────────────────────────────────

const goToText = ref('');
const NS_PER = { ns: 1, us: 1e3, µs: 1e3, ms: 1e6 } as const;

/** A clock as people type it: 8,590,018,300 · 8.59e9 · 8590018300 clk · 1234.5 ns · 1.5 us. */
function parseClock(text: string): number | null {
  const m = /^([0-9][0-9,_' ]*(?:\.[0-9]+)?(?:e[0-9]+)?)\s*(clk|ns|us|µs|ms)?$/i.exec(text.trim());
  if (!m) return null;
  const value = Number(m[1]!.replace(/[,_' ]/g, ''));
  const unit = (m[2] ?? 'clk').toLowerCase() as 'clk' | keyof typeof NS_PER;
  if (!Number.isFinite(value)) return null;
  if (unit === 'clk') return value;
  const tck = sessionStore.getTimingValue('tCK_ps');
  return tck ? (value * NS_PER[unit] * 1000) / tck : null;
}

/** Centre the view on the clock typed in the Go-to field and put the cursor there. */
function goToClock(event: KeyboardEvent) {
  const t = parseClock(goToText.value);
  if (t === null) {
    toast.add({ color: 'warning', title: 'Not a clock', description: 'Type a clock such as 8,590,018,300, or a time such as 1234.5 ns or 1.5 us.' });
    return;
  }
  if (t < viewState.minTime || t > viewState.maxTime) {
    toast.add({
      color: 'warning',
      title: 'Outside the trace',
      description: `The trace runs from clock ${formatCycles(viewState.minTime)} to ${formatCycles(viewState.maxTime)}.`,
    });
    return;
  }
  setView(t - viewState.duration / 2, viewState.duration);
  setCursorMode(true);
  placeCursor(t, uiStore.activeView);
  (event.target as HTMLInputElement).blur();
}

// Escape clears the selection, unless an open popover or menu took it first. Not a shortcut:
// shortcuts mark the key handled, which keeps it from closing popovers.
function onEscape(e: KeyboardEvent) {
  if (e.key === 'Escape' && !e.defaultPrevented) clearSelection();
}
onMounted(() => window.addEventListener('keydown', onEscape));
onUnmounted(() => window.removeEventListener('keydown', onEscape));

const uiStore = useUIStore();

/** The cursor's time, in ns as well when the time axis shows ns. */
function cursorLabel(t: number): string {
  const tck = sessionStore.getTimingValue('tCK_ps');
  const clk = `${formatCycles(t)} clk`;
  if (!uiStore.displayTimelineInNs || !tck) return clk;
  return `${((t * tck) / 1000).toLocaleString('en-US', { maximumFractionDigits: 2 })} ns · ${clk}`;
}

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

.rv-stream-dot.lost {
  background: #b35900;
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

.rv-goto {
  width: 150px;
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
