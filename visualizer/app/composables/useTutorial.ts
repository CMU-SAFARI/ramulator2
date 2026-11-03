/**
 * The tutorial's state, shared by the header button that opens it, the start page (which opens an
 * example trace first) and the walkthrough itself (components/Trace/Tutorial.vue).
 */
export function useTutorial() {
  const open = useState('rv-tutorial-open', () => false);
  /** Open it once the next trace is on screen. */
  const pending = useState('rv-tutorial-pending', () => false);
  return {
    open,
    pending,
    start() {
      pending.value = false;
      open.value = true;
    },
    close() {
      open.value = false;
    },
  };
}
