<template>
  <div v-if="left !== null" class="rv-unread" :style="{ left: `${left}%` }">
    <span v-if="label && left < 80" class="rv-unread-label">Reading the trace · {{ percent }}%</span>
  </div>
</template>

<script setup lang="ts">
// Marks the part of a time range the trace has not been read up to yet, with moving stripes: what
// lies there appears as the read gets to it. Over a panel it also shows how far the read is.
const props = defineProps<{ start: number; end: number; label?: boolean }>();

const tl = useTimeline();

/** Where the unread part begins, as a percentage of the range; null when all of it is read. */
const left = computed(() => {
  const l = tl.load.value;
  if (!l || l.done || l.error || l.frontier === Infinity || !(props.end > props.start)) return null;
  const f = ((l.frontier - props.start) / (props.end - props.start)) * 100;
  return f >= 100 ? null : Math.max(0, f);
});

const percent = computed(() => {
  const l = tl.load.value;
  return l && l.total ? Math.floor((100 * l.read) / l.total) : 0;
});
</script>

<style scoped>
.rv-unread {
  position: absolute;
  top: 0;
  right: 0;
  bottom: 0;
  pointer-events: none;
  display: flex;
  align-items: center;
  justify-content: center;
  border-left: 1px solid var(--rv-rule-strong);
  /* 135° stripes repeat every 20 / sin(45°) = 28.28 px along x, so moving by that loops seamlessly. */
  background: repeating-linear-gradient(135deg, rgb(89 99 110 / 0.14) 0 10px, rgb(89 99 110 / 0.04) 10px 20px);
  background-size: 28.28px 28.28px;
  animation: rv-unread-move 1.2s linear infinite;
}

.rv-unread-label {
  padding: 4px 10px;
  border: 1px solid var(--rv-rule);
  background: var(--rv-bg);
  color: var(--rv-muted);
  font-size: 12px;
  font-variant-numeric: tabular-nums;
  white-space: nowrap;
}

@keyframes rv-unread-move {
  to {
    background-position: 28.28px 0;
  }
}

@media (prefers-reduced-motion: reduce) {
  .rv-unread {
    animation: none;
  }
}
</style>
