<template>
  <UPopover
    :content="{ side: 'bottom', align: 'end', sideOffset: 8, collisionPadding: 12 }"
    :ui="{ content: 'max-w-sm' }"
  >
    <UButton
      label="Settings"
      icon="i-lucide-settings-2"
      color="neutral"
      variant="outline"
      size="sm"
      class="shrink-0 cursor-pointer"
      aria-label="Trace settings"
    />

    <template #content>
      <div class="flex flex-col gap-3 p-3 w-80">
        <div>
          <p class="text-xs font-semibold text-highlighted">
            Timeline
          </p>
          <p class="mt-1 text-xs text-muted leading-snug">
           Configure the timeline display.
          </p>
        </div>

        <div class="rounded-md border border-default px-3 py-2.5">
          <div class="flex items-center gap-3">
            <div class="min-w-0 flex-1">
              <p class="text-xs font-medium text-default">
                Display x-axis in nanoseconds
              </p>
              <p class="mt-0.5 text-[11px] text-muted leading-snug">
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

        <div class="rounded-md border border-default px-3 py-2.5">
          <div class="flex items-center gap-3">
            <div class="min-w-0 flex-1">
              <p class="text-xs font-medium text-default">
                Panel layout
              </p>
              <p class="mt-0.5 text-[11px] text-muted leading-snug">
                Bank state above Commands; the other views parked.
              </p>
            </div>
            <UButton label="Reset" size="xs" color="neutral" variant="outline" @click="uiStore.resetLayout()" />
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
