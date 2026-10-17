import { buildTraceDerived, traceDerivedTransferables, type TraceDerivedInput } from '~/utils/tracePipeline';

// Live-streaming refreshes: rebuild every derived array off the main thread.
// Failures are posted back ({ error }) so the caller's promise always settles.
self.onmessage = async (event: MessageEvent<TraceDerivedInput>) => {
  try {
    const derived = await buildTraceDerived(event.data);
    // The caller already holds the trace's cmdId column; don't copy it back.
    derived.cmdId = new Uint8Array(0);
    self.postMessage(derived, { transfer: traceDerivedTransferables(derived) });
  } catch (e) {
    self.postMessage({ error: e instanceof Error ? `${e.name}: ${e.message}` : String(e) });
  }
};
