<template>
  <svg :width="size" :height="size" viewBox="0 0 64 64" role="img" aria-label="RamViz" class="shrink-0">
    <rect x="2" y="2" width="60" height="60" rx="12" fill="#1f2328" />
    <rect x="12" y="12" width="23" height="7" rx="1.5" fill="#5b9bff" />
    <rect x="38" y="12" width="14" height="7" rx="1.5" fill="#6b7480" />
    <rect x="12" y="23" width="9" height="7" rx="1.5" fill="#e07a3f" />
    <rect x="24" y="23" width="28" height="7" rx="1.5" fill="#5b9bff" />
    <rect x="12" y="34" width="14" height="7" rx="1.5" fill="#6b7480" />
    <rect x="29" y="34" width="9" height="7" rx="1.5" fill="#e07a3f" />
    <rect x="41" y="34" width="11" height="7" rx="1.5" fill="#5b9bff" />
    <rect x="12" y="45" width="26" height="7" rx="1.5" fill="#5b9bff" />
    <rect x="41" y="45" width="11" height="7" rx="1.5" fill="#6b7480" />
  </svg>
</template>

<script setup lang="ts">
/** RamViz logo mark: four bank lanes (open, activating, precharging) on a dark tile. */
withDefaults(defineProps<{ size?: number }>(), { size: 22 });
</script>
