// The app's own pages over every chain it runs (doc/unified-app.md §18). Each sidechain turned on adds itself to the
// matching page of the app: Home and Overview show every chain at once, Send and Receive have a chain to choose,
// Transactions lists every chain with a Chain column, Manage wallets lists each chain's wallets, Sidechains moves coins
// into and out of each, Staking and Assets gather what the chains that have them hold.
//
// One adapter per chain answers the same questions: this app's own chain from its own state (wallets, the history),
// a sidechain from the host's chain.* methods (webui/chainadapter.h), which read its node and build its spends from
// checked parameters; every spend on a sidechain is that chain's own question, asked in the app's trusted dialog.
//
// Read views show many chains by default, with a filter (All, or one chain); actions act on one chain, chosen in a
// picker. The filter or the chain of each page is remembered (prefs.chainViews).
import { request } from './bridge';
import { prefs, savePrefs, wallets } from './state.svelte';
import { hub } from './sidechains.svelte';
import { modules, productName, productUnit } from './modules.svelte';
import type { SidechainEntry } from './unified';

/** The id of this app's own chain in every list of chains. */
export const HOST = 'chains';
/** Every chain together, in a filter. */
export const ALL = 'all';

export interface ChainCaps {
  stem: string;
  decimals: number;
  assets: boolean;
  dex: boolean;
  staking: boolean;
  names: boolean;
  markets: boolean;
  shielded: boolean;
  send: boolean;
  receive: boolean;
  deposit: boolean;
  withdraw: boolean;
  /** Its pages the app shows inside its own (the module's frame), where it has no view of its own for them. */
  embed: string[];
}

export interface ChainAsset {
  id: string;
  label: string;
  decimals: number;
  balance: string;
  pending: string;
  control: boolean;
  supply: string;
}

export interface ChainSummary {
  stem: string;
  name: string;
  running: boolean;
  capabilities: ChainCaps;
  wallet?: string;
  wallets?: string[];
  walletMode?: 'shared' | 'separate';
  height?: number;
  mainchain?: { slot?: number; connected?: boolean; state?: string; height?: number; behind_blocks?: number; error?: string };
  balances?: { trusted: string; pending: string; immature: string };
  assets?: ChainAsset[];
  shielded?: { available: string; spendable: string; pending: string };
  withdrawals?: { count: number; amount: string };
  errors?: Record<string, string>;
}

export interface ChainTxRow {
  txid: string;
  vout: number;
  category: string;
  amount: string;
  fee?: string;
  address: string;
  label: string;
  confirmations: number;
  time: number;
  abandoned: boolean;
}

export interface ChainWallet {
  name: string;
  loaded: boolean;
  txcount?: number;
  encrypted?: boolean;
  keys?: boolean;
  trusted?: string;
  error?: string;
}

export interface ChainWallets {
  stem: string;
  mode: 'shared' | 'separate';
  selected: string;
  wallets: ChainWallet[];
}

export interface ChainWithdrawal {
  txid: string;
  vout: number;
  amount: string;
  mainchainfee?: string;
  refundaddress?: string;
  height?: number;
  status: string;
  mine: boolean;
}

export interface ChainPeg {
  stem: string;
  wallet: string;
  mainchain: { slot?: number; connected?: boolean; state?: string; height?: number; tipheight?: number; behind_blocks?: number; error?: string };
  bmm: { mining?: boolean; address?: string; always?: boolean; amount?: string; idle?: boolean; requests?: number; blocks?: number; outbid?: number; error?: string; candidates?: number };
  withdrawals: ChainWithdrawal[];
}

export interface ChainSendParams {
  stem: string;
  wallet?: string;
  address: string;
  /** As typed: the host parses it exactly with the decimals of what is sent. */
  amount: string;
  asset?: string;
  subtractFee?: boolean;
  shielded?: boolean;
  memo?: string;
}

const w = (stem: string, wallet?: string) => ({ stem, ...(wallet ? { wallet } : {}) });

/** The host's chain.* methods (webui/chainadapter.h). */
export const chainHost = {
  capabilities: () => request<ChainCaps[]>('host', 'chain.capabilities', {}),
  summary: (stem: string, wallet?: string) => request<ChainSummary>('host', 'chain.summary', w(stem, wallet)),
  transactions: (stem: string, wallet?: string, count = 200) => request<{ stem: string; wallet: string; rows: ChainTxRow[] }>('host', 'chain.transactions', { ...w(stem, wallet), count }),
  transaction: (stem: string, txid: string, wallet?: string, idx = 0, address = '') => request<unknown>('host', 'chain.transaction', { ...w(stem, wallet), txid, idx, address }),
  wallets: (stem: string, wallet?: string) => request<ChainWallets>('host', 'chain.wallets', w(stem, wallet)),
  walletCreate: (stem: string, name: string) => request<unknown>('host', 'chain.wallet.create', { stem, name }),
  walletLoad: (stem: string, name: string) => request<unknown>('host', 'chain.wallet.load', { stem, name }),
  walletUnload: (stem: string, name: string) => request<unknown>('host', 'chain.wallet.unload', { stem, name }),
  walletBackup: (stem: string, wallet: string) => request<string | null>('host', 'chain.wallet.backup', { stem, wallet }),
  receive: (stem: string, wallet?: string, label = '', shielded = false) => request<{ address: string; wallet: string; shielded?: boolean }>('host', 'chain.receive', { ...w(stem, wallet), label, shielded }),
  depositAddress: (stem: string, wallet?: string) => request<{ depositaddress: string; address: string; slot: number; wallet: string }>('host', 'chain.depositAddress', w(stem, wallet)),
  validate: (stem: string, address: string, wallet?: string, shielded = false) => request<{ address: string; own: boolean; hrp?: string; shielded?: boolean }>('host', 'chain.validate', { ...w(stem, wallet), address, shielded }),
  send: (p: ChainSendParams) => request<{ stem: string; wallet: string; txid?: string }>('host', 'chain.send', { ...p }),
  withdraw: (stem: string, address: string, amount: string, fee?: string, wallet?: string) => request<{ txid?: string }>('host', 'chain.withdraw', { ...w(stem, wallet), address, amount, ...(fee ? { fee } : {}) }),
  deposit: (stem: string, amount: string, wallet?: string) => request<{ txid?: string; address: string }>('host', 'chain.deposit', { ...w(stem, wallet), amount }),
  staking: (stem: string, wallet?: string) => request<ChainStaking>('host', 'chain.staking', w(stem, wallet)),
  peg: (stem: string, wallet?: string) => request<ChainPeg>('host', 'chain.peg', w(stem, wallet)),
  assets: (stem: string, wallet?: string) => request<{ stem: string; wallet: string; assets: ChainAsset[] }>('host', 'chain.assets', w(stem, wallet)),
  names: (stem: string, wallet?: string) => request<{ stem: string; names: ChainName[] }>('host', 'chain.names', w(stem, wallet)),
  markets: (stem: string) => request<{ stem: string; markets: ChainMarket[] }>('host', 'chain.markets', { stem }),
};

export interface ChainStaking {
  stem: string;
  wallet: string;
  node?: {
    validator?: boolean;
    enabled?: boolean;
    producererror?: string;
    keys?: { pubkey: string; inset?: boolean; stake?: number; poolshare?: string; bond?: Record<string, unknown> }[];
    stake?: number;
    totalstake?: number;
    poolshare?: string;
    produced?: number;
    blocksmade?: number;
    nextslot?: number;
    nexttime?: number;
    feesplit?: { active?: boolean; bidderbps?: number; validatorsbps?: number };
  };
  chain?: {
    active?: boolean;
    epoch?: number;
    slot?: number;
    validators?: { pubkey: string; stake: number; poolshare?: string }[];
    pot?: { accrued?: string; paid?: string; reserve?: string };
    staking?: { bonded?: string; unbonding?: string; slashed?: string; minstake?: string };
    feesplit?: { active?: boolean; bidderbps?: number; validatorsbps?: number };
  };
  set?: { epoch?: number; totalstake?: number; validators?: { pubkey: string; stake: number; slots?: number; local?: boolean; banned?: boolean }[] };
  bonds?: { validator: string; owner?: string; bonded?: string; unbonding?: string; inset?: boolean; eligible?: boolean; banned?: boolean }[];
  errors?: Record<string, string>;
}

export interface ChainName {
  kind?: string;
  name?: string;
  namehash?: string;
  seq?: string;
  address?: string;
  txid?: string;
}

export interface ChainMarket {
  id?: string;
  text?: string;
  status?: string;
  liquidity?: string;
  feebps?: number;
  states?: { state?: number; answers?: string; price?: string }[];
}

/** A chain of a list: this app's own, or a sidechain turned on. */
export interface ChainEntry {
  id: string;
  name: string;
  unit: string;
  host: boolean;
  /** A sidechain: its node runs and answers. */
  running: boolean;
  caps: ChainCaps | null;
  entry?: SidechainEntry;
}

export const chainState = $state({
  caps: {} as Record<string, ChainCaps>,
  capsLoaded: false,
  summaries: {} as Record<string, ChainSummary>,
  /** When each summary was asked last (ms). */
  asked: {} as Record<string, number>,
});

export async function loadCapabilities(): Promise<void> {
  if (chainState.capsLoaded) return;
  try {
    const list = await chainHost.capabilities();
    for (const c of list) chainState.caps[c.stem] = c;
    chainState.capsLoaded = true;
  } catch {
    // Asked again next time.
  }
}

/** The unit of a chain's coin: its module's word if it said one, else the app's (a sidechain's coins are the app's). */
export function chainUnit(id: string): string {
  if (id === HOST) return productUnit();
  return modules.manifests[id]?.unit || productUnit();
}

/** Every chain of the app's pages: its own first, then each sidechain turned on (in the catalog's order). */
export function chainEntries(on: SidechainEntry[] = (hub.list?.chains ?? []).filter((c) => c.enabled)): ChainEntry[] {
  return [
    { id: HOST, name: productName(), unit: productUnit(), host: true, running: true, caps: null },
    ...on.map((c) => ({ id: c.stem, name: c.name, unit: chainUnit(c.stem), host: false, running: c.state === 'running' && c.ready, caps: chainState.caps[c.stem] ?? null, entry: c })),
  ];
}

type Capability = 'assets' | 'dex' | 'staking' | 'names' | 'markets' | 'shielded';

/** The sidechains turned on that can do something (their table, never what their node answered). */
export function chainsWith(cap: Capability, entries: ChainEntry[] = chainEntries()): ChainEntry[] {
  return entries.filter((e) => !e.host && !!e.caps?.[cap]);
}

// ---- What each page remembers: a filter of read views, the chain of actions ----

/** The pages that show many chains (a filter: All or one chain) and those that act on one. */
export const READ_VIEWS = ['home', 'overview', 'transactions', 'wallets', 'sidechains', 'staking', 'assets', 'names', 'markets'] as const;
export const ACTION_VIEWS = ['send', 'receive', 'trade'] as const;
export type ReadView = (typeof READ_VIEWS)[number];
export type ActionView = (typeof ACTION_VIEWS)[number];

export { cleanChainViews, WALLET_NAME } from './chainprefs';

/** The filter of a read view: what the user chose last, if that chain is still on; else every chain. */
export function viewFilter(page: ReadView, entries: ChainEntry[] = chainEntries()): string {
  const v = prefs.chainViews[page];
  if (v && (v === ALL || entries.some((e) => e.id === v))) return v;
  return ALL;
}

export function setViewFilter(page: ReadView, value: string): void {
  savePrefs({ chainViews: { ...prefs.chainViews, [page]: value } });
}

/**
 * The chain an action page acts on: the one asked for (a link: Send from a chain's card), else the one used last on
 * that page if it is still on, else this app's own.
 */
export function actionChain(page: ActionView, entries: ChainEntry[] = chainEntries(), asked?: string | null): string {
  if (asked && entries.some((e) => e.id === asked)) return asked;
  const v = prefs.chainViews[page];
  if (v && entries.some((e) => e.id === v)) return v;
  return entries[0]?.id ?? HOST;
}

export function setActionChain(page: ActionView, id: string): void {
  if (prefs.chainViews[page] === id) return;
  savePrefs({ chainViews: { ...prefs.chainViews, [page]: id } });
}

/** The chains a filter shows. */
export function filtered<T extends { id: string }>(entries: T[], filter: string): T[] {
  return filter === ALL ? entries : entries.filter((e) => e.id === filter);
}

/** Rows of many chains, as a filter shows them. */
export function rowsOf<T extends { chain: string }>(rows: T[], filter: string): T[] {
  return filter === ALL ? rows : rows.filter((r) => r.chain === filter);
}

/** The wallet the app's pages use on a sidechain: the one chosen on Manage wallets, else the chain's own choice. */
export function chainWallet(stem: string): string | undefined {
  if (stem === HOST) return wallets.current ?? undefined;
  return prefs.chainWallets[stem] || undefined;
}

export function setChainWallet(stem: string, wallet: string): void {
  savePrefs({ chainWallets: { ...prefs.chainWallets, [stem]: wallet } });
}

/** A chain asked to show on an action page next (Send on Layers from its card): taken once. */
export const chainLink = $state({ send: null as null | { chain: string; asset?: string }, receive: null as null | { chain: string }, peg: null as string | null });

// ---- Amounts: exact, as text (a sidechain's amounts never go through a float here) ----

/** An amount as typed, in units of 10^-decimals; null if it is not one (no sign, no exponent, at most `decimals`). */
export function parseUnits(text: string, decimals: number): bigint | null {
  const s = text.trim();
  if (!/^(\d+(\.\d*)?|\.\d+)$/.test(s) || s.endsWith('.')) return null;
  const [whole, frac = ''] = s.split('.');
  if (frac.length > decimals) return null;
  return BigInt(whole || '0') * 10n ** BigInt(decimals) + BigInt((frac + '0'.repeat(decimals)).slice(0, decimals) || '0');
}

export function formatUnits(units: bigint, decimals: number): string {
  const neg = units < 0n;
  const abs = neg ? -units : units;
  const scale = 10n ** BigInt(decimals);
  const frac = decimals > 0 ? `.${(abs % scale).toString().padStart(decimals, '0')}` : '';
  return `${neg ? '-' : ''}${abs / scale}${frac}`;
}

/** What is wrong with an amount to send, in the app's words (the host checks it again): null when fine. */
export function amountProblem(text: string, decimals: number, available?: string | null): string | null {
  if (!text.trim()) return 'Enter an amount';
  const units = parseUnits(text, decimals);
  if (units === null) return decimals === 0 ? 'A whole number' : 'A number with at most %1 decimals';
  if (units <= 0n) return 'More than zero';
  if (available != null) {
    const have = parseUnits(available.replace(/^-/, ''), decimals) ?? parseUnits(Number(available).toFixed(decimals), decimals);
    if (have !== null && units > have) return 'More than the wallet holds';
  }
  return null;
}

/** An address that is surely of no chain to pay on a sidechain (the host checks every address against every chain). */
export function addressHint(address: string): 'empty' | 'deposit' | null {
  const a = address.trim();
  if (!a) return 'empty';
  if (/^s\d+_/.test(a)) return 'deposit';
  return null;
}

// ---- Transactions of many chains ----

export interface ManyChainTx {
  chain: string;
  name: string;
  txid: string;
  time: number;
  category: string;
  /** In coins, as text, exactly as the chain's node gave it. */
  amount: string;
  address: string;
  label: string;
  confirmations: number;
  vout: number;
}

export function sidechainTxs(stem: string, name: string, rows: ChainTxRow[]): ManyChainTx[] {
  return rows
    .filter((r) => /^[0-9a-f]{64}$/.test(r.txid) && /^-?\d+(\.\d+)?$/.test(r.amount))
    .slice(0, 500)
    .map((r) => ({ chain: stem, name, txid: r.txid, time: r.time, category: r.category.slice(0, 16), amount: r.amount, address: r.address.slice(0, 120), label: r.label.slice(0, 120), confirmations: r.confirmations, vout: r.vout }));
}

/** Newest first, every chain together. */
export function mergeTxs(lists: ManyChainTx[][]): ManyChainTx[] {
  return lists.flat().sort((a, b) => b.time - a.time || a.chain.localeCompare(b.chain) || a.txid.localeCompare(b.txid));
}

// ---- Summaries, asked while shown ----

/** The summary of a sidechain, asked again if older than maxAgeMs. */
export async function refreshSummary(stem: string, maxAgeMs = 4000): Promise<ChainSummary | null> {
  if (stem === HOST) return null;
  const at = chainState.asked[stem] ?? 0;
  if (Date.now() - at < maxAgeMs && chainState.summaries[stem]) return chainState.summaries[stem];
  chainState.asked[stem] = Date.now();
  try {
    const s = await chainHost.summary(stem, chainWallet(stem));
    chainState.summaries[stem] = s;
    return s;
  } catch {
    return chainState.summaries[stem] ?? null;
  }
}

/** The total of a chain's coins in its wallet, as text: available, then waiting (pending and immature). */
export function balanceOf(s: ChainSummary | undefined): { available: string | null; waiting: string | null } {
  const b = s?.balances;
  if (!b) return { available: null, waiting: null };
  const p = parseUnits(b.pending || '0', 8) ?? 0n;
  const i = parseUnits(b.immature || '0', 8) ?? 0n;
  return { available: b.trusted || null, waiting: formatUnits(p + i, 8) };
}

/** The assets of many chains in one list: each named after its chain. */
export interface ManyChainAsset extends ChainAsset {
  chain: string;
  name: string;
}

export function mergeAssets(summaries: ChainSummary[]): ManyChainAsset[] {
  const out: ManyChainAsset[] = [];
  for (const s of summaries) for (const a of s.assets ?? []) out.push({ ...a, chain: s.stem, name: s.name });
  return out.sort((a, b) => a.label.localeCompare(b.label) || a.chain.localeCompare(b.chain));
}

/** The pages of a module the app shows inside its own, when its table names them. */
export function canEmbed(stem: string, page: string): boolean {
  return !!chainState.caps[stem]?.embed.includes(page);
}

/** The key of the chain in the dialog: the label in words, never a mark alone. */
export function chainLabel(id: string): string {
  if (id === HOST) return productName();
  return hub.list?.chains.find((c) => c.stem === id)?.name ?? id;
}

// ---- Deposits on their way (made from the app's pages): until the sidechain's wallet shows them ----

export interface PendingDeposit {
  stem: string;
  txid: string;
  /** The address of the sidechain the deposit credits (from its node). */
  address: string;
  amount: string;
  at: number;
  credited: boolean;
}

export const deposits = $state({ list: [] as PendingDeposit[] });

export function noteDeposit(d: Omit<PendingDeposit, 'credited' | 'at'>, now = Date.now()): void {
  if (!/^[0-9a-f]{64}$/.test(d.txid) || deposits.list.some((x) => x.txid === d.txid)) return;
  deposits.list = [...deposits.list, { ...d, at: now, credited: false }].slice(-50);
}

/** A sidechain's history seen: its deposits whose address got coins are credited. */
export function creditDeposits(stem: string, rows: Pick<ChainTxRow, 'address' | 'amount' | 'category'>[]): number {
  let n = 0;
  deposits.list = deposits.list.map((d) => {
    if (d.stem !== stem || d.credited || !rows.some((r) => r.address === d.address && !r.amount.startsWith('-'))) return d;
    n++;
    return { ...d, credited: true };
  });
  return n;
}

export function pendingDeposits(stem?: string): PendingDeposit[] {
  return deposits.list.filter((d) => !d.credited && (!stem || d.stem === stem));
}
