// The sidechains the app runs (doc/unified-app.md): their list from the host, kept fresh by its
// "sidechains" events, the chain chosen in the switcher, and the calls of Settings → Sidechains.
import { on, request } from './bridge';
import { go, nav } from './nav.svelte';
import type { SidechainList, SidechainOverview } from './unified';

export const hub = $state({
  list: null as SidechainList | null,
  error: '',
  /** The chain chosen in the switcher: 'chains', or the stem of a sidechain. */
  selected: 'chains',
  /** The wizard open: one sidechain to turn on, or the first-start guide. */
  wizard: null as null | { kind: 'enable'; stem: string } | { kind: 'firstrun' },
});

export const sidechainHost = {
  list: () => request<SidechainList>('host', 'sidechains.list', {}),
  enable: (stem: string, wallet: 'shared' | 'separate') => request<boolean>('host', 'sidechains.enable', { stem, wallet }),
  disable: (stem: string) => request<boolean>('host', 'sidechains.disable', { stem }),
  chooseDataDir: (stem: string) => request<{ path: string; problem: string; freeGB?: number } | null>('host', 'sidechains.chooseDataDir', { stem }),
  setDataDir: (stem: string, dir: string | null, move: boolean) => request<boolean>('host', 'sidechains.setDataDir', { stem, dir, move }),
  deleteData: (stem: string, wallets: boolean) => request<boolean>('host', 'sidechains.deleteData', { stem, wallets }),
  overview: (stem: string) => request<SidechainOverview>('host', 'sidechains.overview', { stem }),
  log: (stem: string) => request<{ node: string; supervisor: string }>('host', 'sidechains.log', { stem }),
};

let refreshing: Promise<void> | null = null;

export function refreshSidechains(): Promise<void> {
  if (refreshing) return refreshing;
  refreshing = (async () => {
    try {
      hub.list = await sidechainHost.list();
      hub.error = '';
      // A sidechain turned off while shown: back to Chains.
      if (hub.selected !== 'chains' && !hub.list.chains.some((c) => c.stem === hub.selected && (c.enabled || c.state === 'stopping'))) selectChain('chains');
    } catch (e) {
      hub.error = e instanceof Error ? e.message : String(e);
    } finally {
      refreshing = null;
    }
  })();
  return refreshing;
}

let watching = false;
/** Follow the host's changes (once, for the life of the page). */
export function watchSidechains(): void {
  if (watching) return;
  watching = true;
  let queued = false;
  on('sidechains', () => {
    // A burst of changes (a start: starting, running, its wallet): one refresh.
    if (queued) return;
    queued = true;
    setTimeout(() => {
      queued = false;
      void refreshSidechains();
    }, 150);
  });
}

/** Show a chain: Chains (its pages, from Home), or a sidechain (its overview). */
export function selectChain(id: string): void {
  hub.selected = id;
  if (id === 'chains') {
    if (nav.page === 'sidechain') go('home');
  } else {
    go('sidechain');
  }
}

if (typeof window !== 'undefined') (window as unknown as Record<string, unknown>).__chainsHub = hub;
