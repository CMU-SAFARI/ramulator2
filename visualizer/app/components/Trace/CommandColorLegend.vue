<template>
  <UPopover
    v-if="sessionStore.spec"
    :content="{ side: 'bottom', align: 'end', sideOffset: 8, collisionPadding: 12 }"
    :ui="{ content: 'max-w-sm' }"
  >
    <UButton
      label="Legend"
      icon="i-lucide-layers"
      color="neutral"
      variant="subtle"
      size="sm"
      class="shrink-0 cursor-pointer"
      :aria-label="'Color legend'"
    />

    <template #content>
      <div class="flex flex-col gap-3 p-3 w-80 max-h-[min(70vh,520px)]">
        <p class="text-xs text-zinc-500 leading-snug">
          <template v-if="uiStore.viewMode === 'command'">
            <strong>Command View Colors</strong> <br>
            Swatches open a larger picker. <br>
            Overrides persist by command name.
          </template>
          <template v-else>
            <strong>Request View Colors</strong> <br>
            Swatches open a larger picker. <br>
            Overrides persist in the browser.
          </template>
        </p>

        <UButton
          block
          label="Reset color palette"
          color="neutral"
          variant="soft"
          size="xs"
          @click="sessionStore.resetColorPalette()"
        />

        <!-- Command view -->
        <template v-if="uiStore.viewMode === 'command'">
          <!-- <div class="flex flex-wrap gap-x-3 gap-y-1.5 text-[11px] text-zinc-400 border-b border-zinc-800 pb-2">
            <span v-for="entry in semanticLegend" :key="entry.label" class="inline-flex items-center gap-1.5">
              <span
                class="size-2.5 rounded-full shrink-0 ring-1 ring-white/10"
                :style="{ backgroundColor: entry.color }"
              />
              {{ entry.label }}
            </span>
          </div> -->

          <div class="overflow-y-auto min-h-0 space-y-2 pr-1">
            <div
              v-for="row in commandRows"
              :key="row.id"
              class="flex items-center gap-3 min-w-0"
            >
              <span class="text-xs font-mono text-zinc-300 truncate flex-1" :title="row.name">
                {{ row.name }}
              </span>
              <UPopover
                :content="{ side: 'left', align: 'center', sideOffset: 10, collisionPadding: 16 }"
                :ui="{ content: 'p-0 z-[60]' }"
              >
                <button
                  type="button"
                  class="size-7 shrink-0 rounded-full border border-white/20 shadow-sm outline-none transition ring-2 ring-transparent hover:ring-white/25 focus-visible:ring-primary-400"
                  :style="{ backgroundColor: sessionStore.getCommandColor(row.id) }"
                  :aria-label="`Color for ${row.name}`"
                />

                <template #content>
                  <div class="p-4" @pointerdown.stop>
                    <UColorPicker
                      size="lg"
                      format="hex"
                      :model-value="sessionStore.getCommandColor(row.id)"
                      @update:model-value="onPickCommand(row.id, $event)"
                    />
                  </div>
                </template>
              </UPopover>
            </div>
          </div>
        </template>

        <!-- Request view -->
        <template v-else>
          <div class="overflow-y-auto min-h-0 space-y-2 pr-1">
            <div
              v-for="row in requestRows"
              :key="row.key"
              class="flex items-center gap-3 min-w-0"
            >
              <span class="text-xs font-mono text-zinc-300 truncate flex-1">
                {{ row.label }}
              </span>
              <UPopover
                :content="{ side: 'left', align: 'center', sideOffset: 10, collisionPadding: 16 }"
                :ui="{ content: 'p-0 z-[60]' }"
              >
                <button
                  type="button"
                  class="size-7 shrink-0 rounded-full border border-white/20 shadow-sm outline-none transition ring-2 ring-transparent hover:ring-white/25 focus-visible:ring-primary-400"
                  :style="{ backgroundColor: sessionStore.requestTypeColors[row.key] }"
                  :aria-label="`Color for ${row.label}`"
                />

                <template #content>
                  <div class="p-4" @pointerdown.stop>
                    <UColorPicker
                      size="lg"
                      format="hex"
                      :model-value="sessionStore.requestTypeColors[row.key]"
                      @update:model-value="onPickRequest(row.key, $event)"
                    />
                  </div>
                </template>
              </UPopover>
            </div>
          </div>
        </template>
      </div>
    </template>
  </UPopover>
</template>

<script setup lang="ts">
import type { RequestTypeStoredColors } from '~/utils/commandColorStorage';
import { CMD_PALETTE } from '~/utils/commandColors';

const sessionStore = useSessionStore();
const uiStore = useUIStore();

// const semanticLegend = [
//   { label: 'Opening', color: CMD_PALETTE.opening },
//   { label: 'Closing', color: CMD_PALETTE.closing },
//   { label: 'Read', color: CMD_PALETTE.readAccess },
//   { label: 'Write', color: CMD_PALETTE.writeAccess },
//   { label: 'Refresh', color: CMD_PALETTE.refreshing },
//   { label: 'Other', color: CMD_PALETTE.fallback },
// ] as const;

const commandRows = computed(() => {
  const s = sessionStore.spec;
  if (!s) return [];
  return s.commandNames.map((name, id) => ({ id, name }));
});

const requestRows: { key: keyof RequestTypeStoredColors; label: string }[] = [
  { key: 'read', label: 'Read' },
  { key: 'write', label: 'Write' },
  { key: 'maintenance', label: 'Maintenance' },
];

function onPickCommand(cmdId: number, value: string | undefined) {
  if (value) sessionStore.setCommandColor(cmdId, value);
}

function onPickRequest(key: keyof RequestTypeStoredColors, value: string | undefined) {
  if (value) sessionStore.setRequestTypeColor(key, value);
}
</script>
