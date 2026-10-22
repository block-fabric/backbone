// The unified app (doc/unified-app.md): Chains alone by default, sidechains run by the app on request.
// What the pages of it decide, without the host: the words of a state, the chain switcher, the steps
// of the wizards, the checks of a data folder. The host side is webui/supervisor.cpp (sidechains.*).

/** States of a sidechain's daemon, as the supervisor names them. */
export type SidechainState = 'disabled' | 'stopped' | 'starting' | 'running' | 'stopping' | 'crashed' | 'failed';

export interface SidechainEntry {
  stem: string;
  name: string;
  description: string;
  slot: number;
  diskGB: number;
  ramMB: number;
  staking: boolean;
  /** A chain that stakes: whether its node runs with a validator key this app made on this computer (doc §24.2). */
  validatorKey?: boolean;
  /** false: its wallet never comes from the backup phrase (zSide: its private funds need its own backup). */
  phraseKeys?: boolean;
  enabled: boolean;
  state: SidechainState;
  pid: number;
  restarts: number;
  ready: boolean;
  error?: string;
  dataDir: string;
  defaultDataDir: string;
  customDataDir: boolean;
  hasData: boolean;
  freeGB?: number;
  program: string;
  rpcPort: number;
  port: number;
  wallet: 'shared' | 'separate';
}

export interface SidechainList {
  network: string;
  rpcServer: boolean;
  chainsDataDir: string;
  chains: SidechainEntry[];
}

export interface SidechainBlock {
  height: number;
  hash: string;
  time: number;
  txs: number;
}

export interface SidechainOverview {
  stem: string;
  name: string;
  state: SidechainState;
  mainchainHeight: number;
  height?: number;
  headers?: number;
  progress?: number;
  bestBlockHash?: string;
  mainchainConnected?: boolean;
  mainchainSeen?: number;
  mainchainError?: string;
  synced?: boolean;
  blocks?: SidechainBlock[];
  wallet?: { name: string; trusted: number; pending: number; immature: number };
  walletError?: string;
  error?: string;
}

export type Tone = 'ok' | 'busy' | 'warn' | 'bad' | 'off';

/** Words of the app with their values: shown as t(key, ...args). */
export interface Said {
  key: string;
  args: (string | number)[];
  tone: Tone;
}

const said = (key: string, tone: Tone, ...args: (string | number)[]): Said => ({ key, args, tone });

/** How a sidechain is doing, in plain words. */
export function describeState(e: Pick<SidechainEntry, 'state' | 'enabled'> & { ready?: boolean }): Said {
  switch (e.state) {
    case 'running':
      return e.ready === false ? said('Starting…', 'busy') : said('Running', 'ok');
    case 'starting':
      return said('Starting…', 'busy');
    case 'stopping':
      return said('Stopping…', 'busy');
    case 'crashed':
      return said('Restarting after a problem', 'warn');
    case 'failed':
      return said('Stopped: needs your attention', 'bad');
    case 'stopped':
      return e.enabled ? said('Waiting to start', 'busy') : said('Off', 'off');
    default:
      return said('Off', 'off');
  }
}

/** Whether a sidechain is in step with Chains (the switcher, the overview). */
export function describeSync(o: SidechainOverview | null | undefined): Said {
  if (!o) return said('Loading…', 'busy');
  if (o.state !== 'running') return describeState({ state: o.state, enabled: true });
  if (o.error) return said('Cannot be read right now', 'warn');
  if (o.height === undefined) return said('Starting…', 'busy');
  if (!o.mainchainConnected) return said('Not connected to Chains yet', 'warn');
  if (o.synced) return said('In step with Chains', 'ok');
  return said('Catching up with Chains: block %1 of %2', 'busy', Math.max(0, o.mainchainSeen ?? 0), o.mainchainHeight);
}

export interface SwitcherItem {
  id: string;
  name: string;
  tone: Tone;
}

/** The chain switcher: Chains, then each sidechain turned on. Nothing to switch between: no switcher. */
export function switcherItems(list: SidechainList | null | undefined, product = 'Chains'): SwitcherItem[] {
  const on = (list?.chains ?? []).filter((c) => c.enabled || c.state === 'stopping');
  if (on.length === 0) return [];
  return [{ id: 'chains', name: product, tone: 'ok' }, ...on.map((c) => ({ id: c.stem, name: c.name, tone: describeState(c).tone }))];
}

/** "About 4 GB of disk and 400 MB of memory". */
export function resources(e: Pick<SidechainEntry, 'diskGB' | 'ramMB'>): Said {
  return said('About %1 GB of disk and %2 MB of memory', 'off', e.diskGB, e.ramMB);
}

/** Room for a sidechain where its data would go. */
export function diskCheck(needGB: number, freeGB: number | undefined): Said {
  if (freeGB === undefined || !Number.isFinite(freeGB)) return said('The free space there is not known.', 'warn');
  const free = Math.floor(freeGB);
  if (freeGB < needGB) return said('Not enough room: %1 GB free, about %2 GB needed.', 'bad', free, needGB);
  if (freeGB < needGB + 10) return said('Tight: %1 GB free, about %2 GB needed. It grows with time.', 'warn', free, needGB);
  return said('%1 GB free: enough room.', 'ok', free);
}

// ---- The wizard that turns one sidechain on ----

export type EnableStep = 'about' | 'program' | 'storage' | 'wallet' | 'staking' | 'review';

export interface EnableChoices {
  storage: 'default' | 'custom';
  customPath: string;
  /** Why the host refused the folder chosen ('' when it can be used). */
  customProblem: string;
  customFreeGB?: number;
  /** Move the data already there along to the new folder. */
  move: boolean;
  wallet: 'shared' | 'separate';
  stake: boolean;
  /** Its program is on this computer (next to the app, in a folder chosen, or downloaded in the wizard). */
  programReady: boolean;
}

/** The steps for a sidechain: its program (the download, §15), staking only for one that can stake. */
export function enableSteps(e: Pick<SidechainEntry, 'staking'>): EnableStep[] {
  return e.staking ? ['about', 'program', 'storage', 'wallet', 'staking', 'review'] : ['about', 'program', 'storage', 'wallet', 'review'];
}

export function defaultChoices(e: SidechainEntry): EnableChoices {
  return {
    storage: e.customDataDir ? 'custom' : 'default',
    customPath: e.customDataDir ? e.dataDir : '',
    customProblem: '',
    move: e.hasData,
    // zSide: always its own wallet (its shielded keys are not made from the phrase).
    wallet: e.phraseKeys === false ? 'separate' : (e.wallet ?? 'shared'),
    stake: false,
    programReady: !!e.program,
  };
}

/** Whether the wizard can go on from a step. */
export function canContinue(step: EnableStep, c: EnableChoices): boolean {
  if (step === 'storage' && c.storage === 'custom') return c.customPath !== '' && c.customProblem === '';
  if (step === 'program') return c.programReady;
  return true;
}

/** The next step, or null past the last. */
export function nextStep(steps: EnableStep[], step: EnableStep): EnableStep | null {
  const i = steps.indexOf(step);
  return i >= 0 && i + 1 < steps.length ? steps[i + 1] : null;
}

export function previousStep(steps: EnableStep[], step: EnableStep): EnableStep | null {
  const i = steps.indexOf(step);
  return i > 0 ? steps[i - 1] : null;
}

/** What the host is asked to do when the user says "Turn it on". */
export function enablePlan(e: SidechainEntry, c: EnableChoices): { setDir: { dir: string | null; move: boolean } | null; wallet: 'shared' | 'separate'; stake: boolean } {
  const wantCustom = c.storage === 'custom' ? c.customPath : null;
  const now = e.customDataDir ? e.dataDir : null;
  const setDir = wantCustom === now ? null : { dir: wantCustom, move: c.move && e.hasData };
  // Staking: a validator key of this computer, made before it starts (only a chain that stakes).
  return { setDir, wallet: e.phraseKeys === false ? 'separate' : c.wallet, stake: !!e.staking && c.stake };
}

// ---- The first start of the app ----

export type FirstRunStep = 'welcome' | 'wallet' | 'backup' | 'restore' | 'network' | 'sidechains' | 'storage' | 'sync';

export interface FirstRunChoices {
  wallet: 'create' | 'restore';
  picked: string[];
  storage: 'unified' | 'advanced';
}

export function firstRunSteps(c: FirstRunChoices): FirstRunStep[] {
  const steps: FirstRunStep[] = ['welcome', 'wallet', c.wallet === 'create' ? 'backup' : 'restore', 'network', 'sidechains'];
  if (c.picked.length > 0) steps.push('storage');
  steps.push('sync');
  return steps;
}

/** What the sidechains picked take, together. */
export function totalResources(list: SidechainEntry[], picked: string[]): { diskGB: number; ramMB: number } {
  return list.filter((e) => picked.includes(e.stem)).reduce((sum, e) => ({ diskGB: sum.diskGB + e.diskGB, ramMB: sum.ramMB + e.ramMB }), { diskGB: 0, ramMB: 0 });
}

/** Twelve words of the backup, as a grid (phase 3 makes them; until then the screen shows its layout). */
export function seedGrid(words: string[]): { n: number; word: string }[] {
  return words.map((word, i) => ({ n: i + 1, word }));
}

/** The words a user typed to restore: lower case, single spaces; 12 or 24 of them. */
export function restoreWords(text: string): { words: string[]; ok: boolean } {
  const words = text.trim().toLowerCase().split(/\s+/).filter(Boolean);
  return { words, ok: words.length === 12 || words.length === 24 };
}
