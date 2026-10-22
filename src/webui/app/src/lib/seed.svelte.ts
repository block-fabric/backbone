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
  /** false: its keys never come from the phrase (zSide: its private funds need its own backup). */
  phraseKeys?: boolean;
}

/** The mining wallet (doc/unified-app.md §23.12): it pays merged mining's bids so the main wallet can stay locked. */
export interface MiningRecord {
  wallet: string;
  /** From the phrase (account 1 of the app's chain) or with keys of its own (no phrase). */
  from: 'phrase' | 'own';
  on: boolean;
}

export interface SeedStatus {
  exists: boolean;
  damaged?: boolean;
  words?: number;
  created?: number;
  restored?: boolean;
  fingerprint?: string;
  extraWord?: boolean;
  /** false: the phrase and its wallets have no password (the user's choice): anyone using this computer can spend. */
  password?: boolean;
  mining?: MiningRecord;
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
  password?: boolean;
}

export const seedHost = {
  status: () => request<SeedStatus>('host', 'seed.status', {}),
  /** password: false makes the phrase and its wallets without one (said plainly in the page before). */
  create: (words: 12 | 24, extraWord: boolean, password = true) => request<SeedDone>('host', 'seed.create', { words, extraWord, password }),
  restore: (password = true) => request<SeedDone>('host', 'seed.restore', { password }),
  unlock: (stems: string[] = []) => request<SeedDone>('host', 'seed.unlock', { stems }),
  lock: () => request<boolean>('host', 'seed.lock', {}),
  addChain: (stem: string) => request<SeedDone>('host', 'seed.addChain', { stem }),
  show: () => request<{ shown?: boolean; cancelled?: boolean }>('host', 'seed.show', {}),
  changePassphrase: () => request<{ changed?: boolean; cancelled?: boolean; wallets?: number }>('host', 'seed.changePassphrase', {}),
  addPassword: () => request<{ added?: boolean; cancelled?: boolean; locked?: number }>('host', 'seed.addPassword', {}),
  removePassword: () => request<{ removed?: boolean; cancelled?: boolean }>('host', 'seed.removePassword', {}),
  dismiss: (what: 'firstRun' | 'offer') => request<boolean>('host', 'seed.dismiss', { what }),
};

/** What pays merged mining's bids now, and whether it can (mining.status). */
export interface MiningStatus {
  /** A mining wallet pays them (else the main wallet). */
  on: boolean;
  main: string;
  paying: string;
  payingPresent: boolean;
  payingEncrypted: boolean;
  payingLocked: boolean;
  mining?: { wallet: string; from: 'phrase' | 'own'; loaded: boolean; trusted?: number | string; pending?: number | string };
  /** The sidechains whose merged mining is on, and what each last said. */
  bidding: { stem: string; name: string; error?: string }[];
  /** '' fine; 'locked' the main wallet pays and is locked; 'mining-locked'; 'empty' the mining wallet holds nothing; 'missing'. */
  problem: '' | 'locked' | 'mining-locked' | 'empty' | 'missing';
  phrase: boolean;
}

export const miningHost = {
  status: () => request<MiningStatus>('host', 'mining.status', {}),
  create: () => request<{ wallet?: string; from?: string; cancelled?: boolean }>('host', 'mining.create', {}),
  stop: () => request<{ wallet?: string }>('host', 'mining.stop', {}),
};

/** Merged mining's problem in plain words (or ''), for the notices of Home, Sidechains and Settings. */
export function miningProblem(m: MiningStatus | null): string {
  if (!m) return '';
  switch (m.problem) {
    case 'locked':
      return 'Merged mining cannot pay its bids: your wallet has a password and is locked. Use a separate mining wallet, or unlock your wallet for a while.';
    case 'mining-locked':
      return 'Merged mining cannot pay its bids: the mining wallet is locked.';
    case 'empty':
      return 'Merged mining cannot pay its bids: your mining wallet is empty. Top it up from your wallet.';
    case 'missing':
      return 'Merged mining cannot pay its bids: the wallet that pays them is not open.';
    default:
      return '';
  }
}

/**
 * What a sidechain's merged mining last said when it could not bid (getbmminfo.error), in plain words: the wallet that pays
 * its bids is locked, empty, or not open; else what it said.
 */
export function bmmErrorWords(error: string | undefined | null): { text: string; settings: boolean } | null {
  if (!error) return null;
  if (/walletpassphrase|unlock needed|is locked|wallet passphrase/i.test(error)) {
    return { text: 'Its bids cannot be paid: the wallet that pays them is locked (it has a password). Use a separate mining wallet, or unlock your wallet for a while.', settings: true };
  }
  if (/insufficient funds|not enough/i.test(error)) return { text: 'Its bids cannot be paid: the wallet that pays them has no coins to spend. Top it up.', settings: true };
  if (/wallet file not specified|does not exist or is not loaded|no wallet/i.test(error)) return { text: 'Its bids cannot be paid: the wallet that pays them is not open.', settings: true };
  return { text: error, settings: false };
}

/**
 * How the app's own wallet named stands with the backup phrase: 'open' (the phrase's, without a password: anyone using this
 * computer can spend), 'password' (the phrase's, with one: its password is the phrase's, changed in Settings → Wallet), or
 * null (another wallet).
 */
export function phraseWalletState(status: SeedStatus | null, name: string | null | undefined): 'open' | 'password' | null {
  if (!status?.exists || !name) return null;
  const app = status.chains.find((c) => c.id === 'chains');
  if (!app || app.mode !== 'phrase' || app.wallet !== name) return null;
  return status.password === false ? 'open' : 'password';
}

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
      if (c.phraseKeys === false) return { text: 'Its own wallet: its private funds are not covered by your backup phrase', tone: 'warn' };
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
/** zSide's notice (doc/unified-app.md §23.13), said wherever the phrase is said to cover the sidechains. */
export const ZSIDE_NOTE = 'zSide’s private (shielded) funds are not covered by your backup phrase: zSide keeps a wallet of its own, which needs its own backup.';

export function allReady(status: SeedStatus | null, ids?: string[]): boolean {
  if (!status) return false;
  return status.chains.filter((c) => !ids || ids.includes(c.id)).every((c) => c.mode !== 'phrase' || c.state === 'ready' || c.state === 'off');
}

if (typeof window !== 'undefined') (window as unknown as Record<string, unknown>).__chainsSeed = seed;
