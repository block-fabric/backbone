// Back and forward, owned by the app (doc/unified-app.md §14): the places the user went, the app's own pages and the
// pages of the modules alike, as one list. Alt+Left / Alt+Right, the back and forward buttons of a mouse, the arrows of
// the header; a module's frame forwards those keys to the app, it has no history of its own.
import { untrack } from 'svelte';
import { go, nav, PAGES, type PageId } from './nav.svelte';
import { hub } from './sidechains.svelte';
import { activeModule, moduleRoute, openModule } from './modules.svelte';

export interface Place {
  /** 'chains' for the app's own pages, else a module's stem. */
  chain: string;
  page: string;
  tab: string;
}

const MAX = 100;
const back: Place[] = [];
const forward: Place[] = [];
let current: Place | null = null;
/** A place being restored: when the app gets there, it is not a new step. */
let restoring: string | null = null;

export const history = $state({ canBack: false, canForward: false });

const key = (p: Place) => `${p.chain}|${p.page}|${p.tab}`;

function update() {
  history.canBack = back.length > 0;
  history.canForward = forward.length > 0;
}

/** Where the user is now. */
export function here(): Place {
  const stem = activeModule();
  if (stem) {
    const r = moduleRoute(stem);
    return { chain: stem, page: r.page, tab: r.tab };
  }
  return { chain: 'chains', page: nav.page, tab: nav.tab };
}

/** A new place (the effect of the shell calls this on every move). */
export function record(place: Place): void {
  if (restoring !== null) {
    if (restoring === key(place)) restoring = null;
    current = place;
    return;
  }
  if (current && key(current) === key(place)) return;
  if (current) {
    back.push(current);
    if (back.length > MAX) back.shift();
  }
  forward.length = 0;
  current = place;
  update();
}

function restore(place: Place): void {
  restoring = key(place);
  if (place.chain === 'chains') {
    hub.selected = 'chains';
    if (place.page in PAGES) go(place.page as PageId, place.tab);
  } else if (hub.list?.chains.some((c) => c.stem === place.chain && c.enabled)) {
    openModule(place.chain, place.page, place.tab);
  } else {
    // A module turned off since: the step is skipped.
    restoring = null;
    return;
  }
  update();
}

export function goBack(): boolean {
  const to = back.pop();
  if (!to) return false;
  if (current) forward.push(current);
  restore(to);
  return true;
}

export function goForward(): boolean {
  const to = forward.pop();
  if (!to) return false;
  if (current) back.push(current);
  restore(to);
  return true;
}

/** Follow the moves (Shell.svelte, once). */
export function followHistory(): void {
  $effect(() => {
    const place = here();
    untrack(() => record(place));
  });
}

/** For the tests. */
export function resetHistory(): void {
  back.length = 0;
  forward.length = 0;
  current = null;
  restoring = null;
  update();
}
