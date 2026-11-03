<template>
  <Teleport to="body">
    <div v-if="tutorial.open.value && step" class="rv-tour">
      <div v-if="spot" class="rv-tour-spot" :style="spotStyle" />
      <div v-else class="rv-tour-dim" />
      <section
        ref="card"
        class="rv-tour-card"
        role="dialog"
        aria-modal="false"
        aria-labelledby="rv-tour-title"
        :style="cardStyle"
      >
        <header class="rv-tour-head">
          <span class="rv-tour-count">{{ index + 1 }} of {{ steps.length }}</span>
          <button type="button" class="rv-tour-close" aria-label="Close the tutorial" @click="tutorial.close()">
            <svg viewBox="0 0 12 12" aria-hidden="true"><path d="M3 3l6 6M9 3l-6 6" /></svg>
          </button>
        </header>
        <h2 id="rv-tour-title" class="rv-tour-title">{{ step.title }}</h2>
        <template v-for="(para, i) in step.body" :key="i">
          <ul v-if="Array.isArray(para)" class="rv-tour-text rv-tour-list">
            <li v-for="(item, k) in para" :key="k">
              <template v-for="(part, j) in withKeys(item)" :key="j">
                <kbd v-if="part.key">{{ part.text }}</kbd>
                <template v-else>{{ part.text }}</template>
              </template>
            </li>
          </ul>
          <p v-else class="rv-tour-text">
            <template v-for="(part, j) in withKeys(para)" :key="j">
              <kbd v-if="part.key">{{ part.text }}</kbd>
              <template v-else>{{ part.text }}</template>
            </template>
          </p>
        </template>
        <footer class="rv-tour-foot">
          <UButton v-if="index > 0" size="sm" color="neutral" variant="ghost" label="Back" @click="go(index - 1)" />
          <UButton
            size="sm"
            color="neutral"
            variant="solid"
            :label="index === steps.length - 1 ? 'Done' : 'Next'"
            @click="index === steps.length - 1 ? tutorial.close() : go(index + 1)"
          />
        </footer>
      </section>
    </div>
  </Teleport>
</template>

<script setup lang="ts">
// A walk through the trace page: each step points at one part of it and says what it shows. The
// page stays usable underneath, so people can try things as they read.
import { bankUnsupportedReason } from '~/utils/bankState';

interface Step {
  title: string;
  /** Paragraphs, or lists as arrays of items; [[W]] shows a key. */
  body: (string | string[])[];
  /** What the step points at; several selectors make one area. None centres the card. */
  target?: string[];
  /** Where the card goes, in order of preference. */
  place?: Side[];
  /** Only when this holds (a live session, a trace still being read, ...). */
  when?: () => boolean;
  /** Brings what the step points at on screen. */
  prepare?: () => void;
}
type Side = 'right' | 'left' | 'below' | 'above';
const SIDES: Side[] = ['right', 'left', 'below', 'above'];

const tutorial = useTutorial();
const tl = useTimeline();
const uiStore = useUIStore();
const sessionStore = useSessionStore();
const stream = useStreamSession();

const bankSupported = () => !!sessionStore.spec && !bankUnsupportedReason(sessionStore.spec);
const isLive = () => !!stream.sessionId.value;
const show = (view: 'command' | 'bank') => () => {
  if (!uiStore.stack.includes(view)) uiStore.reveal(view);
};

const ALL_STEPS: Step[] = [
  {
    title: 'Panels',
    body: [
      'Each panel is one view of the trace. All panels share one time axis, running left to right, so they always show the same stretch of time. Zooming or moving in one panel moves them all.',
    ],
    target: ['.rv-dock-wrap'],
  },
  {
    title: 'Commands panel',
    body: [
      'Each mark is one command the controller issued, drawn on the row of the bank it went to. The colour is the command type (see Legend on the right). The width is how many cycles the command takes on the command bus. A command to several banks, such as an all-bank refresh, appears on each of them.',
      'Scroll to zoom around the mouse pointer. Drag, or Shift+scroll, to move. Keys: [[W]]/[[S]] zoom, [[A]]/[[D]] move, [[Home]]/[[End]] go to the start or end.',
    ],
    target: ['[data-dock-view="command"] .rv-canvas-wrap'],
    place: ['above', 'right', 'left'],
    prepare: show('command'),
  },
  {
    title: 'Rows',
    body: [
      'Rows follow the memory\'s structure for this standard: channel, then rank, bank group and bank. The arrows fold and unfold a level.',
      'CMD BUS is the channel\'s command bus, with every command. DATA BUS shows when the data of each read or write is on the bus: one burst, starting the read or write latency after the command.',
    ],
    target: ['[data-dock-view="command"] .rv-tree'],
    place: ['right'],
    prepare: show('command'),
  },
  {
    title: 'Time axis and overview',
    body: [
      'The top line is the time axis for all panels, in clock cycles (or nanoseconds, set in Settings). Below it is the whole trace: the blue curve is the number of commands over time, and the dark frame is the range shown in the panels.',
      'Drag the frame to move it, drag its edges to change its width, or click the strip to jump.',
    ],
    target: ['.rv-timeline'],
    place: ['above'],
  },
  {
    title: 'Unread part',
    body: [
      'Striped areas are parts of the trace not yet read from the file. They fill in as reading goes on, and the header shows the progress. Figures that cover unread time are marked ≈, or say "not read yet".',
      'After the first full read, RamViz saves an index of the trace, so it opens at once next time.',
    ],
    target: ['.rv-timeline'],
    place: ['above'],
    when: () => !isLive() && !!tl.load.value && !tl.load.value.done && !tl.load.value.error,
  },
  {
    title: 'Inspector: Selection',
    body: [
      'Shows the command, request or bank state under the mouse pointer. For a command: issue clock, length, request id and address. For a request: arrival, time queued and time in service. For a bank state: its interval and the command that caused it.',
      'Clicking a read or write selects it with its request. The Requests panel opens, and lines connect the request to its commands and to their bank in Bank state. [[Esc]] clears the selection.',
    ],
    target: ['[data-tour="selection"]'],
    place: ['left'],
  },
  {
    title: 'Inspector: Visible window',
    body: [
      'Totals for the time range on screen, for the active panel:',
      [
        'Commands and Requests: command counts and the command-bus rate.',
        'Throughput: bus utilization.',
        'Bank state: the share of time the banks spent in each state.',
      ],
      'Shift+drag across a panel to get totals for that range instead (the title changes to Selected range). Ctrl+drag (⌘+drag on a Mac) zooms to the range.',
    ],
    target: ['[data-tour="window"]'],
    place: ['left'],
  },
  {
    title: 'View tabs',
    body: [
      'Commands: every command issued, per bank.',
      'Requests: every memory request. A bar runs from arrival at the controller until its data is on the bus (reads) or its write command is issued (writes). The lighter first part of a bar is the time before its first command. Hatched bars were still open when the trace ended.',
      'Throughput: utilization of the data bus and the command bus over time, and each part of the memory\'s share of the data bus.',
      'Bank state: what each bank was doing, reconstructed from the commands.',
      'Click a tab to show it in place of the active panel. Drag a tab into the panel area to add a panel. The − on a panel\'s title bar removes it.',
    ],
    target: ['nav[aria-label="Views"]'],
    place: ['below'],
  },
  {
    title: 'Bank state panel',
    body: [
      'One row per bank, coloured by its state over time. The states come from the commands and the standard\'s timing parameters.',
      'Activating, Precharging, Write recovery and Refreshing each last for the timing shown next to them in the legend (tRCD, tRP, tWR, tRFC). Closed, idle means no row is open.',
      'An open row is shaded by what was in its way:',
      [
        'Open, data bus busy: the bus was carrying data.',
        'Open, bank group blocked: within tCCD_L of a column command in the same bank group.',
        'Open, not blocked: nothing was in the way and no command came.',
      ],
      'DATA BUS rows show the bursts. ACT / tFAW is red while the rank can\'t issue another ACT because four were issued within tFAW. Symbols on the rows are the commands: ▲ ACT, ▼ PRE, | RD/WR, ■ RDA/WRA, ◆ REF. The line above each group gives the totals for the window.',
    ],
    target: ['[data-dock-view="bank"]'],
    place: ['right', 'below', 'above'],
    when: bankSupported,
    prepare: show('bank'),
  },
  {
    title: 'Live session',
    body: [
      'The command count grows while the simulation streams. Follow live keeps the newest commands in view, and moving away from the end turns it off. Stop ends streaming for this run, but the simulation itself keeps running.',
    ],
    target: ['[data-tour="live"]'],
    place: ['below'],
    when: isLive,
  },
  {
    title: 'Go to clock and Cursor',
    body: [
      'Go to clock: type a clock cycle (such as 8,590,018,300) or a time (1250 ns, 1.5 us) and press [[Enter]]. The view centres on it and the cursor is placed there. [[G]] moves to this field.',
      'Cursor ([[C]]): click a panel to place a vertical line. [[W]]/[[S]] then zoom around the line. The arrows next to Cursor move it to the previous or next command (or request, in Requests).',
    ],
    target: ['.rv-goto', '.rv-cursor'],
    place: ['below'],
  },
  {
    title: 'Settings',
    body: [
      'Time axis in nanoseconds: shows times in ns, using the trace\'s clock period (tCK), instead of clock cycles.',
      'Panel layout → Reset: puts back Bank state above Commands, with the other views as tabs.',
    ],
    target: ['button[aria-label="Trace settings"]'],
    place: ['below'],
  },
];

/** The steps for this trace, fixed when the tutorial opens. */
const steps = shallowRef<Step[]>([]);
const index = ref(0);
const step = computed(() => steps.value[index.value] ?? null);

function go(i: number) {
  index.value = Math.max(0, Math.min(steps.value.length - 1, i));
  step.value?.prepare?.();
}

watch(() => tutorial.open.value, (open) => {
  if (!open) return;
  steps.value = ALL_STEPS.filter(s => !s.when || s.when());
  go(0);
}, { immediate: true });

// Opened from the start page: begin once the trace is on screen, or not at all if it fails to open.
watch(() => tl.load.value, (l) => {
  if (!l || !tutorial.pending.value) return;
  if (l.ready) tutorial.start();
  else if (l.error) tutorial.pending.value = false;
}, { immediate: true });

// ── Placement ──────────────────────────────────────────────────

interface Box { left: number; top: number; width: number; height: number }

const card = ref<HTMLElement | null>(null);
const spot = ref<Box | null>(null);
const cardAt = ref<{ left: number; top: number } | null>(null);

const PAD = 4;
const GAP = 14;
const EDGE = 12;

function targetBox(selectors: string[] | undefined): Box | null {
  if (!selectors) return null;
  let l = Infinity, t = Infinity, r = -Infinity, b = -Infinity;
  for (const sel of selectors) {
    const el = document.querySelector(sel);
    if (!el) continue;
    const rc = el.getBoundingClientRect();
    if (rc.width === 0 && rc.height === 0) continue;
    l = Math.min(l, rc.left);
    t = Math.min(t, rc.top);
    r = Math.max(r, rc.right);
    b = Math.max(b, rc.bottom);
  }
  if (!(r > l && b > t)) return null;
  return { left: l - PAD, top: t - PAD, width: r - l + 2 * PAD, height: b - t + 2 * PAD };
}

/** Next to the target on the first side with room, else where it covers the least of the target. */
function placeCard(box: Box | null, cw: number, ch: number, sides: Side[]): { left: number; top: number } {
  const vw = window.innerWidth;
  const vh = window.innerHeight;
  const clampX = (x: number) => Math.max(EDGE, Math.min(vw - cw - EDGE, x));
  const clampY = (y: number) => Math.max(EDGE, Math.min(vh - ch - EDGE, y));
  if (!box) return { left: clampX((vw - cw) / 2), top: clampY((vh - ch) / 2) };
  const right = box.left + box.width;
  const bottom = box.top + box.height;
  const at: Record<Side, { left: number; top: number }> = {
    right: { left: right + GAP, top: clampY(box.top) },
    left: { left: box.left - GAP - cw, top: clampY(box.top) },
    below: { left: clampX(box.left), top: bottom + GAP },
    above: { left: clampX(box.left), top: box.top - GAP - ch },
  };
  const fits = (p: { left: number; top: number }) => p.left >= EDGE && p.top >= EDGE && p.left + cw <= vw - EDGE && p.top + ch <= vh - EDGE;
  for (const side of sides) if (fits(at[side])) return at[side];
  let best = { left: clampX(at[sides[0] ?? 'right'].left), top: clampY(at[sides[0] ?? 'right'].top) };
  let least = Infinity;
  for (const side of [...sides, ...SIDES.filter(x => !sides.includes(x))]) {
    const p = { left: clampX(at[side].left), top: clampY(at[side].top) };
    const w = Math.max(0, Math.min(p.left + cw, right) - Math.max(p.left, box.left));
    const h = Math.max(0, Math.min(p.top + ch, bottom) - Math.max(p.top, box.top));
    if (w * h < least - 1) {
      least = w * h;
      best = p;
    }
  }
  return best;
}

const same = (a: Box | null, b: Box | null) => (!a || !b ? a === b
  : Math.abs(a.left - b.left) < 0.5 && Math.abs(a.top - b.top) < 0.5 && Math.abs(a.width - b.width) < 0.5 && Math.abs(a.height - b.height) < 0.5);

// Follow the target while the tutorial is open: panels open, resize and move as people use the page.
let raf = 0;
function track() {
  raf = requestAnimationFrame(track);
  const s = step.value;
  if (!s || !card.value) return;
  const box = targetBox(s.target);
  if (!same(box, spot.value)) spot.value = box;
  const at = placeCard(box, card.value.offsetWidth, card.value.offsetHeight, s.place ?? SIDES);
  const c = cardAt.value;
  if (!c || Math.abs(c.left - at.left) > 0.5 || Math.abs(c.top - at.top) > 0.5) cardAt.value = at;
  // A hidden card cannot take the focus: wait until it is placed and shown.
  if (focusPending && card.value.style.visibility !== 'hidden') {
    focusPending = false;
    card.value.querySelector<HTMLElement>('.rv-tour-foot button:last-child')?.focus();
  }
}

/** Keyboard users go on with Enter or Space: Next gets the focus at every step. */
let focusPending = false;

watch(() => tutorial.open.value, (open) => {
  cancelAnimationFrame(raf);
  if (!open) {
    spot.value = null;
    cardAt.value = null;
    return;
  }
  focusPending = true;
  raf = requestAnimationFrame(track);
}, { immediate: true });

watch(index, () => {
  focusPending = true;
});

function onKey(e: KeyboardEvent) {
  if (e.key === 'Escape' && tutorial.open.value) tutorial.close();
}
onMounted(() => window.addEventListener('keydown', onKey));
onUnmounted(() => {
  window.removeEventListener('keydown', onKey);
  cancelAnimationFrame(raf);
  tutorial.close();
});

const spotStyle = computed(() => {
  const b = spot.value!;
  return { left: `${b.left}px`, top: `${b.top}px`, width: `${b.width}px`, height: `${b.height}px` };
});

// Hidden until placed, so the card does not flash in a corner first.
const cardStyle = computed(() => (cardAt.value
  ? { left: `${cardAt.value.left}px`, top: `${cardAt.value.top}px` }
  : { left: '0px', top: '0px', visibility: 'hidden' as const }));

/** A paragraph split into text and [[key]] parts. */
function withKeys(text: string): { text: string; key: boolean }[] {
  return text.split(/(\[\[[^\]]+\]\])/).filter(Boolean).map(p => (p.startsWith('[[')
    ? { text: p.slice(2, -2), key: true }
    : { text: p, key: false }));
}
</script>

<style scoped>
.rv-tour {
  position: fixed;
  inset: 0;
  z-index: 60;
  pointer-events: none;
}

/* Dims everything but the target, without blocking it: people can try things as they read. */
.rv-tour-spot {
  position: fixed;
  border-radius: 4px;
  outline: 2px solid var(--rv-accent);
  box-shadow: 0 0 0 100vmax rgb(31 35 40 / 0.28);
  transition: left 0.18s ease, top 0.18s ease, width 0.18s ease, height 0.18s ease;
}

.rv-tour-dim {
  position: fixed;
  inset: 0;
  background: rgb(31 35 40 / 0.28);
}

.rv-tour-card {
  position: fixed;
  width: 360px;
  max-width: calc(100vw - 24px);
  max-height: calc(100vh - 24px);
  overflow-y: auto;
  display: flex;
  flex-direction: column;
  gap: 8px;
  padding: 14px 16px 12px;
  border: 1px solid var(--rv-rule-strong);
  border-radius: 6px;
  background: var(--rv-bg);
  color: var(--rv-ink);
  box-shadow: 0 8px 28px rgb(31 35 40 / 0.18);
  pointer-events: auto;
  transition: left 0.18s ease, top 0.18s ease;
}

.rv-tour-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.rv-tour-count {
  font-size: 11px;
  font-weight: 600;
  letter-spacing: 0.06em;
  text-transform: uppercase;
  color: var(--rv-muted);
  font-variant-numeric: tabular-nums;
}

.rv-tour-close {
  display: grid;
  place-items: center;
  width: 24px;
  height: 24px;
  margin: -4px -6px 0 0;
  border-radius: 4px;
  color: var(--rv-muted);
  cursor: pointer;
}

.rv-tour-close:hover {
  background: var(--rv-hover);
  color: var(--rv-ink);
}

.rv-tour-close:focus-visible {
  outline: 2px solid var(--rv-accent);
}

.rv-tour-close svg {
  width: 12px;
  height: 12px;
  stroke: currentColor;
  stroke-width: 1.5;
  stroke-linecap: round;
}

.rv-tour-title {
  margin: 0;
  font-size: 15px;
  font-weight: 600;
  text-wrap: balance;
}

.rv-tour-text {
  margin: 0;
  font-size: 13px;
  line-height: 1.5;
}

.rv-tour-list {
  display: grid;
  gap: 2px;
  padding-left: 18px;
  list-style: disc;
}

.rv-tour-text kbd {
  display: inline-block;
  min-width: 1.6em;
  padding: 0 4px;
  border: 1px solid var(--rv-rule-strong);
  border-bottom-width: 2px;
  border-radius: 3px;
  background: var(--rv-side);
  font-family: var(--font-mono);
  font-size: 11px;
  line-height: 1.5;
  text-align: center;
}

.rv-tour-foot {
  display: flex;
  justify-content: flex-end;
  gap: 6px;
  margin-top: 4px;
}

@media (prefers-reduced-motion: reduce) {
  .rv-tour-spot,
  .rv-tour-card {
    transition: none;
  }
}
</style>
