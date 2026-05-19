<template>
  <UDashboardGroup>
    <UDashboardSidebar :ui="{ body: 'p-0 gap-0' }">
      <div class="sticky top-0 z-50">
        <div class="px-3 flex items-center text-xs font-mono text-zinc-500 truncate select-none" style="height: 40px">
          {{ sessionStore.header?.dramType || 'Trace' }} — ch{{ sessionStore.header?.channelId }}
        </div>
        <div class="border-b border-neutral-800" />
      </div>
      <div ref="treeContainer" class="h-full overflow-auto pb-[52px]">
        <UTree
          v-model:expanded="expandedState"
          size="xl"
          :items="treeItems"
          :ui="{ root: 'border-b border-neutral-800', link: 'rounded-none before:rounded-none', itemWithChildren: 'ps-0' }"
        >
          <template #item-label="{ item }">
            {{ (item as any).displayName }}
          </template>
          <template #item-leading>
            <div />
          </template>
        </UTree>
      </div>
    </UDashboardSidebar>
    <UDashboardPanel>
      <slot />
    </UDashboardPanel>
  </UDashboardGroup>
</template>

<script setup lang="ts">
import type { TreeItem } from '@nuxt/ui';
import type { RowLayout, BusType } from '~/stores/ui';
import {
  nodeId,
  mirrorCmdDataExpansion,
  computeDefaultExpanded as computeDefaultExpandedIds,
  channelBasePath as channelBasePathFromSpec,
  findDeepestRowPrefix,
} from '~/utils/traceTree';

/** Must match request bar metrics in useRenderer (ROW_HEIGHT / STACK_ROW_GAP). */
const ROW_HEIGHT = 16;
const STACK_ROW_GAP = 4;

function computeStridesForLayout(levelSizes: Uint32Array, depth: number) {
  if (depth === 0) return { strides: [] as number[], totalLanes: 1 };
  const strides = new Array<number>(depth);
  strides[depth - 1] = 1;
  for (let k = depth - 2; k >= 0; k--)
    strides[k] = strides[k + 1]! * levelSizes[k + 1]!;
  return { strides, totalLanes: levelSizes[0]! * strides[0]! };
}

const uiStore = useUIStore();
const sessionStore = useSessionStore();
const treeContainer = ref<HTMLElement | null>(null);

const structuralDepth = computed(() => {
  const names = sessionStore.spec?.levelNames;
  if (!names) return 0;
  const rowIdx = names.indexOf('Row');
  return rowIdx >= 0 ? rowIdx : Math.max(names.length - 2, 1);
});

const rankLevel = computed(() => {
  const names = sessionStore.spec?.levelNames;
  if (!names) return 1;
  const idx = names.indexOf('Rank');
  return idx >= 0 ? idx : 1;
});

function buildHierarchy(prefix: string, depth: number, maxDepth: number, path: number[]): TreeItem[] {
  const spec = sessionStore.spec;
  if (!spec || depth >= maxDepth) return [];

  const count = spec.levelSizes[depth]!;
  const name = spec.levelNames[depth]!;

  return Array.from({ length: count }, (_, i) => {
    const childPath = [...path, i];
    const id = nodeId(prefix, childPath);
    const children = buildHierarchy(prefix, depth + 1, maxDepth, childPath);
    return {
      label: id,
      displayName: `${name} ${i}`,
      ...(children.length > 0 ? { children } : {}),
    } as TreeItem;
  });
}

const channelBasePath = computed(() => {
  const spec = sessionStore.spec;
  if (!spec) return [];
  return channelBasePathFromSpec(spec);
});

const dataStrideInfo = computed(() => {
  const spec = sessionStore.spec;
  if (!spec) return null;
  return computeStridesForLayout(spec.levelSizes, structuralDepth.value);
});

function pathToFlatLane(path: number[], strides: number[]): number {
  let flat = 0;
  for (let k = 0; k < path.length; k++) flat += path[k]! * strides[k]!;
  return flat;
}

const treeItems = computed(() => {
  const spec = sessionStore.spec;
  if (!spec) return [];

  const rl = rankLevel.value;
  const base = channelBasePath.value;

  if (uiStore.viewMode === 'throughput') {
    return [{
      label: '_channel',
      displayName: `Channel ${sessionStore.header?.channelId ?? 0}`,
      children: [
        { label: '_tp_data', displayName: 'Data Bus Utilization' } as TreeItem,
        { label: '_tp_cmd', displayName: 'CMD Bus Utilization' } as TreeItem,
      ],
    } as TreeItem];
  }

  if (uiStore.viewMode === 'request') {
    const reqChildren = buildHierarchy('req', rl, structuralDepth.value, base);
    return [{
      label: '_channel',
      displayName: `Channel ${sessionStore.header?.channelId ?? 0}`,
      children: reqChildren,
    } as TreeItem];
  }

  const cmdChildren = buildHierarchy('cmd', rl, structuralDepth.value, base);
  const dataChildren = buildHierarchy('data', rl, structuralDepth.value, base);

  return [{
    label: '_channel',
    displayName: `Channel ${sessionStore.header?.channelId ?? 0}`,
    children: [
      {
        label: '_bus_cmd',
        displayName: 'Command Bus',
        children: cmdChildren,
      } as TreeItem,
      {
        label: '_bus_data',
        displayName: 'Data Bus',
        children: dataChildren,
      } as TreeItem,
    ],
  } as TreeItem];
});

const expandedState = computed({
  get: () => {
    if (uiStore.expandedState.length === 0) {
      return computeDefaultExpanded();
    }
    return uiStore.expandedState;
  },
  set: (value: string[]) => {
    const previous = uiStore.expandedState.length > 0
      ? uiStore.expandedState
      : computeDefaultExpanded();
    uiStore.setExpandedState(mirrorCmdDataExpansion(value, previous));
  },
});

function computeDefaultExpanded(): string[] {
  const spec = sessionStore.spec;
  if (!spec) return [];
  return computeDefaultExpandedIds(uiStore.viewMode, spec);
}

function updateLayout() {
  if (!treeContainer.value || !sessionStore.spec) return;
  const buttons = treeContainer.value.querySelectorAll('button[data-slot="link"]');
  if (buttons.length === 0) return;

  const expandedSet = new Set(expandedState.value);
  const layout: RowLayout[] = [];
  const domIdx = { value: 0 };

  function skip() { domIdx.value++; }

  function walkBus(busType: BusType, prefix: string, depth: number, maxDepth: number, path: number[]) {
    if (depth >= maxDepth) return;
    const count = sessionStore.spec!.levelSizes[depth]!;
    const depths = sessionStore.requestLaneStackDepth;
    const dsi = dataStrideInfo.value;

    for (let i = 0; i < count; i++) {
      const el = buttons[domIdx.value] as HTMLElement | undefined;
      if (!el) return;

      const childPath = [...path, i];
      if (
        uiStore.viewMode === 'request'
        && busType === 'request'
        && dsi
        && depths.length > 0
      ) {
        const flat = pathToFlatLane(childPath, dsi.strides);
        const stacks = flat < depths.length ? (depths[flat] ?? 1) : 1;
        const h = stacks * ROW_HEIGHT + Math.max(0, stacks - 1) * STACK_ROW_GAP;
        el.style.minHeight = `${Math.max(1, h)}px`;
      }
      else {
        el.style.minHeight = '';
        el.style.height = '';
        el.style.maxHeight = '';
      }

      const rect = el.getBoundingClientRect();
      layout.push({ top: rect.top, height: rect.height, path: childPath, depth: depth + 1, busType });
      domIdx.value++;

      if (expandedSet.has(nodeId(prefix, childPath))) {
        walkBus(busType, prefix, depth + 1, maxDepth, childPath);
      }
    }
  }

  const base = channelBasePath.value;
  const rl = rankLevel.value;

  skip(); // Channel header
  if (expandedSet.has('_channel')) {
    if (uiStore.viewMode === 'throughput') {
      const targetRowH = 88;
      for (const busType of ['tp_data', 'tp_cmd'] as const) {
        const el = buttons[domIdx.value] as HTMLElement | undefined;
        if (!el) break;
        el.style.minHeight = `${targetRowH}px`;
        el.style.height = `${targetRowH}px`;
        el.style.maxHeight = `${targetRowH}px`;
        const rect = el.getBoundingClientRect();
        layout.push({ top: rect.top, height: rect.height, path: [], depth: 0, busType });
        domIdx.value++;
      }
    } else if (uiStore.viewMode === 'request') {
      walkBus('request', 'req', rl, structuralDepth.value, base);
    } else {
      skip(); // Command Bus header
      if (expandedSet.has('_bus_cmd')) {
        walkBus('command', 'cmd', rl, structuralDepth.value, base);
      }

      skip(); // Data Bus header
      if (expandedSet.has('_bus_data')) {
        walkBus('data', 'data', rl, structuralDepth.value, base);
      }
    }
  }

  uiStore.setRowLayout(layout);
}

let rafId = 0;
const scheduleLayout = () => {
  if (rafId) return;
  rafId = requestAnimationFrame(() => {
    rafId = 0;
    updateLayout();
  });
};

watch(
  () => sessionStore.requestLaneStackDepth,
  () => {
    nextTick(scheduleLayout);
  },
  { deep: true },
);

watch(
  () => [uiStore.layoutVersion, uiStore.treeScrollTarget] as const,
  async () => {
    const t = uiStore.treeScrollTarget;
    if (!t || !treeContainer.value) return;
    await nextTick();
    await nextTick();
    let row = findDeepestRowPrefix(uiStore.rowLayout, t.busType, t.path);
    if (!row) {
      updateLayout();
      row = findDeepestRowPrefix(uiStore.rowLayout, t.busType, t.path);
    }
    if (!row) {
      uiStore.clearTreeScrollTarget();
      return;
    }
    const ct = treeContainer.value;
    const crect = ct.getBoundingClientRect();
    const rowTopInContainer = row.top - crect.top + ct.scrollTop;
    const targetScroll = rowTopInContainer - ct.clientHeight / 2 + row.height / 2;
    ct.scrollTop = Math.max(0, Math.min(targetScroll, ct.scrollHeight - ct.clientHeight));
    uiStore.clearTreeScrollTarget();
  },
);

let resizeObs: ResizeObserver | null = null;
let mutationObs: MutationObserver | null = null;

onMounted(() => {
  requestAnimationFrame(updateLayout);

  if (treeContainer.value) {
    resizeObs = new ResizeObserver(scheduleLayout);
    resizeObs.observe(treeContainer.value);
    mutationObs = new MutationObserver(scheduleLayout);
    mutationObs.observe(treeContainer.value, { childList: true, subtree: true, attributes: true });
    treeContainer.value.addEventListener('scroll', scheduleLayout, { passive: true });
  }

  window.addEventListener('resize', scheduleLayout);
  window.addEventListener('scroll', scheduleLayout, true);
});

onUnmounted(() => {
  if (rafId) cancelAnimationFrame(rafId);
  resizeObs?.disconnect();
  mutationObs?.disconnect();
  treeContainer.value?.removeEventListener('scroll', scheduleLayout);
  window.removeEventListener('resize', scheduleLayout);
  window.removeEventListener('scroll', scheduleLayout, true);
});
</script>
