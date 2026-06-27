<template>
  <div class="w-96 min-h-48 mx-auto space-y-4">
    <UFileUpload
      v-model="selectedFile"
      label="Drop your trace here"
      layout="list"
      :multiple="false"
      :disabled="loading"
    />
    <UButton
      block
      square
      size="lg"
      :disabled="disabled || loading"
      :loading="loading"
      @click="loadTrace"
    >
      Load Trace
    </UButton>
  </div>
</template>

<script setup lang="ts">
const { parseTraceAsync } = useTrace();
const sessionStore = useSessionStore();

const loading = ref(false);
const selectedFile = ref<File | null>(null);
const disabled = computed(() => selectedFile.value === null);

const toast = useToast();

async function flushPaint() {
  await nextTick();
  await new Promise<void>((resolve) => {
    requestAnimationFrame(() => {
      requestAnimationFrame(() => resolve());
    });
  });
}

const loadTrace = async () => {
  if (!selectedFile.value) return;
  loading.value = true;
  await flushPaint();
  try {
    const buffer = await selectedFile.value.arrayBuffer();
    const trace = await parseTraceAsync(buffer);

    // Clear any live/done stream state before replacing it with a static trace.
    const stream = useStreamSession();
    if (stream.connected.value || stream.status.value !== 'idle') {
      stream.disconnect();
    }

    sessionStore.setPendingTrace(trace);
    await flushPaint();
    await navigateTo('/trace/setup');
  } catch (e: any) {
    console.error('Failed to parse trace:', e);
    toast.add({
      color: 'error',
      icon: 'mdi-alert-circle',
      title: 'Failed to parse trace file',
      description: e.message ?? 'Failed to parse trace file',
    });
    loading.value = false;
  }
};
</script>
