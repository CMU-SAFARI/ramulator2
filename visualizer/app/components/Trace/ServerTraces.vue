<template>
  <section v-if="enabled" aria-label="Traces on the server" class="rv-server">
    <div class="rv-head">
      <h2 class="rv-h">Traces on the server</h2>
      <UButton size="xs" color="neutral" variant="ghost" icon="i-lucide-refresh-cw" aria-label="Refresh the list" @click="refresh" />
    </div>
    <p v-if="!files.length" class="rv-empty">
      No <code>.ram2bin</code> files in <code>{{ root }}</code> yet.
    </p>
    <ul v-else>
      <li v-for="f in files" :key="f.path">
        <button type="button" :disabled="loadingFile !== null" @click="open(f)">
          <span class="rv-title">{{ f.path }}</span>
          <span class="rv-meta">{{ (f.size / 1e6).toFixed(1) }} MB · {{ new Date(f.modified).toLocaleString() }}</span>
          <span v-if="loadingFile === f.path" class="rv-meta">Loading…</span>
        </button>
      </li>
    </ul>
  </section>
</template>

<script setup lang="ts">
// Lists the .ram2bin files in the directory given to `python -m ramulator visualize --traces DIR`.
interface ServerTrace {
  path: string;
  size: number;
  modified: number;
}

const sessionStore = useSessionStore();
const stream = useStreamSession();
const toast = useToast();

const enabled = ref(false);
const root = ref('');
const files = ref<ServerTrace[]>([]);
const loadingFile = ref<string | null>(null);

async function refresh() {
  try {
    const res = await fetch('/api/traces');
    if (!res.ok) return;
    const body = await res.json();
    enabled.value = !!body.enabled;
    root.value = body.root ?? '';
    files.value = body.files ?? [];
  } catch {
    enabled.value = false;
  }
}

async function open(f: ServerTrace) {
  loadingFile.value = f.path;
  try {
    // Read in pieces with Range requests, so large traces never travel whole.
    // The server also keeps the trace's index, so only the first open reads it in full.
    const path = encodeURIComponent(f.path);
    const trace = await openPending({ kind: 'url', url: `/api/traces/file?path=${path}`, index: `/api/traces/trace-index?path=${path}` });
    if (trace.header.incomplete) {
      toast.add({
        color: 'warning',
        icon: 'mdi-alert',
        title: 'Partial recording',
        description: `The simulation stopped before the recording finished. Showing the first ${trace.header.numEntries.toLocaleString()} commands.`,
      });
    }
    stream.leave();
    sessionStore.setPendingTrace(trace, f.path.split('/').pop() ?? f.path);
    await navigateTo('/trace/setup');
  } catch (e: any) {
    toast.add({ color: 'error', title: 'Failed to open trace', description: e.message ?? String(e) });
    loadingFile.value = null;
  }
}

onMounted(refresh);
</script>

<style scoped>
.rv-server {
  width: 100%;
}

.rv-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 8px;
}

.rv-h {
  margin: 0;
  font-size: 11px;
  font-weight: 600;
  letter-spacing: 0.06em;
  text-transform: uppercase;
  color: var(--rv-muted);
}

.rv-empty {
  margin: 0;
  font-size: 12.5px;
  color: var(--rv-muted);
}

code {
  font-family: var(--font-mono);
  font-size: 12px;
}

ul {
  margin: 0;
  padding: 0;
  list-style: none;
  border: 1px solid var(--rv-rule);
  max-height: 320px;
  overflow-y: auto;
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
  overflow-wrap: anywhere;
}

.rv-meta {
  font-family: var(--font-mono);
  font-size: 11.5px;
  color: var(--rv-muted);
}
</style>
