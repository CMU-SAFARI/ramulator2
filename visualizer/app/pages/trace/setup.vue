<template>
  <main v-if="pendingTrace" class="flex h-screen w-full items-center justify-center overflow-hidden bg-neutral-950 p-6 text-neutral-100">
    <div class="flex h-[720px] max-h-[calc(100vh-3rem)] w-full max-w-2xl flex-col">
      <div class="mb-5 flex items-center justify-between gap-4">
        <div>
          <h1 class="text-xl font-semibold tracking-tight text-neutral-50">
            Timing Parameters
          </h1>
        </div>

        <UButton
          color="neutral"
          variant="ghost"
          icon="i-lucide-arrow-left"
          label="Back"
          :disabled="loading"
          @click="goBack"
        />
      </div>

      <div class="min-h-0 flex-1 overflow-y-auto rounded-lg border border-neutral-800 bg-neutral-900/60">
        <div class="grid grid-cols-[minmax(8rem,1fr)_9rem] items-center gap-4 border-b border-neutral-800 px-4 py-3">
          <div class="min-w-0">
            <p class="truncate font-mono text-sm text-neutral-200">
              readLatency
            </p>
            <p class="mt-0.5 text-xs text-neutral-500">
              Original: {{ originalReadLatency.toLocaleString() }} cycles
            </p>
          </div>
          <UInput
            v-model.number="readLatency"
            type="number"
            size="sm"
            :min="0"
            :disabled="loading"
            :ui="{ trailing: 'pointer-events-none' }"
          >
            <template #trailing>
              <span class="text-[10px] text-neutral-500">clk</span>
            </template>
          </UInput>
        </div>

        <div
          v-for="(row, idx) in timingRows"
          :key="row.name"
          class="grid grid-cols-[minmax(8rem,1fr)_9rem] items-center gap-4 border-b border-neutral-800/70 px-4 py-3 last:border-b-0"
        >
          <div class="min-w-0">
            <p class="truncate font-mono text-sm text-neutral-200">
              {{ row.name }}
            </p>
            <p class="mt-0.5 text-xs text-neutral-500">
              Original: {{ originalTimingValues[idx]?.toLocaleString() ?? 0 }} cycles
            </p>
          </div>
          <UInput
            v-model.number="row.value"
            type="number"
            size="sm"
            :min="0"
            :disabled="loading"
            :ui="{ trailing: 'pointer-events-none' }"
          >
            <template #trailing>
              <span class="text-[10px] text-neutral-500">clk</span>
            </template>
          </UInput>
        </div>
      </div>

      <div class="mt-4 flex items-center justify-between gap-3">
        <UButton
          color="neutral"
          variant="subtle"
          icon="i-lucide-rotate-ccw"
          label="Reset"
          :disabled="loading"
          @click="resetValues"
        />

        <UButton
          trailing-icon="i-lucide-arrow-right"
          :loading="loading"
          :disabled="loading"
          @click="continueToTrace"
        >
          Continue
        </UButton>
      </div>
    </div>
  </main>
</template>

<script setup lang="ts">
const sessionStore = useSessionStore();
const traceLoadStatus = useTraceLoadStatus();

const pendingTrace = computed(() => sessionStore.pendingTrace);

if (!pendingTrace.value) {
  void navigateTo('/');
}

const originalTimingValues = Array.from(pendingTrace.value?.spec.timingValues ?? []);
const originalReadLatency = pendingTrace.value?.header.readLatency ?? 0;

const timingRows = reactive(
  (pendingTrace.value?.spec.timingNames ?? []).map((name, idx) => ({
    name,
    value: originalTimingValues[idx] ?? 0,
  })),
);

const readLatency = ref(originalReadLatency);
const loading = ref(false);

function toInt(value: number): number {
  if (!Number.isFinite(value)) return 0;
  return Math.max(0, Math.trunc(value));
}

function resetValues() {
  for (let i = 0; i < timingRows.length; i++) {
    timingRows[i]!.value = originalTimingValues[i] ?? 0;
  }
  readLatency.value = originalReadLatency;
}

async function flushPaint() {
  await nextTick();
  await new Promise<void>((resolve) => {
    requestAnimationFrame(() => {
      requestAnimationFrame(() => resolve());
    });
  });
}

function applyOverrides() {
  const trace = pendingTrace.value;
  if (!trace) return null;

  const values = new Int32Array(timingRows.map(row => toInt(row.value)));
  trace.spec.timingValues = values;
  trace.header.readLatency = toInt(readLatency.value);
  return { trace, values: Array.from(values), readLatency: trace.header.readLatency };
}

async function continueToTrace() {
  const overrides = applyOverrides();
  if (!overrides) {
    await navigateTo('/');
    return;
  }

  loading.value = true;
  traceLoadStatus.value = 'Opening viewer...';
  await flushPaint();

  sessionStore.clearPendingTrace();
  sessionStore.loadTrace(overrides.trace);

  await navigateTo('/trace');
}

async function goBack() {
  sessionStore.clearPendingTrace();
  await navigateTo('/');
}
</script>
