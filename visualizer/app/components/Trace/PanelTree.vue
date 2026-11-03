<template>
  <aside class="rv-tree" aria-label="Hierarchy">
    <div ref="treeContainer" class="rv-tree-scroll">
      <UTree
        v-model:expanded="expandedState"
        size="xs"
        :items="treeItems"
        :ui="{ link: 'rounded-none before:rounded-none', itemWithChildren: 'ps-0' }"
      >
        <template #item-label="{ item }">
          <span :class="labelClass(item)">{{ (item as any).displayName }}</span>
        </template>
        <template #item-leading>
          <div />
        </template>
      </UTree>
    </div>
  </aside>
</template>

<script setup lang="ts">
// Sidebar tree of one panel. Every view uses the trace's organization hierarchy (Channel →
// PseudoChannel / Rank → … → Bank) plus its own summary rows. The rows define the panel's lanes:
// updateLayout() measures them into uiStore.panels[view].rowLayout for the renderer.
import type { TreeItem } from '@nuxt/ui';
import type { RowLayout, BusType, ViewMode } from '~/stores/ui';
import {
  nodeId,
  computeDefaultExpanded,
  channelBasePath as channelBasePathFromSpec,
  rankLevelIndex,
  structuralDepth as specStructuralDepth,
  TREE_PREFIX,
} from '~/utils/traceTree';
import { bankBuildLayout, bankCoordsRange, type BankLayout } from '~/utils/bankState';
import { steadyClock } from '~/composables/useTimeline';

const props = defineProps<{ view: ViewMode }>();

/** Must match the request bar metrics in useRenderer (REQ_BAR_HEIGHT / REQ_STACK_GAP / REQ_MAX_STACKS / REQ_ROW_PAD). */
const REQ_BAR_HEIGHT = 10;
const REQ_STACK_GAP = 2;
const REQ_MAX_STACKS = 8;
const REQ_ROW_PAD = 3;
/** Height of the channel-level throughput charts. */
const TP_ROW_HEIGHT = 88;

const uiStore = useUIStore();
const sessionStore = useSessionStore();
const panel = uiStore.panels[props.view];
const treeContainer = ref<HTMLElement | null>(null);

/** A tree row and the lane it becomes (rowKind unset: a plain header, not a lane). */
interface RowItem extends TreeItem {
  displayName: string;
  rowKind?: BusType;
  rowPath: number[];
  summary?: boolean;
  /** Request bank rows grow with their stacked requests. */
  reqLeaf?: boolean;
  fixedHeight?: number;
  children?: RowItem[];
}

function labelClass(item: unknown): string {
  return (item as RowItem).summary ? 'rv-tree-summary' : '';
}

const bankLayout = computed<BankLayout | null>(() => (sessionStore.spec ? bankBuildLayout(sessionStore.spec) : null));

/** Flat lane strides over the structural levels (same tiling as the renderer). */
const laneStrides = computed(() => {
  const spec = sessionStore.spec;
  const depth = spec ? specStructuralDepth(spec) : 0;
  if (!spec || depth === 0) return [] as number[];
  const strides = new Array<number>(depth);
  strides[depth - 1] = 1;
  for (let k = depth - 2; k >= 0; k--) strides[k] = strides[k + 1]! * spec.levelSizes[k + 1]!;
  return strides;
});

const treeItems = computed<RowItem[]>(() => {
  const spec = sessionStore.spec;
  if (!spec) return [];
  const view = props.view;
  const prefix = TREE_PREFIX[view];
  const sd = specStructuralDepth(spec);
  const rl = rankLevelIndex(spec);
  const base = channelBasePathFromSpec(spec);
  const L = bankLayout.value;
  const summary = (id: string, name: string, rowKind: BusType, rowPath: number[], fixedHeight?: number): RowItem =>
    ({ label: id, displayName: name, rowKind, rowPath, summary: true, fixedHeight });

  /** Summary rows of the node at `level` (rl - 1 = the channel), listed before its children. */
  const summaries = (level: number, path: number[]): RowItem[] => {
    if (view === 'command' && level === rl - 1) {
      // CMD BUS: every command redrawn on one row; DATA BUS: a channel-level row all data lanes map onto.
      return [summary('_sum_cmd', 'CMD BUS', 'cmd_sum', []), summary('_sum_data', 'DATA BUS', 'data', base)];
    }
    if (view === 'throughput' && level === rl - 1) {
      return [summary('_tp_data', 'DATA BUS', 'tp_data', [], TP_ROW_HEIGHT), summary('_tp_cmd', 'CMD BUS', 'tp_cmd', [], TP_ROW_HEIGHT)];
    }
    if (view === 'bank' && L) {
      // DATA BUS / ACT-tFAW lanes sit on their domain's node (PseudoChannel, Rank or the channel).
      const coords = path.slice(L.firstLevel);
      const out: RowItem[] = [];
      if (L.busLevel === level) out.push(summary(nodeId('bkbus', coords), 'DATA BUS', 'bk_bus', coords));
      if (L.fawLevel === level) out.push(summary(nodeId('bkfaw', coords), 'ACT / tFAW', 'bk_faw', coords));
      return out;
    }
    return [];
  };

  /** Lane of an organization node in this view. */
  const lane = (path: number[], leaf: boolean): Pick<RowItem, 'rowKind' | 'rowPath' | 'reqLeaf'> => {
    switch (view) {
      case 'command': return { rowKind: 'command', rowPath: path };
      case 'request': return { rowKind: 'request', rowPath: path, reqLeaf: leaf };
      case 'throughput': return { rowKind: 'tp_node', rowPath: path };
      default: {
        const coords = path.slice(L!.firstLevel);
        return leaf ? { rowKind: 'bank', rowPath: [bankCoordsRange(L!, coords).first] } : { rowKind: 'bk_hdr', rowPath: coords };
      }
    }
  };

  const walk = (level: number, path: number[]): RowItem[] => {
    if (level >= sd) return [];
    return Array.from({ length: spec.levelSizes[level]! }, (_, i) => {
      const p = [...path, i];
      const leaf = level === sd - 1;
      const children = leaf ? [] : [...summaries(level, p), ...walk(level + 1, p)];
      return {
        label: nodeId(prefix, p),
        displayName: `${spec.levelNames[level]} ${i}`,
        ...lane(p, leaf),
        ...(children.length > 0 ? { children } : {}),
      };
    });
  };

  if (view === 'bank' && !L) return [];
  return [{
    label: '_channel',
    displayName: `Channel ${sessionStore.header?.channelId ?? 0}`,
    // The bank view titles the channel row with its statistics; elsewhere it is a plain header.
    ...(view === 'bank' ? { rowKind: 'bk_hdr' as const, rowPath: [] } : { rowPath: base }),
    children: [...summaries(rl - 1, base), ...walk(rl, base)],
  }];
});

const expandedState = computed({
  get: () => (panel.expandedState.length > 0 ? panel.expandedState : defaultExpanded()),
  set: (value: string[]) => uiStore.setExpandedState(props.view, value),
});

function defaultExpanded(): string[] {
  return sessionStore.spec ? computeDefaultExpanded(props.view, sessionStore.spec) : [];
}

/** Request bank row: tall enough for its stacked requests (at most REQ_MAX_STACKS). */
function requestRowHeight(path: number[]): number | undefined {
  const depths = sessionStore.requestLaneStackDepth;
  if (depths.length === 0) return undefined;
  const strides = laneStrides.value;
  let flat = 0;
  for (let k = 0; k < path.length; k++) flat += path[k]! * strides[k]!;
  const shown = Math.min(depths[flat] ?? 1, REQ_MAX_STACKS);
  return Math.max(1, 2 * REQ_ROW_PAD + shown * REQ_BAR_HEIGHT + Math.max(0, shown - 1) * REQ_STACK_GAP);
}

function updateLayout() {
  if (!treeContainer.value || !sessionStore.spec) return;
  const buttons = treeContainer.value.querySelectorAll<HTMLElement>('button[data-slot="link"]');
  if (buttons.length === 0) return;

  const expandedSet = new Set(expandedState.value);
  const layout: RowLayout[] = [];
  const offsets: { top: number; height: number }[] = [];
  const ct = treeContainer.value;
  const contentTop = ct.getBoundingClientRect().top - ct.scrollTop;
  let domIdx = 0;

  const walk = (items: RowItem[]): boolean => {
    for (const item of items) {
      const el = buttons[domIdx++];
      if (!el) return false;
      const minHeight = item.fixedHeight ?? (item.reqLeaf ? requestRowHeight(item.rowPath) : undefined);
      el.style.minHeight = minHeight !== undefined ? `${minHeight}px` : '';
      el.style.height = el.style.maxHeight = item.fixedHeight !== undefined ? `${item.fixedHeight}px` : '';
      if (item.rowKind) {
        const rect = el.getBoundingClientRect();
        layout.push({ top: rect.top, height: rect.height, path: item.rowPath, depth: item.rowPath.length, busType: item.rowKind });
        offsets.push({ top: rect.top - contentTop, height: rect.height });
      }
      if (item.children?.length && expandedSet.has(item.label as string) && !walk(item.children)) return false;
    }
    return true;
  };
  walk(treeItems.value);

  rowOffsets = offsets;
  uiStore.setRowLayout(props.view, layout);
}

let rafId = 0;
function scheduleLayout() {
  if (rafId) return;
  rafId = requestAnimationFrame(() => {
    rafId = 0;
    updateLayout();
  });
}

watch(() => sessionStore.requestLaneStackDepth, () => nextTick(scheduleLayout), { deep: true });
// Docking, parking or resizing other panels moves this one without necessarily resizing it.
watch(() => [uiStore.stack, uiStore.weights], () => nextTick(scheduleLayout), { deep: true });

/** Scroll-independent offsets (within the tree content) of the rows in panel.rowLayout, same order. */
let rowOffsets: { top: number; height: number }[] = [];

/** Index of the deepest visible row on the way to `path` (a collapsed ancestor stands in for it). */
function targetRow(busType: BusType, path: number[]): number {
  let best = -1;
  panel.rowLayout.forEach((r, i) => {
    if (r.busType !== busType || r.path.length > path.length) return;
    if (r.path.some((v, k) => v !== path[k])) return;
    if (best < 0 || r.path.length > panel.rowLayout[best]!.path.length) best = i;
  });
  return best;
}

const SCROLL_MS = 320;
const reduceMotion = import.meta.client && window.matchMedia('(prefers-reduced-motion: reduce)').matches;
let scrollRaf = 0;

/**
 * Glide the tree just far enough to show a row (with margin), in step with the time-axis glide.
 * The goal is re-measured every frame, so it stays right while a panel opens or shrinks.
 */
function scrollIntoView(busType: BusType, path: number[]) {
  const ct = treeContainer.value;
  if (!ct) return;
  cancelAnimationFrame(scrollRaf);
  const s0 = ct.scrollTop;
  const clock = steadyClock();
  const step = (now: number) => {
    const i = targetRow(busType, path);
    const off = i >= 0 ? rowOffsets[i] : undefined;
    if (!off) return;
    const ch = ct.clientHeight;
    let goal = s0;
    if (off.top < s0 + 6) goal = off.top - 12;
    else if (off.top + off.height > s0 + ch - 6) goal = off.top + off.height - ch + 12;
    goal = Math.max(0, Math.min(goal, ct.scrollHeight - ch));
    const t = reduceMotion ? 1 : Math.min(1, clock(now) / SCROLL_MS);
    const e = t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2;
    ct.scrollTop = s0 + (goal - s0) * e;
    if (t < 1) scrollRaf = requestAnimationFrame(step);
  };
  scrollRaf = requestAnimationFrame(step);
}

watch(
  () => [panel.layoutVersion, panel.treeScrollTarget] as const,
  async () => {
    const t = panel.treeScrollTarget;
    if (!t || !treeContainer.value) return;
    uiStore.setTreeScrollTarget(props.view, null);
    // Let expansions from the navigation render, then measure.
    await nextTick();
    await nextTick();
    updateLayout();
    scrollIntoView(t.busType, t.path);
  },
);

let resizeObs: ResizeObserver | null = null;
let mutationObs: MutationObserver | null = null;

onMounted(() => {
  requestAnimationFrame(updateLayout);
  const ct = treeContainer.value;
  if (ct) {
    resizeObs = new ResizeObserver(scheduleLayout);
    resizeObs.observe(ct);
    mutationObs = new MutationObserver(scheduleLayout);
    mutationObs.observe(ct, { childList: true, subtree: true, attributes: true });
    ct.addEventListener('scroll', scheduleLayout, { passive: true });
  }
  window.addEventListener('resize', scheduleLayout);
  window.addEventListener('scroll', scheduleLayout, true);
});

onUnmounted(() => {
  if (rafId) cancelAnimationFrame(rafId);
  cancelAnimationFrame(scrollRaf);
  resizeObs?.disconnect();
  mutationObs?.disconnect();
  treeContainer.value?.removeEventListener('scroll', scheduleLayout);
  window.removeEventListener('resize', scheduleLayout);
  window.removeEventListener('scroll', scheduleLayout, true);
});
</script>

<style scoped>
.rv-tree {
  flex: none;
  width: var(--rv-tree-w);
  display: flex;
  flex-direction: column;
  min-height: 0;
  background: var(--rv-side);
  border-right: 1px solid var(--rv-rule);
}

.rv-tree-scroll {
  flex: 1;
  min-height: 0;
  overflow: auto;
  padding-bottom: 12px;
}

.rv-tree-summary {
  font-size: 11.5px;
  letter-spacing: 0.02em;
  color: var(--rv-muted);
}
</style>
