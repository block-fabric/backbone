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
 * `scope`, when given, says what the value is of (the chains shown): a change of it is followed at once, so that a part
 * never shows another chain's data after the user picked one (only the data's own changes wait).
 * To be made while a component is made (it uses an $effect); `.value` is reactive.
 */
export function throttled<T>(get: () => T, ms: number, scope?: () => string): { readonly value: T } {
  let value = $state.raw(untrack(get));
  let of = scope ? untrack(scope) : '';
  const born = Date.now();
  let at = 0;
  $effect(() => {
    const next = get();
    const now_of = scope ? scope() : '';
    if (next === untrack(() => value)) return;
    const now = Date.now();
    const wait = now - born < WARMUP_MS || now_of !== of ? 0 : at + ms - now;
    if (wait <= 0) {
      at = now;
      of = now_of;
      value = next;
      return;
    }
    const timer = setTimeout(() => {
      at = Date.now();
      of = scope ? untrack(scope) : '';
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
