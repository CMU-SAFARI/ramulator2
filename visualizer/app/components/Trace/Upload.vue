<template>
  <div class="w-full space-y-3">
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
      Load trace
    </UButton>
  </div>
</template>

<script setup lang="ts">
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
    const trace = await openPending({ kind: 'blob', blob: selectedFile.value });
    if (trace.header.incomplete) {
      toast.add({
        color: 'warning',
        icon: 'mdi-alert',
        title: 'Partial recording',
        description: `The simulation stopped before the recording finished. Showing the first ${trace.header.numEntries.toLocaleString()} commands.`,
      });
    }

    // Stop showing a live session; its stream continues for other viewers.
    useStreamSession().leave();

    sessionStore.setPendingTrace(trace, selectedFile.value.name);
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
