export interface ExampleEntry {
  file: string;
  title: string;
  description: string;
  standard: string;
  commands: number;
  bytes: number;
}

/** The example traces (scripts/gen_example_traces.py); none in builds without them. */
export function useExamples() {
  const list = useState<ExampleEntry[]>('rv-examples', () => []);
  const sessionStore = useSessionStore();

  async function load() {
    try {
      const res = await fetch('/examples/index.json');
      list.value = res.ok ? await res.json() : [];
    } catch {
      list.value = [];
    }
  }

  /** Fetch an example and make it the trace to open next. */
  async function prepare(ex: ExampleEntry) {
    const res = await fetch(`/examples/${ex.file}`);
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const trace = await openPending({ kind: 'blob', blob: await res.blob() });
    useStreamSession().leave();
    sessionStore.setPendingTrace(trace, ex.file);
  }

  return { list, load, prepare };
}
