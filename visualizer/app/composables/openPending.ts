// Opening a trace for the setup page: its header and spec, checked for damage and truncation.
import { blobSource, httpSource, openTrace } from '~/engine/source';
import type { EngineSource } from '~/engine/protocol';
import type { PendingTrace } from '~/stores/session';

export async function openPending(source: Exclude<EngineSource, { kind: 'live' }>): Promise<PendingTrace> {
  const bytes = source.kind === 'blob' ? blobSource(source.blob) : await httpSource(source.url);
  const file = await openTrace(bytes);
  return { source, header: file.header, spec: file.spec };
}
