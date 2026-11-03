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
  app: {
    head: {
      title: 'RamViz',
      link: [
        { rel: 'icon', type: 'image/svg+xml', href: '/favicon.svg' },
        { rel: 'alternate icon', href: '/favicon.ico' },
      ],
    },
  },
  googleFonts: {
    families: {
      'IBM Plex Sans': [400, 500, 600, 700],
      'IBM Plex Mono': [400, 500],
    },
  },
  colorMode: {
    preference: 'light',
    fallback: 'light',
    storageKey: 'color-mode-ramviz',
  },
  css: ['~/assets/css/main.css'],
  routeRules: {
    // No framing by other sites, no content sniffing, no referrers; other sites cannot load its files.
    '/**': {
      headers: {
        'Content-Security-Policy': "frame-ancestors 'none'",
        'X-Frame-Options': 'DENY',
        'X-Content-Type-Options': 'nosniff',
        'Referrer-Policy': 'no-referrer',
        'Cross-Origin-Resource-Policy': 'same-origin',
      },
    },
    // The trace pages hold client-side data and WebGL only; render them in the browser.
    '/trace': { ssr: false },
    '/trace/**': { ssr: false },
  },
  vite: {
    // regl is CommonJS; pre-bundling it avoids a full page reload on first dev load.
    optimizeDeps: { include: ['regl'] },
  },
})
