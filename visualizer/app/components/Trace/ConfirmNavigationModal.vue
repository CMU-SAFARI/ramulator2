<template>
  <UModal
    v-model:open="isOpen"
    title="Stop Visualization?"
    description="A simulation is currently running and visualizing live data. If you leave this page, the visualization will stop. Are you sure you want to proceed?"
    :ui="{ footer: 'justify-end' }"
  >
    <template #footer>
      <UButton label="Cancel" color="neutral" variant="outline" @click="cancel" />
      <UButton label="Stop & Leave" color="error" @click="confirm" />
    </template>
  </UModal>
</template>

<script setup lang="ts">
import { ref } from 'vue';

const isOpen = ref(false);
let resolvePromise: ((value: boolean) => void) | null = null;

const requestConfirmation = (): Promise<boolean> => {
  isOpen.value = true;
  return new Promise((resolve) => {
    resolvePromise = resolve;
  });
};

const confirm = () => {
  isOpen.value = false;
  if (resolvePromise) resolvePromise(true);
};

const cancel = () => {
  isOpen.value = false;
  if (resolvePromise) resolvePromise(false);
};

defineExpose({ requestConfirmation });
</script>
