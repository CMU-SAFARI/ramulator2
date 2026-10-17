<template>
  <div
    class="flex flex-col items-center justify-center gap-5"
    role="status"
    aria-live="polite"
  >
    <div
      class="size-12 shrink-0 rounded-full border-2 border-neutral-300 border-t-[#1f5fb4] animate-spin motion-reduce:animate-none"
      style="animation-duration: 0.85s"
      aria-hidden="true"
    />
    <p class="text-sm text-neutral-600 text-center max-w-[min(22rem,90vw)] leading-snug">
      {{ message }}
    </p>
  </div>
</template>

<script setup lang="ts">
defineProps<{
  message: string;
}>();
</script>
