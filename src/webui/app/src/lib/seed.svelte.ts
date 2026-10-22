// The backup phrase of every chain (doc/unified-app.md §23): what the host says of it, and its calls. The phrase itself
// never comes here: every call that makes, restores, shows or unlocks it asks in the host's own dialog window (its words,
// its passphrase), and answers only what a page may know: made or not, and the state of each chain's wallet.
import { on, request } from './bridge';

export type SeedChainState = 'ready' | 'off' | 'own' | 'waiting' | 'needs-keys' | 'importing' | 'scanning' | 'error';

export interface SeedChain {
  /** 'chains' for the app's own chain (HOST), else the sidechain's stem. */
  id: string;
  name: string;
  /** From the phrase ('phrase'), its own wallet by choice ('separate'), or its own because there is no phrase yet ('own'). */
  mode: 'phrase' | 'separate' | 'own';
  state: SeedChainState;
  wallet?: string;
  account?: number;
  /** Of a rescan, 0 to 1. */
  progress?: number;
  error?: string;
}

export interface SeedStatus {
  exists: boolean;
  damaged?: boolean;
  words?: number;
  created?: number;
  restored?: boolean;
  fingerprint?: string;
  extraWord?: boolean;
  unlocked: boolean;
  expiresIn?: number;
  nodeReady: boolean;
  hasWallets?: boolean;
  /** No phrase, no wallet: the first start (the guide opens by itself). */
  firstRun: boolean;
  /** No phrase, wallets made before: the offer to make one (once, decision 6). */
  offer: boolean;
  minPassphrase: number;
  chains: SeedChain[];
}

export interface SeedDone {
  cancelled?: boolean;
  created?: boolean;
  restored?: boolean;
  unlocked?: boolean;
  fingerprint?: string;
  words?: number;
}

export const seedHost = {
  status: () => request<SeedStatus>('host', 'seed.status', {}),
  create: (words: 12 | 24, extraWord: boolean) => request<SeedDone>('host', 'seed.create', { words, extraWord }),
  restore: () => request<SeedDone>('host', 'seed.restore', {}),
  unlock: (stems: string[] = []) => request<SeedDone>('host', 'seed.unlock', { stems }),
  lock: () => request<boolean>('host', 'seed.lock', {}),
  addChain: (stem: string) => request<SeedDone>('host', 'seed.addChain', { stem }),
  show: () => request<{ shown?: boolean; cancelled?: boolean }>('host', 'seed.show', {}),
  changePassphrase: () => request<{ changed?: boolean; cancelled?: boolean; wallets?: number }>('host', 'seed.changePassphrase', {}),
  dismiss: (what: 'firstRun' | 'offer') => request<boolean>('host', 'seed.dismiss', { what }),
};

export const seed = $state({
  status: null as SeedStatus | null,
  error: '',
  /** Which guide shows: the first start, the move to a phrase of existing users, the guided move of coins. */
  open: null as null | 'firstrun' | 'migrate' | 'move',
  /**
   * The guides open by themselves (the first run, the offer): on once the app's info says it is not a test of the app
   * (defaultForTests), never before it is known; the tests of the app set it themselves.
   */
  autoOpen: false,
  /** Opened by itself once in this run already: not again (a dismissal is the host's). */
  autoOpened: false,
});

let refreshing: Promise<void> | null = null;
let again = false;
/** The host's state read again; asked while a read is under way, read once more after it (that read may be from before). */
export function refreshSeed(): Promise<void> {
  if (refreshing) {
    again = true;
    return refreshing;
  }
  refreshing = (async () => {
    try {
      seed.status = await seedHost.status();
      seed.error = '';
    } catch (e) {
      seed.error = e instanceof Error ? e.message : String(e);
    } finally {
      refreshing = null;
      if (again) {
        again = false;
        void refreshSeed();
      }
    }
  })();
  return refreshing;
}

let testDefaulted = false;
/**
 * In the tests of the app the guides do not open by themselves (they would cover the pages the tests drive) unless a test
 * asks (seed.autoOpen): set once for the life of the page, whatever is mounted again.
 */
export function defaultForTests(test: boolean): void {
  if (testDefaulted) return;
  testDefaulted = true;
  if (!test) seed.autoOpen = true;
}

/** Which guide opens by itself, if any: the first run, else the offer; once per run, never over another. */
export function autoGuide(status: SeedStatus | null, state: { open: unknown; autoOpen: boolean; autoOpened: boolean }): null | 'firstrun' | 'migrate' {
  if (!status || !status.nodeReady || state.open || !state.autoOpen || state.autoOpened) return null;
  if (status.firstRun) return 'firstrun';
  if (status.offer) return 'migrate';
  return null;
}

let watching = false;
/** Follow the host's changes of the phrase (event "seed") and of the wallets (a wallet made or loaded). */
export function watchSeed(): void {
  if (watching) return;
  watching = true;
  let queued = false;
  const later = () => {
    if (queued) return;
    queued = true;
    setTimeout(() => {
      queued = false;
      void refreshSeed();
    }, 150);
  };
  on('seed', later);
  on('sidechains', later);
  on('wallet.loaded', later);
  on('wallet.unloaded', later);
}

/** A chain's line in words: where its keys come from and how it is doing. */
export function chainWords(c: SeedChain): { text: string; tone: 'ok' | 'busy' | 'warn' | 'bad' | 'off' } {
  switch (c.state) {
    case 'ready':
      return { text: 'From your backup phrase', tone: 'ok' };
    case 'off':
      return { text: c.mode === 'phrase' ? 'From your backup phrase · turned off' : 'Turned off', tone: 'off' };
    case 'own':
      return { text: c.mode === 'separate' ? 'Its own wallet: back it up on its own' : 'Its own wallet, made before your backup phrase', tone: 'warn' };
    case 'waiting':
      return { text: 'Waiting for its node to start', tone: 'busy' };
    case 'needs-keys':
      return { text: 'Its keys are not made yet', tone: 'warn' };
    case 'importing':
      return { text: 'Making its keys…', tone: 'busy' };
    case 'scanning':
      return { text: 'Reading its chain again for your coins…', tone: 'busy' };
    case 'error':
      return { text: 'Its keys could not be made', tone: 'bad' };
  }
}

/** "40 %" of a rescan, or nothing. */
export function progressText(c: SeedChain): string {
  return typeof c.progress === 'number' && Number.isFinite(c.progress) ? `${Math.round(Math.max(0, Math.min(1, c.progress)) * 100)} %` : '';
}

/** Whether every chain whose wallet is the phrase's has its keys (nothing being made, nothing waiting). */
export function allReady(status: SeedStatus | null, ids?: string[]): boolean {
  if (!status) return false;
  return status.chains.filter((c) => !ids || ids.includes(c.id)).every((c) => c.mode !== 'phrase' || c.state === 'ready' || c.state === 'off');
}

if (typeof window !== 'undefined') (window as unknown as Record<string, unknown>).__chainsSeed = seed;
