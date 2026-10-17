<template>
  <aside class="rv-inspector" aria-label="Inspector">
    <section class="rv-section">
      <h2 class="rv-h">Selection <span class="rv-h-scope">{{ VIEW_LABELS[mode] }}</span></h2>

      <template v-if="mode === 'bank' && selBank">
        <div class="rv-sel-head">
          <span class="rv-sw" :style="{ background: selBank.color }" />
          <strong>{{ capitalize(selBank.state) }}</strong>
          <span class="rv-mono rv-muted rv-push">{{ selBank.title }}</span>
        </div>
        <dl class="rv-dl">
          <dt>Interval</dt>
          <dd class="rv-mono">{{ fmtRange(selBank.start, selBank.end) }}</dd>
          <dt>Duration</dt>
          <dd class="rv-mono">{{ fmtSpan(selBank.end - selBank.start) }}</dd>
          <template v-if="selBank.sub">
            <dt>Now</dt>
            <dd><span class="rv-sw rv-sw-inline" :style="{ background: selBank.subColor }" />{{ selBank.sub }}</dd>
          </template>
          <template v-if="selBank.causeName">
            <dt>Cause</dt>
            <dd class="rv-mono">{{ selBank.causeName }} @ {{ fmtClk(selBank.causeClk ?? 0) }}</dd>
          </template>
          <dt>{{ selBank.row === 'bank' ? 'Bank' : 'Scope' }}</dt>
          <dd>{{ selBank.path }}</dd>
        </dl>
        <button v-if="selBank.cause >= 0" type="button" class="rv-link" @click="tl.focusCommand(selBank.cause, 'bank')">
          Show command in Commands
        </button>
      </template>

      <template v-else-if="(mode === 'command' || mode === 'request') && selHit">
        <div class="rv-sel-head">
          <span class="rv-sw" :style="{ background: hitColor }" />
          <strong>{{ hitName }}</strong>
          <span class="rv-badge">{{ hitBadge }}</span>
        </div>
        <dl class="rv-dl">
          <template v-if="selHit.bus === 'request'">
            <dt>Arrived</dt>
            <dd class="rv-mono">{{ fmtClk(Number(selHit.clk)) }}</dd>
            <dt>Queued</dt>
            <dd class="rv-mono">{{ fmtSpan(selHit.queueDelay ?? 0) }}</dd>
            <dt>In service</dt>
            <dd class="rv-mono">{{ fmtSpan(selHit.duration) }}</dd>
          </template>
          <template v-else>
            <dt>Issued</dt>
            <dd class="rv-mono">{{ fmtClk(Number(selHit.clk)) }}</dd>
            <dt>Duration</dt>
            <dd class="rv-mono">{{ fmtSpan(selHit.duration) }}</dd>
            <template v-if="selHit.bus === 'data' && selHit.busClk != null">
              <dt>Data at</dt>
              <dd class="rv-mono">{{ fmtClk(selHit.busClk) }}</dd>
            </template>
          </template>
          <template v-if="selHit.sourceId >= 0">
            <dt>Source</dt>
            <dd class="rv-mono">{{ selHit.sourceId }}</dd>
          </template>
          <dt>Address</dt>
          <dd class="rv-addr">
            <span v-for="(val, idx) in selHit.addr" :key="idx"><span class="rv-muted">{{ sessionStore.getLevelName(idx) }}</span> {{ val }}</span>
          </dd>
        </dl>
        <button type="button" class="rv-link" @click="tl.navigateToCounterpart(selHit, mode)">
          {{ selHit.bus === 'request' ? 'Show commands in Commands' : 'Show request in Requests' }}
        </button>
      </template>

      <template v-else-if="mode === 'throughput' && selTp">
        <div class="rv-sel-head">
          <span class="rv-sw" :style="{ background: selTp.series === 'cmd' ? PAPER.tp.cmd : PAPER.tp.data }" />
          <strong>{{ selTp.series === 'cmd' ? 'Command-bus utilization' : selTp.series === 'node' ? 'Data-bus share' : 'Data-bus utilization' }}</strong>
        </div>
        <dl class="rv-dl">
          <template v-if="selTp.label">
            <dt>Node</dt>
            <dd>{{ selTp.label }}</dd>
          </template>
          <dt>Value</dt>
          <dd class="rv-mono">{{ (selTp.value * 100).toFixed(2) }}%</dd>
          <dt>Bin</dt>
          <dd class="rv-mono">{{ fmtRange(selTp.binStartTime, selTp.binEndTime) }}</dd>
        </dl>
      </template>

      <p v-else class="rv-empty">
        Hover a {{ mode === 'throughput' ? 'point' : 'bar' }} to inspect it.
      </p>
    </section>

    <section class="rv-section">
      <h2 class="rv-h">{{ windowTitle }}</h2>
      <p class="rv-window rv-mono">{{ windowText }}</p>
      <div v-for="row in statRows" :key="row.label" class="rv-stat">
        <div class="rv-stat-line">
          <span v-if="row.sw" class="rv-sw rv-sw-sm" :style="{ background: row.sw }" />
          <span>{{ row.label }}</span>
          <span class="rv-mono rv-push">{{ row.value }}</span>
        </div>
        <div v-if="row.bar != null" class="rv-bar" :class="{ 'rv-bar-indent': !!row.sw }">
          <div :style="{ width: `${Math.max(0, Math.min(100, row.bar))}%` }" />
        </div>
      </div>
      <p v-if="statRows.length === 0" class="rv-empty">No data in this window.</p>
      <p v-if="!tl.rangeSelection.value" class="rv-hint">Shift+drag to select a range, Ctrl+drag to zoom to it.</p>
    </section>

    <section class="rv-section">
      <h2 class="rv-h">Legend</h2>

      <template v-if="mode === 'bank'">
        <div v-for="row in bankLegend" :key="row.label" class="rv-legend-row">
          <span class="rv-sw" :style="{ background: row.color, border: row.border }">
            <span v-if="row.glyph" :style="{ color: row.glyphColor }">{{ row.glyph }}</span>
          </span>
          <span class="rv-legend-text">
            {{ row.label }}
            <span v-if="row.detail" class="rv-legend-detail rv-mono">{{ row.detail }}</span>
          </span>
        </div>
        <ul v-if="bankRules?.notes.length" class="rv-notes">
          <li v-for="n in bankRules.notes" :key="n">{{ n }}</li>
        </ul>
        <p class="rv-caveat">
          Reconstructed from the command stream and the trace's timing table
          ({{ bankRules?.familyLabel }}), not simulator state. Not modelled: tRAS-delayed auto-precharge,
          tRRD, write-to-read turnaround.
        </p>
      </template>

      <template v-else-if="mode === 'command'">
        <div v-for="row in commandRows" :key="row.id" class="rv-legend-row">
          <UPopover :content="{ side: 'left', align: 'center', sideOffset: 10, collisionPadding: 16 }" :ui="{ content: 'p-0 z-[60]' }">
            <button type="button" class="rv-sw rv-sw-btn" :style="{ background: sessionStore.getCommandColor(row.id) }" :aria-label="`Colour for ${row.name}`" />
            <template #content>
              <div class="p-3" @pointerdown.stop>
                <UColorPicker size="md" format="hex" :model-value="sessionStore.getCommandColor(row.id)" @update:model-value="(v?: string) => v && sessionStore.setCommandColor(row.id, v)" />
              </div>
            </template>
          </UPopover>
          <span class="rv-mono">{{ row.name }}</span>
        </div>
        <button type="button" class="rv-link rv-link-sm" @click="sessionStore.resetColorPalette()">Reset colours</button>
      </template>

      <template v-else-if="mode === 'request'">
        <div v-for="row in requestRows" :key="row.key" class="rv-legend-row">
          <UPopover :content="{ side: 'left', align: 'center', sideOffset: 10, collisionPadding: 16 }" :ui="{ content: 'p-0 z-[60]' }">
            <button type="button" class="rv-sw rv-sw-btn" :style="{ background: sessionStore.requestTypeColors[row.key] }" :aria-label="`Colour for ${row.label}`" />
            <template #content>
              <div class="p-3" @pointerdown.stop>
                <UColorPicker size="md" format="hex" :model-value="sessionStore.requestTypeColors[row.key]" @update:model-value="(v?: string) => v && sessionStore.setRequestTypeColor(row.key, v)" />
              </div>
            </template>
          </UPopover>
          <span>{{ row.label }}</span>
        </div>
        <p class="rv-caveat">The lighter part of a bar is time spent queued.</p>
      </template>

      <template v-else>
        <div class="rv-legend-row"><span class="rv-sw rv-sw-line" :style="{ background: PAPER.tp.data }" />Data-bus utilization</div>
        <div class="rv-legend-row"><span class="rv-sw rv-sw-line" :style="{ background: PAPER.tp.cmd }" />Command-bus utilization</div>
        <p class="rv-caveat">Hierarchy rows: share of the data bus used by that node's bursts. Rows at one depth share a scale (top-left value).</p>
      </template>
    </section>
  </aside>
</template>

<script setup lang="ts">
// Right column: hover selection, window statistics and legend of the active panel
// (the last panel the pointer entered or the user showed).
import type { BankHover, HitResult, ThroughputHover } from '~/composables/useTimeline';
import type { RequestTypeStoredColors } from '~/utils/commandColorStorage';
import { BANK_COLORS, BANK_MARKER, BANK_MARKER_COLORS, bankResolveRules, type BankTimingRef } from '~/utils/bankState';
import { VIEW_LABELS } from '~/stores/ui';
import { PAPER } from '~/utils/theme';

const sessionStore = useSessionStore();
const uiStore = useUIStore();
const tl = useTimeline();
const mode = computed(() => uiStore.activeView);

// Hover is sticky per panel: the last item stays inspectable after the pointer leaves it.
const selBank = ref<BankHover | null>(null);
const selCmd = ref<HitResult | null>(null);
const selReq = ref<HitResult | null>(null);
const selTp = ref<ThroughputHover | null>(null);
watch(tl.bankHover, (h) => { if (h) selBank.value = h; });
watch(tl.hoveredCommand, (h) => { if (h) selCmd.value = h; });
watch(tl.hoveredRequest, (h) => { if (h) selReq.value = h; });
watch(tl.throughputHover, (h) => { if (h) selTp.value = h; });
// A click selects a request with its commands: show that request and the clicked command.
watch(tl.selection, (s) => {
  if (!s) return;
  selCmd.value = tl.hitForCommand(s.focus) ?? selCmd.value;
  if (s.req >= 0) selReq.value = tl.hitForRequest(s.req) ?? selReq.value;
});
watch(() => sessionStore.sessionGeneration, () => {
  selBank.value = null;
  selCmd.value = null;
  selReq.value = null;
  selTp.value = null;
});
const selHit = computed(() => (mode.value === 'request' ? selReq.value : selCmd.value));

const tckPs = computed(() => sessionStore.getTimingValue('tCK_ps') ?? null);

function fmtClk(c: number): string {
  const clk = `${Math.round(c).toLocaleString('en-US')} clk`;
  return tckPs.value ? `${clk} · ${((c * tckPs.value) / 1000).toLocaleString('en-US', { maximumFractionDigits: 2 })} ns` : clk;
}

function fmtSpan(d: number): string {
  const clk = `${Math.round(d).toLocaleString('en-US')} clk`;
  return tckPs.value ? `${clk} · ${((d * tckPs.value) / 1000).toFixed(2)} ns` : clk;
}

function fmtRange(a: number, b: number): string {
  if (tckPs.value) {
    const ns = (x: number) => ((x * tckPs.value!) / 1000).toLocaleString('en-US', { maximumFractionDigits: 2 });
    return `${ns(a)} – ${ns(b)} ns`;
  }
  return `${Math.round(a).toLocaleString('en-US')} – ${Math.round(b).toLocaleString('en-US')} clk`;
}

function capitalize(s: string): string {
  return s ? s.charAt(0).toUpperCase() + s.slice(1) : s;
}

const hitColor = computed(() => {
  const h = selHit.value;
  if (!h) return 'transparent';
  return h.bus === 'request' ? sessionStore.getRequestTypeColor(h.typeId) : sessionStore.getCommandColor(h.cmdId);
});
const hitName = computed(() => {
  const h = selHit.value;
  if (!h) return '';
  return h.bus === 'request' ? sessionStore.getRequestTypeName(h.typeId) : sessionStore.getCommandName(h.cmdId);
});
const hitBadge = computed(() => (selHit.value?.bus === 'request' ? 'request' : selHit.value?.bus === 'data' ? 'data bus' : 'command bus'));

// ── Window statistics ────────────────────────────────────────────

const win = computed(() => {
  const sel = tl.rangeSelection.value;
  if (sel && sel.endTime > sel.startTime) return { scope: 'Selected range', t0: sel.startTime, t1: sel.endTime };
  return { scope: 'Visible window', t0: tl.viewState.start, t1: tl.viewState.start + tl.viewState.duration };
});
const windowTitle = computed(() => win.value.scope);
const windowText = computed(() => `${fmtRange(win.value.t0, win.value.t1)} · ${Math.round(win.value.t1 - win.value.t0).toLocaleString('en-US')} clk`);

interface StatRow { label: string; value: string; sw?: string; bar?: number | null }

const statRows = computed<StatRow[]>(() => {
  const ranged = !!tl.rangeSelection.value;
  if (mode.value === 'bank') {
    const s = tl.bankStats.value;
    if (!s) return [];
    const rows: StatRow[] = [];
    if (s.busPct !== null) rows.push({ label: 'Data bus utilized', value: `${s.busPct.toFixed(1)}%`, sw: BANK_COLORS.busLane, bar: s.busPct });
    if (s.fawPct !== null) rows.push({ label: 'ACT blocked by tFAW', value: `${s.fawPct.toFixed(1)}%`, sw: BANK_COLORS.fawLane, bar: s.fawPct });
    const share = (label: string, v: number, sw: string) => rows.push({ label, value: `${v.toFixed(1)}%`, sw, bar: v });
    share('Open, not blocked', s.shares.starved, BANK_COLORS.open);
    share('Open, data bus busy', s.shares.bus, BANK_COLORS.bus);
    if (s.hasBg) share('Open, bank group blocked', s.shares.bg, BANK_COLORS.bg);
    share('Activating', s.shares.act, BANK_COLORS.act);
    share('Precharging', s.shares.pre, BANK_COLORS.pre);
    if (s.hasWrrec) share('Write recovery', s.shares.wrrec, BANK_COLORS.wrrec);
    if (s.hasRef) share('Refreshing', s.shares.ref, BANK_COLORS.ref);
    share('Closed, idle', s.shares.idle, BANK_COLORS.idle);
    rows.push({ label: 'Banks counted', value: String(s.banks), bar: null });
    return rows;
  }
  if (mode.value === 'throughput') {
    const t = ranged ? tl.throughputRangeStats.value : tl.viewThroughputStats.value;
    if (!t) return [];
    return [
      { label: 'Mean data-bus utilization', value: `${(t.dataMeanUtil * 100).toFixed(1)}%`, sw: PAPER.tp.data, bar: t.dataMeanUtil * 100 },
      { label: 'Mean command-bus utilization', value: `${(t.cmdMeanUtil * 100).toFixed(1)}%`, sw: PAPER.tp.cmd, bar: t.cmdMeanUtil * 100 },
      { label: 'Bins', value: String(t.binCount), bar: null },
    ];
  }
  const c = ranged ? tl.rangeStats.value : tl.viewStats.value;
  if (!c) return [];
  return [
    { label: 'Commands', value: c.totalCommands.toLocaleString('en-US'), bar: null },
    { label: 'Reads', value: c.readCommands.toLocaleString('en-US'), bar: null },
    { label: 'Writes', value: c.writeCommands.toLocaleString('en-US'), bar: null },
    { label: 'Other commands', value: c.otherCommands.toLocaleString('en-US'), bar: null },
    { label: 'Command-bus rate', value: `${c.cmdBusThroughput.toFixed(3)} cmd/clk`, bar: null },
  ];
});

// ── Legends ──────────────────────────────────────────────────────

const bankRules = computed(() => (sessionStore.spec
  ? bankResolveRules(sessionStore.spec, sessionStore.header?.dramType ?? '')
  : null));

function fmtTiming(t: BankTimingRef | null | undefined): string {
  if (!t) return 'n/a in this standard';
  const ns = tckPs.value ? ` (${((t.value * tckPs.value) / 1000).toFixed(2)} ns)` : '';
  return `+${t.expr} = ${t.value} clk${ns}`;
}

interface LegendRow { label: string; color: string; border?: string; glyph?: string; glyphColor?: string; detail?: string }

const bankLegend = computed<LegendRow[]>(() => {
  const r = bankRules.value;
  if (!r) return [];
  const t = r.timings;
  const rows: LegendRow[] = [
    { label: 'DATA BUS: burst on the bus', color: BANK_COLORS.busLane, detail: `column command ${fmtTiming(t.bl)}` },
  ];
  if (t.faw) rows.push({ label: 'ACT / tFAW: no ACT allowed', color: BANK_COLORS.fawLane, detail: `window ${fmtTiming(t.faw)}` });
  rows.push(
    { label: 'Activating', color: BANK_COLORS.act, detail: `ACT ${fmtTiming(t.rcd)}` },
    { label: 'Precharging', color: BANK_COLORS.pre, detail: `RDA ${fmtTiming(t.rdaPre)}; PRE ${fmtTiming(t.rp)}` },
  );
  if (t.wrRec) rows.push({ label: 'Write recovery, then tRP', color: BANK_COLORS.wrrec, detail: `WRA ${fmtTiming(t.wrRec)}` });
  if (t.ref.length) rows.push({ label: 'Refreshing', color: BANK_COLORS.ref, detail: t.ref.map(x => `${x.cmd} ${fmtTiming(x.t)}`).join('; ') });
  rows.push(
    { label: 'Closed, idle', color: BANK_COLORS.idle },
    { label: 'Open, data bus busy', color: BANK_COLORS.bus },
  );
  if (r.hasBgBlocking) rows.push({ label: 'Open, bank group blocked', color: BANK_COLORS.bg, detail: `same-group column command ${fmtTiming(t.ccdl)}` });
  rows.push({ label: 'Open, not blocked', color: BANK_COLORS.open, border: '1px solid var(--rv-rule-strong)' });
  const mk = (glyph: string, m: number, label: string): LegendRow => ({ label, color: 'transparent', glyph, glyphColor: BANK_MARKER_COLORS[m] });
  rows.push(
    mk('▲', BANK_MARKER.ACT, 'ACT'), mk('|', BANK_MARKER.RD, 'RD'), mk('■', BANK_MARKER.RDA, 'RDA'),
    mk('|', BANK_MARKER.WR, 'WR'), mk('■', BANK_MARKER.WRA, 'WRA'), mk('▼', BANK_MARKER.PRE, 'PRE'), mk('◆', BANK_MARKER.REF, 'REF / RFM'),
  );
  return rows;
});

const commandRows = computed(() => {
  const s = sessionStore.spec;
  return s ? s.commandNames.map((name, id) => ({ id, name })) : [];
});

const requestRows: { key: keyof RequestTypeStoredColors; label: string }[] = [
  { key: 'read', label: 'Read' },
  { key: 'write', label: 'Write' },
  { key: 'maintenance', label: 'Maintenance' },
];
</script>

<style scoped>
.rv-inspector {
  flex: none;
  width: 288px;
  overflow-y: auto;
  background: var(--rv-side);
  border-left: 1px solid var(--rv-rule);
  font-size: 12.5px;
}

.rv-section {
  padding: 14px 16px;
  border-bottom: 1px solid var(--rv-rule);
}

.rv-section:last-child {
  border-bottom: 0;
}

.rv-h {
  margin: 0 0 10px;
  font-size: 11px;
  font-weight: 600;
  letter-spacing: 0.06em;
  text-transform: uppercase;
  color: var(--rv-muted);
}

.rv-h-scope {
  margin-left: 6px;
  font-weight: 500;
  letter-spacing: 0.02em;
  text-transform: none;
  color: var(--rv-ink);
}

.rv-mono {
  font-family: var(--font-mono);
  font-size: 12px;
}

.rv-muted {
  color: var(--rv-muted);
}

.rv-push {
  margin-left: auto;
}

.rv-empty {
  margin: 0;
  color: var(--rv-muted);
}

.rv-sel-head {
  display: flex;
  align-items: center;
  gap: 8px;
}

.rv-badge {
  margin-left: auto;
  font-size: 11px;
  color: var(--rv-muted);
}

.rv-dl {
  margin: 10px 0 0;
  display: grid;
  grid-template-columns: 72px minmax(0, 1fr);
  gap: 5px 8px;
}

.rv-dl dt {
  color: var(--rv-muted);
}

.rv-dl dd {
  margin: 0;
  min-width: 0;
  overflow-wrap: anywhere;
}

.rv-addr {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 2px 10px;
  font-family: var(--font-mono);
  font-size: 11.5px;
}

.rv-sw {
  flex: none;
  box-sizing: border-box;
  width: 12px;
  height: 12px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  border: 1px solid var(--rv-rule-strong);
  font-size: 10px;
  font-weight: 600;
  line-height: 1;
}

.rv-sw-sm {
  width: 10px;
  height: 10px;
}

.rv-sw-inline {
  margin-right: 6px;
  vertical-align: -1px;
}

.rv-sw-line {
  height: 3px;
  width: 16px;
  border: 0;
}

.rv-sw-btn {
  width: 14px;
  height: 14px;
  cursor: pointer;
  padding: 0;
}

.rv-link {
  margin-top: 10px;
  padding: 0;
  border: 0;
  background: none;
  color: var(--rv-accent);
  font: inherit;
  cursor: pointer;
}

.rv-link:hover {
  text-decoration: underline;
}

.rv-link-sm {
  font-size: 12px;
}

.rv-window {
  margin: -4px 0 10px;
  color: var(--rv-muted);
}

.rv-stat {
  margin-bottom: 7px;
}

.rv-stat-line {
  display: flex;
  align-items: center;
  gap: 8px;
}

.rv-bar {
  height: 3px;
  margin-top: 4px;
  background: var(--rv-track);
}

.rv-bar-indent {
  margin-left: 18px;
}

.rv-bar > div {
  height: 100%;
  background: var(--rv-muted);
}

.rv-legend-row {
  display: flex;
  align-items: flex-start;
  gap: 8px;
  margin-bottom: 6px;
}

.rv-legend-row > .rv-sw {
  margin-top: 2px;
}

.rv-legend-text {
  min-width: 0;
}

.rv-legend-detail {
  display: block;
  font-size: 11px;
  color: var(--rv-muted);
}

.rv-notes {
  margin: 8px 0 0;
  padding-left: 16px;
  font-size: 11.5px;
  color: #9a6700;
}

.rv-hint {
  margin: 10px 0 0;
  font-size: 11.5px;
  color: var(--rv-muted);
}

.rv-caveat {
  margin: 10px 0 0;
  font-size: 11.5px;
  line-height: 1.45;
  color: var(--rv-muted);
}
</style>
