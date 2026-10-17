<template>
  <main class="rv-home">
    <div class="rv-col">
      <header>
        <h1 class="rv-brand"><RamVizMark :size="36" />RamViz</h1>
        <p class="rv-sub">DRAM command and bank-state visualizer for Ramulator traces</p>
      </header>

      <div v-if="hasSessionAlert" class="rv-live" role="status">
        <span class="rv-live-dot" :class="{ on: stream.status.value === 'streaming' }" />
        <span>{{ stream.status.value === 'streaming' ? 'Live session active' : 'Simulation completed' }}</span>
        <span class="rv-mono">{{ stream.eventCount.value.toLocaleString() }} events</span>
        <UButton size="xs" color="neutral" variant="outline" class="ml-auto" @click="connectToSession">
          {{ stream.status.value === 'streaming' ? 'Connect to session' : 'View session' }}
        </UButton>
      </div>

      <TraceUpload />

      <TraceExamples />

      <p class="rv-note">
        Record a trace with the <code>BinTraceRecorder</code> controller plugin (<code>.ram2bin</code>),
        or stream one live with <code>LiveTraceStreamer</code>.
      </p>
    </div>
  </main>
</template>

<script setup lang="ts">
const stream = useStreamSession();
const hasSessionAlert = computed(() => stream.status.value === 'streaming' || stream.status.value === 'done');

function connectToSession() {
  void navigateTo('/trace');
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

.rv-live {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 10px 12px;
  border: 1px solid var(--rv-rule);
  font-size: 13px;
}

.rv-live-dot {
  width: 8px;
  height: 8px;
  border-radius: 999px;
  background: var(--rv-rule-strong);
}

.rv-live-dot.on {
  background: #1a7f37;
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
