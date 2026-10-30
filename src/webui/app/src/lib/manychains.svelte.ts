// What Home and Overview show of every chain at once (doc/unified-app.md §19): each sidechain's latest transactions,
// its latest blocks and its mempool, its balance over time (made from its history: never added to another chain's),
// and Hivemind's open positions. Read through the host's chain.* methods while a page shows them, each chain asked
// again only once its answer is a few seconds old.
import { request } from './bridge';
import { chainHost, chainWallet, formatUnits, parseUnits, type ChainSummary, type ChainTxRow } from './chains.svelte';

export interface ChainActivityInfo {
  stem: string;
  height: number;
  blocks: { height: number; hash: string; time: number; txs: number }[];
  mempool: { size?: number; rows?: { txid: string; time: number; fee: string; vsize: number }[]; error?: string };
}

export interface ChainPosition {
  market: string;
  account: string;
  state: number;
  shares: string;
  text?: string;
  status?: string;
  answers?: string;
  price?: string;
  /** The market's decisions in order (the state's bits), when it is about several or no longer trades (doc/unified-app.md §24). */
  decisions?: { text?: string; scaled?: boolean; status?: string; outcome?: string }[];
}

export const many = $state({
  txs: {} as Record<string, ChainTxRow[]>,
  activity: {} as Record<string, ChainActivityInfo>,
  positions: {} as Record<string, ChainPosition[]>,
  errors: {} as Record<string, string>,
});
const asked: Record<string, number> = {};

async function once<T>(key: string, maxAgeMs: number, ask: () => Promise<T>, keep: (v: T) => void): Promise<void> {
  const now = Date.now();
  if (now - (asked[key] ?? 0) < maxAgeMs) return;
  asked[key] = now;
  try {
    keep(await ask());
    delete many.errors[key];
  } catch (e) {
    many.errors[key] = e instanceof Error ? e.message : String(e);
  }
}

/**
 * Whether what was read again is what is shown: then it is not put in its place, so that nothing shown changes (a
 * page left alone polls; under software GL each change is a frame drawn on every core, doc/unified-app.md §20.8).
 */
function same(a: unknown, b: unknown): boolean {
  return a !== undefined && JSON.stringify(a) === JSON.stringify(b);
}

export function refreshTxs(stem: string, maxAgeMs = 5000): Promise<void> {
  return once(`txs:${stem}`, maxAgeMs, () => chainHost.transactions(stem, chainWallet(stem), 200), (r) => {
    if (!same(many.txs[stem], r.rows)) many.txs[stem] = r.rows;
  });
}

export function refreshActivity(stem: string, maxAgeMs = 5000): Promise<void> {
  return once(`activity:${stem}`, maxAgeMs, () => request<ChainActivityInfo>('host', 'chain.activity', { stem }), (r) => {
    if (!same(many.activity[stem], r)) many.activity[stem] = r;
  });
}

export function refreshPositions(stem: string, maxAgeMs = 8000): Promise<void> {
  return once(
    `positions:${stem}`,
    maxAgeMs,
    () => request<{ positions: ChainPosition[] }>('host', 'chain.positions', { stem, ...(chainWallet(stem) ? { wallet: chainWallet(stem) } : {}) }),
    (r) => {
      const list = (r.positions ?? []).filter((p) => /^[0-9a-f]{64}$/.test(p.market));
      if (!same(many.positions[stem], list)) many.positions[stem] = list;
    },
  );
}

/** For the tests: forget what was asked when. */
export function forgetAsked(): void {
  for (const k of Object.keys(asked)) delete asked[k];
}

/** Ask while shown: now, then every few seconds while the window is seen. Returns the stop (an $effect's cleanup). */
export function poll(load: () => void, everyMs = 6000): () => void {
  load();
  const timer = setInterval(() => {
    if (typeof document === 'undefined' || !document.hidden) load();
  }, everyMs);
  return () => clearInterval(timer);
}

// ---- A sidechain's balance over time, from its history ----

/** A line of the chart of balances (components/ChainBalanceChart.svelte): one chain, in its own coin. */
export interface ChartSeries {
  id: string;
  name: string;
  unit: string;
  color: string;
  /** Ascending in time; balances in units of 10^-8 of the chain's coin. */
  points: { time: number; balance: bigint }[];
}

const signed = (text: string | undefined): bigint => {
  if (!text) return 0n;
  const neg = text.startsWith('-');
  const u = parseUnits(neg ? text.slice(1) : text, 8);
  return u === null ? 0n : neg ? -u : u;
};

/**
 * The balance of a sidechain's wallet over time, in units of 10^-8 of its own coin: from what it holds now (available
 * and on its way), going back through its latest transactions (each one's amount and fee). Its history may be cut (the
 * latest 200): the line then starts at what it held before the oldest of them, never at zero.
 */
export function chainBalanceHistory(rows: readonly ChainTxRow[], summary: ChainSummary | undefined, now = Math.floor(Date.now() / 1000)): { time: number; balance: bigint }[] {
  const b = summary?.balances;
  if (!b) return [];
  let balance = signed(b.trusted) + signed(b.pending) + signed(b.immature);
  const counted = rows.filter((r) => !r.abandoned && r.category !== 'orphan' && r.time > 0).sort((x, y) => y.time - x.time);
  const out: { time: number; balance: bigint }[] = [{ time: now, balance }];
  for (const r of counted) {
    out.push({ time: r.time, balance });
    balance -= signed(r.amount) + (r.category === 'send' ? signed(r.fee) : 0n);
  }
  if (counted.length) out.push({ time: counted[counted.length - 1].time - 1, balance });
  out.reverse();
  // One point per time: the last of each.
  const merged: { time: number; balance: bigint }[] = [];
  for (const p of out) {
    const last = merged[merged.length - 1];
    if (last && last.time === p.time) last.balance = p.balance;
    else merged.push({ ...p });
  }
  return merged;
}

/** What a position is worth now at its outcome's price: shares × price, exactly, as text of 8 decimals ('' unknown). */
export function positionValue(p: Pick<ChainPosition, 'shares' | 'price'>): string {
  if (!p.price) return '';
  const s = parseUnits(p.shares, 8);
  const pr = parseUnits(p.price, 8);
  if (s === null || pr === null) return '';
  return formatUnits((s * pr) / 10n ** 8n, 8);
}
