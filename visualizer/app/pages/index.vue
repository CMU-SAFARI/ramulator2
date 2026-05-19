<template>
  <main class="min-h-screen flex items-center justify-center w-full">
    <div class="flex flex-col items-center gap-6">
      <div
        v-if="hasSessionAlert"
        class="flex items-center gap-3 rounded-full border border-emerald-500/25 bg-emerald-500/10 px-4 py-2 text-sm text-emerald-100 shadow-lg shadow-emerald-950/20"
      >
        <span
          class="inline-block size-2 rounded-full"
          :class="stream.status.value === 'streaming' ? 'bg-emerald-400 animate-pulse' : 'bg-zinc-400'"
        />
        <span>
          {{ stream.status.value === 'streaming' ? 'Live session active' : 'Simulation completed' }}
        </span>
        <span class="font-mono text-emerald-200/80">
          {{ stream.eventCount.value.toLocaleString() }} events
        </span>
        <UButton
          size="xs"
          :color="stream.status.value === 'streaming' ? 'success' : 'neutral'"
          variant="ghost"
          class="rounded-full"
          @click="connectToSession"
        >
          {{ stream.status.value === 'streaming' ? 'Connect to session' : 'View session' }}
        </UButton>
      </div>

      <TraceUpload />

      <!-- <div class="flex items-center gap-2 text-sm text-zinc-500">
        <span
          class="inline-block size-2 rounded-full"
          :class="stream.connected.value ? 'bg-emerald-500' : 'bg-zinc-600'"
        />
        <template v-if="stream.status.value === 'streaming'">
          <span class="text-zinc-300">Receiving live trace…</span>
          <span class="font-mono text-zinc-400">{{ stream.eventCount.value.toLocaleString() }} events</span>
        </template>
        <template v-else-if="stream.status.value === 'finalizing'">
          <span class="text-zinc-300">Loading streamed trace…</span>
        </template>
        <template v-else-if="stream.status.value === 'done'">
          <span class="text-zinc-300">Simulation completed</span>
          <span class="font-mono text-zinc-400">{{ stream.eventCount.value.toLocaleString() }} events</span>
        </template>
        <template v-else>
          <span>Live stream {{ stream.connected.value ? 'ready' : 'disconnected' }}</span>
        </template>
      </div> -->
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
