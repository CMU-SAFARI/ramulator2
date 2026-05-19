<template>
  <UPopover
    :content="{ side: 'bottom', align: 'end', sideOffset: 8, collisionPadding: 12 }"
    :ui="{ content: 'max-w-sm' }"
  >
    <UButton
      label="Settings"
      icon="i-lucide-settings-2"
      color="neutral"
      variant="subtle"
      size="sm"
      class="shrink-0 cursor-pointer"
      aria-label="Trace settings"
    />

    <template #content>
      <div class="flex flex-col gap-3 p-3 w-80">
        <div>
          <p class="text-xs font-semibold text-zinc-300">
            Timeline
          </p>
          <p class="mt-1 text-xs text-zinc-500 leading-snug">
           Configure the timeline display.
          </p>
        </div>

        <div class="rounded-lg bg-zinc-900/50 border border-zinc-800/80 px-3 py-2.5">
          <div class="flex items-center gap-3">
            <div class="min-w-0 flex-1">
              <p class="text-xs font-medium text-zinc-200">
                Display x-axis in nanoseconds
              </p>
              <p class="mt-0.5 text-[11px] text-zinc-500 leading-snug">
                Uses <span class="font-mono">tCK_ps</span>
                <template v-if="tCKPs != null">
                  = {{ tCKPs }} ps.
                </template>
                <template v-else>
                  from the trace spec when available.
                </template>
              </p>
            </div>

            <USwitch
              v-model="displayTimelineInNs"
              :disabled="tCKPs == null"
              aria-label="Display timeline x-axis in nanoseconds"
            />
          </div>
        </div>
      </div>
    </template>
  </UPopover>
</template>

<script setup lang="ts">
const uiStore = useUIStore();
const sessionStore = useSessionStore();

const tCKPs = computed(() => sessionStore.getTimingValue('tCK_ps'));

const displayTimelineInNs = computed({
  get: () => uiStore.displayTimelineInNs,
  set: (value: boolean) => uiStore.setDisplayTimelineInNs(value),
});
</script>
