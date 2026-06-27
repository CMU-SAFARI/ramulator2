<template>
  <div
    class="flex flex-col items-center justify-center gap-5"
    role="status"
    aria-live="polite"
  >
    <div
      class="size-12 shrink-0 rounded-full border-2 border-zinc-700/45 border-t-[#ff6900] animate-spin motion-reduce:animate-none"
      style="animation-duration: 0.85s"
      aria-hidden="true"
    />
    <p class="text-sm text-zinc-400 text-center max-w-[min(22rem,90vw)] leading-snug">
      {{ message }}
    </p>
  </div>
</template>

<script setup lang="ts">
defineProps<{
  message: string;
}>();
</script>
