<template>
  <main class="rv-home">
    <div class="rv-col">
      <header>
        <h1 class="rv-brand"><RamVizMark :size="36" />RamViz</h1>
        <p class="rv-sub">DRAM command and bank-state visualizer for Ramulator traces</p>
      </header>

      <div v-if="tutorialExample" class="rv-start-tour">
        <span>New to RamViz? The tutorial walks you through an example trace.</span>
        <UButton
          size="xs"
          color="neutral"
          variant="outline"
          icon="i-lucide-graduation-cap"
          label="Start the tutorial"
          class="shrink-0"
          :loading="startingTutorial"
          @click="startTutorial"
        />
      </div>

      <section v-if="liveSessions.length" aria-label="Live sessions" class="rv-live">
        <h2 class="rv-h">Live sessions</h2>
        <ul>
          <li v-for="s in liveSessions" :key="s.id">
            <span class="rv-live-dot" :class="s.state" />
            <span class="rv-live-text">
              <span class="rv-live-name">{{ s.dramType }} · channel {{ s.channel }}</span>
              <span class="rv-mono">{{ stateLabel[s.state] }} · {{ s.events.toLocaleString() }} commands{{ keptNote(s) }} · started {{ new Date(s.startedAt).toLocaleTimeString() }}</span>
            </span>
            <UButton size="xs" color="neutral" variant="outline" class="ml-auto" :loading="opening === s.id" :disabled="opening !== null" @click="openSession(s.id)">
              Open
            </UButton>
          </li>
        </ul>
      </section>

      <TraceUpload />

      <TraceServerTraces />

      <TraceExamples />

      <p class="rv-note">
        Record a trace with the <code>BinTraceRecorder</code> controller plugin (<code>.ram2bin</code>),
        or stream one live with <code>LiveTraceStreamer</code>.
      </p>
    </div>
  </main>
</template>

<script setup lang="ts">
import type { LiveSessionInfo } from '~/composables/useStreamSession';

const stream = useStreamSession();
const toast = useToast();
const opening = ref<string | null>(null);

// The tutorial runs on an example with bank groups and tFAW, so every part of Bank state shows.
const examples = useExamples();
const tutorialExample = computed(() => examples.list.value.find(e => e.standard === 'DDR4') ?? examples.list.value[0] ?? null);
const startingTutorial = ref(false);
const sessionStore = useSessionStore();
const tutorial = useTutorial();

async function startTutorial() {
  const ex = tutorialExample.value;
  if (!ex) return;
  startingTutorial.value = true;
  try {
    await examples.prepare(ex);
    sessionStore.loadPendingTrace();
    tutorial.pending.value = true;
    await navigateTo('/trace');
  } catch (e: any) {
    toast.add({ color: 'error', title: 'Could not open the example trace', description: e.message ?? String(e) });
  } finally {
    startingTutorial.value = false;
  }
}

const stateLabel = { live: 'Live', done: 'Finished', stopped: 'Stopped', lost: 'No contact' } as const;

/** The visualizer keeps a limited amount of each session for viewers that open it later. */
function keptNote(s: LiveSessionInfo): string {
  if (!s.dropped) return '';
  return s.dropped >= s.events ? ' (no longer kept)' : ` (first ${s.dropped.toLocaleString()} no longer kept)`;
}
const liveSessions = computed(() => [...stream.sessions.value].sort((a, b) => b.startedAt - a.startedAt || a.channel - b.channel));

async function openSession(id: string) {
  opening.value = id;
  try {
    await stream.open(id);
    await navigateTo('/trace');
  } catch (e: any) {
    toast.add({ color: 'error', title: 'Could not open the live session', description: e.message ?? String(e) });
  } finally {
    opening.value = null;
  }
}

onMounted(() => stream.connect());
</script>

<style scoped>
.rv-home {
  min-height: 100vh;
  display: flex;
  justify-content: center;
  padding: 72px 24px 48px;
  background: var(--rv-bg);
  color: var(--rv-ink);
}

.rv-col {
  width: 100%;
  max-width: 560px;
  display: flex;
  flex-direction: column;
  gap: 28px;
}

.rv-brand {
  display: flex;
  align-items: center;
  gap: 12px;
  margin: 0;
  font-size: 30px;
  font-weight: 600;
  letter-spacing: -0.02em;
}

.rv-sub {
  margin: 4px 0 0;
  color: var(--rv-muted);
}

.rv-h {
  margin: 0 0 8px;
  font-size: 11px;
  font-weight: 600;
  letter-spacing: 0.06em;
  text-transform: uppercase;
  color: var(--rv-muted);
}

.rv-start-tour {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  padding: 10px 12px;
  border: 1px solid var(--rv-rule);
  font-size: 13px;
  color: var(--rv-muted);
}

.rv-live ul {
  margin: 0;
  padding: 0;
  list-style: none;
  border: 1px solid var(--rv-rule);
}

.rv-live li {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 10px 12px;
  font-size: 13px;
}

.rv-live li + li {
  border-top: 1px solid var(--rv-rule);
}

.rv-live-text {
  display: grid;
  gap: 2px;
}

.rv-live-name {
  font-weight: 500;
}

.rv-live-dot {
  flex: none;
  width: 8px;
  height: 8px;
  border-radius: 999px;
  background: var(--rv-rule-strong);
}

.rv-live-dot.live {
  background: #1a7f37;
}

.rv-live-dot.lost {
  background: #b35900;
}

.rv-mono {
  font-family: var(--font-mono);
  font-size: 12px;
  color: var(--rv-muted);
}

.rv-note {
  margin: 0;
  font-size: 12.5px;
  line-height: 1.5;
  color: var(--rv-muted);
}

.rv-note code {
  font-family: var(--font-mono);
  font-size: 12px;
  color: var(--rv-ink);
}
</style>
