// Leaving the page closes the trace engine, so a live session's stored commands are removed while
// its worker still runs. (A page that ends without this, e.g. a crashed tab, leaves them for the
// next page that opens a trace to remove.)
export default defineNuxtPlugin(() => {
  const sessionStore = useSessionStore();
  window.addEventListener('pagehide', (e) => {
    if (!e.persisted) sessionStore.engine?.dispose();
  });
});
