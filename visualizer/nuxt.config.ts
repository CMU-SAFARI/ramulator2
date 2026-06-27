export default defineNuxtConfig({
  compatibilityDate: '2025-07-15',
  devtools: { enabled: false },
  devServer: {
    port: parseInt(import.meta.env.RAMULATOR_VISUALIZER_PORT || '3000', 10),
  },
  nitro: {
    experimental: {
      websocket: true,
    },
  },
  modules: [
    '@nuxt/ui',
    '@pinia/nuxt',
    '@nuxtjs/google-fonts'
  ],
  googleFonts: {
    families: {
      'DM Sans': true,
      'JetBrains Mono': true,
    },
  },
  colorMode: {
    preference: 'dark',
    storageKey: 'color-mode-ramwiz',
  },
  css: ['~/assets/css/main.css'],
})
