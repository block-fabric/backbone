// A value of the page that follows its source at most once every so often (doc/unified-app.md §21.3): a part of a page
// whose data changes with every block and payment of every chain (a chart of balances, a list of the latest
// transactions) is drawn again at most once a period, the newest each time, not at each change. Under software GL (WSLg)
// every change of a page is a frame of the whole window drawn on every core: on the hub's 3440 × 1380 window a
// fraction of a core each.
import { untrack } from 'svelte';

/** The first moments of a part, while what it shows arrives (a chain after the other): followed at once. */
export const WARMUP_MS = 3000;

/**
 * `get` followed at most once every `ms` (at once during the first WARMUP_MS): the newest, `ms` after the one before.
 * To be made while a component is made (it uses an $effect); `.value` is reactive.
 */
export function throttled<T>(get: () => T, ms: number): { readonly value: T } {
  let value = $state.raw(untrack(get));
  const born = Date.now();
  let at = 0;
  $effect(() => {
    const next = get();
    if (next === untrack(() => value)) return;
    const now = Date.now();
    const wait = now - born < WARMUP_MS ? 0 : at + ms - now;
    if (wait <= 0) {
      at = now;
      value = next;
      return;
    }
    const timer = setTimeout(() => {
      at = Date.now();
      value = untrack(get);
    }, wait);
    return () => clearTimeout(timer);
  });
  return {
    get value() {
      return value;
    },
  };
}
