// Draw on demand: panels, the overview and the leader lines register a draw function; anything that
// changes what they show calls invalidate(), and they all draw once in the next animation frame.
// Nothing is drawn while nothing changes.

export interface FrameScheduler {
  add(draw: () => void): () => void;
  invalidate(): void;
  /** Frames drawn so far (for automated checks). */
  readonly count: number;
}

/** `step` runs first in every frame and returns true while an animation needs further frames. */
export function createFrameScheduler(step: () => boolean): FrameScheduler {
  const draws = new Set<() => void>();
  let raf = 0;
  let count = 0;

  function frame() {
    raf = 0;
    count++;
    const animating = step();
    for (const d of draws) d();
    if (animating) invalidate();
  }

  function invalidate() {
    if (!raf && import.meta.client) raf = requestAnimationFrame(frame);
  }

  return {
    add(draw) {
      draws.add(draw);
      invalidate();
      return () => draws.delete(draw);
    },
    invalidate,
    get count() {
      return count;
    },
  };
}
