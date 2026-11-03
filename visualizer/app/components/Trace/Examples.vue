<template>
  <section v-if="examples.length" aria-label="Example traces" class="rv-examples">
    <h2 class="rv-h">Open an example</h2>
    <ul>
      <li v-for="ex in examples" :key="ex.file">
        <button type="button" :disabled="loadingFile !== null" @click="open(ex)">
          <span class="rv-title">{{ ex.title }}</span>
          <span class="rv-desc">{{ ex.description }}</span>
          <span class="rv-meta">{{ ex.commands.toLocaleString('en-US') }} commands · {{ (ex.bytes / 1e6).toFixed(1) }} MB</span>
          <span v-if="loadingFile === ex.file" class="rv-meta">Loading…</span>
        </button>
      </li>
    </ul>
  </section>
</template>

<script setup lang="ts">
import type { ExampleEntry } from '~/composables/useExamples';

const toast = useToast();
const { list: examples, load, prepare } = useExamples();
const loadingFile = ref<string | null>(null);

onMounted(load);

async function open(ex: ExampleEntry) {
  loadingFile.value = ex.file;
  try {
    await prepare(ex);
    await navigateTo('/trace/setup');
  } catch (e: any) {
    toast.add({ color: 'error', title: 'Failed to load example', description: e.message ?? String(e) });
    loadingFile.value = null;
  }
}
</script>

<style scoped>
.rv-examples {
  width: 100%;
}

.rv-h {
  margin: 0 0 8px;
  font-size: 11px;
  font-weight: 600;
  letter-spacing: 0.06em;
  text-transform: uppercase;
  color: var(--rv-muted);
}

ul {
  margin: 0;
  padding: 0;
  list-style: none;
  border: 1px solid var(--rv-rule);
}

li + li {
  border-top: 1px solid var(--rv-rule);
}

button {
  width: 100%;
  display: grid;
  gap: 2px;
  padding: 10px 12px;
  border: 0;
  background: var(--rv-bg);
  color: var(--rv-ink);
  font: inherit;
  text-align: left;
  cursor: pointer;
}

button:hover:not(:disabled) {
  background: var(--rv-hover);
}

button:disabled {
  cursor: progress;
}

.rv-title {
  font-weight: 500;
}

.rv-desc {
  font-size: 12.5px;
  color: var(--rv-muted);
}

.rv-meta {
  font-family: var(--font-mono);
  font-size: 11.5px;
  color: var(--rv-muted);
}
</style>
