// The DEX of the chains this app runs, on the app's own Trade page (doc/unified-app.md §22): Swap, Pools, Positions,
// Orders (phase B) and the market index, over every chain that has a DEX (Layers; BitAssets' pools; Flush later),
// following the top bar's selector. What a chain's DEX does is its table (chain.capabilities `dexFeatures`) and what its
// node says applies now (chain.dex.info: whether routes clear, whether pool kinds apply), never guessed from a pool.
//
// Reads go through the host's chain.dex.* (webui/chainadapter.h): the chain's own pages' policy, bounded, amounts as the
// node's text. Every spend (a swap, liquidity in or out, a new pool) is built by the host from checked fields and asked
// in the trusted dialog by the chain's own describers, with what the app checked (the route quoted again) shown first.
//
// Light (doc/unified-app.md §20.6): only the chain shown and the tab shown are read, while the window is seen; each read
// at its own pace (a quote 15 s, pools 20 s, positions 30 s, a chart 60 s); an answer that did not change is left alone.
import { untrack } from 'svelte';
import { request } from './bridge';
import { parseUnits, formatUnits } from './chains.svelte';
import { t } from './i18n.svelte';

/** The chain's own coin, as an asset: all zeros. */
export const COIN_ID = '0'.repeat(64);

export interface DexFeatures {
  pair: boolean;
  liquidity: boolean;
  route: boolean;
  twap: boolean;
  curves: boolean;
  /** Spends name pools by their ids (with the pool kinds). */
  byId?: boolean;
  orders: boolean;
  stats: boolean;
}

export interface DexInfo {
  stem: string;
  features: DexFeatures;
  active: boolean;
  maxHops: number;
  curvesActive: boolean;
  curves: Curve[];
  feeTiers: number[];
  amp: { min: number; max: number };
  weights: number[];
}

export type Curve = 'constant-product' | 'stable' | 'weighted';

export interface DexAsset {
  id: string;
  label: string;
}

export interface Pool {
  pool: string;
  a: DexAsset;
  b: DexAsset;
  reserveA: string;
  reserveB: string;
  price: string | null;
  shares: string;
  volumeA: string;
  volumeB: string;
  swaps: number;
  created: number;
  abandoned: boolean;
  curve: Curve;
  /** Millionths of what goes in. */
  fee: number;
  amp: number;
  weightA: number;
  legacy: boolean;
}

export interface PoolList {
  stem: string;
  height: number;
  blockSeconds: string;
  pools: Pool[];
}

export interface Hop {
  pool: string;
  from: string;
  to: string;
  amountOut: string;
  curve?: Curve;
  fee?: number;
  amp?: number;
  weightA?: number;
  kind?: string;
  labelFrom?: string;
  labelTo?: string;
}

export interface Route {
  path: string[];
  labels: string[];
  amountOut: string;
  hops: Hop[];
  priceImpact: string | null;
  fees: number;
  /** Each hop's pool, by its id (what the swap names). */
  pools: string[];
  /** Every pool of the route is its pair's of the default kind. */
  defaultPools: boolean;
}

export interface Quote {
  stem: string;
  in: string;
  out: string;
  amountIn: string;
  amountOut: string;
  decimalsIn: number;
  decimalsOut: number;
  via: 'route' | 'pair';
  routes: Route[];
}

export interface Position {
  pool: string;
  a: DexAsset;
  b: DexAsset;
  shares: string;
  percent: string;
  valueA: string;
  valueB: string;
  /** "shares": constant-product shares (always in range, fees in what they are worth); "range" later. */
  kind: 'shares' | 'range';
}

export interface Chart {
  stem: string;
  pool: string;
  /** The pool's first asset as the chain keeps it (its label): the prices are of it, in the other. */
  labelA?: string;
  error?: string;
  spot?: string;
  from?: number;
  to?: number;
  points: { t: number; price: string }[];
  reason?: 'none' | 'young';
}

const s = (stem: string, wallet?: string) => ({ stem, ...(wallet ? { wallet } : {}) });

/** The host's chain.dex.* methods. */
export const dexHost = {
  info: (stem: string) => request<DexInfo>('host', 'chain.dex.info', { stem }),
  pools: (stem: string, asset?: string) => request<PoolList>('host', 'chain.dex.pools', { stem, ...(asset ? { asset } : {}) }),
  quote: (stem: string, inId: string, outId: string, amount: string, maxHops?: number) =>
    request<Quote>('host', 'chain.dex.quote', { stem, in: inId, out: outId, amount, ...(maxHops ? { maxHops } : {}) }),
  chart: (stem: string, pool: string) => request<Chart>('host', 'chain.dex.chart', { stem, pool }),
  positions: (stem: string, wallet?: string) => request<{ stem: string; wallet: string; positions: Position[] }>('host', 'chain.dex.positions', s(stem, wallet)),
  swap: (p: { stem: string; wallet?: string; in: string; out: string; amount: string; path: string[]; pools: string[]; slippage: number; minOut: string }) =>
    request<{ txid?: string; quote: string; minOut: string }>('host', 'chain.dex.swap', { ...p }),
  addLiquidity: (p: { stem: string; wallet?: string; pool: string; a: string; b: string; amountA: string; amountB?: string; slippage?: number; setsPrice?: boolean }) =>
    request<{ txid?: string }>('host', 'chain.dex.addLiquidity', { ...p }),
  removeLiquidity: (p: { stem: string; wallet?: string; pool: string; percent: string; slippage?: number }) => request<{ txid?: string }>('host', 'chain.dex.removeLiquidity', { ...p }),
  createPool: (p: { stem: string; wallet?: string; a: string; b: string; amountA: string; amountB: string; curve: Curve; fee: number; amp?: number; weightA?: number; slippage?: number }) =>
    request<{ txid?: string }>('host', 'chain.dex.createPool', { ...p }),
};

// ---- Exact parts (amounts never through a float) ----

/** The least to receive: `amount` less `bps` basis points, rounded down, never below one unit (as the host does). */
export function minReceived(amount: string, decimals: number, bps: number): string | null {
  const units = parseUnits(amount, decimals);
  if (units === null || units <= 0n) return null;
  let less = (units * BigInt(10000 - Math.min(Math.max(bps, 0), 10000))) / 10000n;
  if (less < 1n) less = 1n;
  return formatUnits(less, decimals);
}

/** A slippage as typed in percent ("0.5") in basis points, 1 to 5000; null when not one. */
export function slippageBps(text: string): number | null {
  const units = parseUnits(text, 2);
  if (units === null || units < 1n || units > 5000n) return null;
  return Number(units);
}

/** The decimals of an amount as a node wrote it ("250.50": 2). */
export function decimalsOf(text: string): number {
  const i = text.indexOf('.');
  return i < 0 ? 0 : text.length - i - 1;
}

/**
 * What goes in of the second asset for `amountA` of the first, at a pool's price (its reserves), rounded up: the amount
 * the wallet offers (the chain takes only what its price takes). Exact, from the reserves' text.
 */
export function pairedAmount(amountA: string, decA: number, reserveA: string, reserveB: string, decB: number): string | null {
  const a = parseUnits(amountA, decA);
  const ra = parseUnits(reserveA, decimalsOf(reserveA));
  const rb = parseUnits(reserveB, decimalsOf(reserveB));
  if (a === null || ra === null || rb === null || a <= 0n || ra <= 0n || rb <= 0n) return null;
  // b = a * rb / ra, each in its own units: a / 10^decA * (rb / 10^dRb) / (ra / 10^dRa) * 10^decB.
  const dRa = BigInt(decimalsOf(reserveA));
  const dRb = BigInt(decimalsOf(reserveB));
  const num = a * rb * 10n ** (BigInt(decB) + dRa);
  const den = ra * 10n ** (BigInt(decA) + dRb);
  const b = (num + den - 1n) / den;
  return formatUnits(b, decB);
}

/** A share of what a position is worth, `percent` (two decimals at most) of `value`, rounded down. */
export function shareOf(value: string, percent: string): string | null {
  const v = parseUnits(value, decimalsOf(value));
  const p = parseUnits(percent, 2);
  if (v === null || p === null) return null;
  return formatUnits((v * p) / 10000n, decimalsOf(value));
}

// ---- Words ----

/** A fee of millionths as a percentage: "0.30 %". */
export function feeText(millionths: number): string {
  return `${(millionths / 10000).toFixed(2)} %`;
}

/** A pool's kind in a few words, in the language shown ("Constant product", "Stable · A 100", "Weighted 80/20"). */
export function curveText(p: Pick<Pool, 'curve' | 'amp' | 'weightA'>): string {
  if (p.curve === 'stable') return t('Stable · A %1', String(p.amp));
  if (p.curve === 'weighted') return t('Weighted %1/%2', String(p.weightA), String(100 - p.weightA));
  return t('Constant product');
}

/** The pair of a pool as read: the coin of the chain last ("GOLD / CHN"). */
export function pairText(a: DexAsset, b: DexAsset): string {
  return a.id === COIN_ID && b.id !== COIN_ID ? `${b.label} / ${a.label}` : `${a.label} / ${b.label}`;
}

/** A pool oriented with the coin of the chain second (its price then in the coin). */
export function oriented(p: Pool): Pool {
  if (p.a.id !== COIN_ID || p.b.id === COIN_ID) return p;
  const flipped = parseFloat(p.reserveA) > 0 && parseFloat(p.reserveB) > 0 ? trimNumber(parseFloat(p.reserveA) / parseFloat(p.reserveB)) : null;
  return { ...p, a: p.b, b: p.a, reserveA: p.reserveB, reserveB: p.reserveA, volumeA: p.volumeB, volumeB: p.volumeA, price: flipped, weightA: p.curve === 'weighted' ? 100 - p.weightA : p.weightA };
}

/** A price as a plain decimal of about 8 significant digits (display only). */
export function trimNumber(n: number): string {
  if (!Number.isFinite(n) || n <= 0) return '0';
  const digits = n >= 1 ? 8 : Math.min(18, 8 - Math.floor(Math.log10(n)));
  return n.toFixed(digits).replace(/\.?0+$/, '');
}

// ---- Estimates of a pool (display only, marked "about"): what it holds in the chain's coin, and a yearly rate ----

/** The price of one whole unit of each asset in the chain's coin, from the pools that pair it with the coin. */
export function coinPrices(pools: Pool[]): Map<string, number> {
  const prices = new Map<string, number>([[COIN_ID, 1]]);
  for (const p of pools) {
    const ra = parseFloat(p.reserveA);
    const rb = parseFloat(p.reserveB);
    if (!(ra > 0 && rb > 0) || p.abandoned) continue;
    // The deepest pool with the coin sets an asset's price.
    if (p.b.id === COIN_ID && !(prices.has(p.a.id) && prices.get(p.a.id)! * ra > rb)) prices.set(p.a.id, rb / ra);
    if (p.a.id === COIN_ID && !(prices.has(p.b.id) && prices.get(p.b.id)! * rb > ra)) prices.set(p.b.id, ra / rb);
  }
  return prices;
}

/** What a pool holds, in the chain's coin; null when an asset of it has no price in the coin. */
export function poolValue(p: Pool, prices: Map<string, number>): number | null {
  const pa = prices.get(p.a.id);
  const pb = prices.get(p.b.id);
  if (pa === undefined || pb === undefined) return null;
  return parseFloat(p.reserveA) * pa + parseFloat(p.reserveB) * pb;
}

/**
 * A yearly rate of the fees for the pool's providers, from every trade since it was made (its volumes times its fee,
 * in the coin, over what it holds), scaled to a year by the chain's own pace of blocks; null when it cannot be said
 * (no price in the coin, a pool younger than a day, an unknown pace).
 */
export function aprEstimate(p: Pool, prices: Map<string, number>, height: number, blockSeconds: number): number | null {
  const tvl = poolValue(p, prices);
  if (!tvl || tvl <= 0 || !(blockSeconds > 0)) return null;
  const age = (height - p.created) * blockSeconds;
  if (age < 86400) return null;
  const pa = prices.get(p.a.id)!;
  const pb = prices.get(p.b.id)!;
  const fees = (parseFloat(p.volumeA) * pa + parseFloat(p.volumeB) * pb) * (p.fee / 1_000_000);
  return (fees / tvl) * ((365 * 86400) / age) * 100;
}

// ---- The market index: search and sort ----

export type SortKey = 'pair' | 'price' | 'liquidity' | 'volume' | 'swaps' | 'apr' | 'fee';

export interface IndexRow {
  chain: string;
  pool: Pool;
  pair: string;
  tvl: number | null;
  apr: number | null;
  /** What it traded since it was made, both sides valued in the chain's coin (an estimate). */
  volume: number | null;
}

export function indexRows(chain: string, list: PoolList | undefined): IndexRow[] {
  if (!list) return [];
  const prices = coinPrices(list.pools);
  const bs = parseFloat(list.blockSeconds);
  return list.pools.map((raw) => {
    const pool = oriented(raw);
    const pa = prices.get(pool.a.id);
    const pb = prices.get(pool.b.id);
    const volume = pa === undefined || pb === undefined ? null : parseFloat(pool.volumeA) * pa + parseFloat(pool.volumeB) * pb;
    return { chain, pool, pair: pairText(pool.a, pool.b), tvl: poolValue(pool, prices), apr: aprEstimate(pool, prices, list.height, bs), volume };
  });
}

export function searchRows(rows: IndexRow[], q: string): IndexRow[] {
  const words = q.trim().toLowerCase().split(/[\s/]+/).filter(Boolean);
  if (!words.length) return rows;
  return rows.filter((r) => {
    const hay = `${r.pool.a.label} ${r.pool.b.label} ${r.pool.pool} ${r.pool.a.id} ${r.pool.b.id}`.toLowerCase();
    return words.every((w) => hay.includes(w));
  });
}

export function sortRows(rows: IndexRow[], key: SortKey, desc: boolean): IndexRow[] {
  const val = (r: IndexRow): number | string => {
    switch (key) {
      case 'pair':
        return r.pair.toLowerCase();
      case 'price':
        return parseFloat(r.pool.price ?? '0');
      case 'liquidity':
        return r.tvl ?? -1;
      case 'volume':
        return r.volume ?? -1;
      case 'swaps':
        return r.pool.swaps;
      case 'apr':
        return r.apr ?? -1;
      case 'fee':
        return r.pool.fee;
    }
  };
  const out = [...rows].sort((x, y) => {
    const a = val(x);
    const b = val(y);
    const c = typeof a === 'string' ? a.localeCompare(b as string) : a - (b as number);
    return desc ? -c : c;
  });
  return out;
}

// ---- What the page shows of each chain, read while shown ----

export const dex = $state({
  info: {} as Record<string, DexInfo>,
  pools: {} as Record<string, PoolList>,
  positions: {} as Record<string, Position[]>,
  charts: {} as Record<string, Chart>,
  errors: {} as Record<string, string>,
});

const asked: Record<string, number> = {};

/** Put an answer in place only when it changed (nothing shown is drawn again for the same answer). */
function keep<K extends 'info' | 'pools' | 'positions' | 'charts'>(what: K, key: string, value: (typeof dex)[K][string]): void {
  const now = untrack(() => dex[what][key]);
  if (JSON.stringify(now) !== JSON.stringify(value)) (dex[what] as Record<string, unknown>)[key] = value;
}

async function once<T>(key: string, maxAgeMs: number, read: () => Promise<T>, put: (v: T) => void): Promise<void> {
  if (Date.now() - (asked[key] ?? 0) < maxAgeMs) return;
  asked[key] = Date.now();
  try {
    put(await read());
    if (untrack(() => dex.errors[key])) delete dex.errors[key];
  } catch (e) {
    dex.errors[key] = e && typeof e === 'object' && 'message' in e ? String((e as { message: unknown }).message) : String(e);
  }
}

export function loadInfo(stem: string, maxAgeMs = 60_000): Promise<void> {
  return once(`info:${stem}`, maxAgeMs, () => dexHost.info(stem), (v) => keep('info', stem, v));
}
export function loadPools(stem: string, maxAgeMs = 15_000): Promise<void> {
  return once(`pools:${stem}`, maxAgeMs, () => dexHost.pools(stem), (v) => keep('pools', stem, v));
}
export function loadPositions(stem: string, wallet?: string, maxAgeMs = 25_000): Promise<void> {
  return once(`positions:${stem}`, maxAgeMs, () => dexHost.positions(stem, wallet), (v) => keep('positions', stem, v.positions));
}
export function loadChart(stem: string, pool: string, maxAgeMs = 55_000): Promise<void> {
  return once(`chart:${stem}:${pool}`, maxAgeMs, () => dexHost.chart(stem, pool), (v) => keep('charts', `${stem}:${pool}`, v));
}
/** Read again at once next time (after a spend of the user). */
export function stale(...keys: string[]): void {
  for (const k of keys) delete asked[k];
}

/**
 * Call `read` now and every `periodMs` while the window is seen (document.hidden: nothing); a cleanup for an $effect.
 */
export function poll(read: () => void, periodMs: number): () => void {
  read();
  const timer = setInterval(() => {
    if (!document.hidden) read();
  }, periodMs);
  const seen = () => {
    if (!document.hidden) read();
  };
  document.addEventListener('visibilitychange', seen);
  return () => {
    clearInterval(timer);
    document.removeEventListener('visibilitychange', seen);
  };
}

/** The assets a chain's pools trade, each once (the coin first), for the pickers. */
export function tradedAssets(list: PoolList | undefined): DexAsset[] {
  const seen = new Map<string, DexAsset>();
  for (const p of list?.pools ?? []) {
    for (const a of [p.a, p.b]) if (!seen.has(a.id)) seen.set(a.id, a);
  }
  return [...seen.values()].sort((x, y) => (x.id === COIN_ID ? -1 : y.id === COIN_ID ? 1 : x.label.localeCompare(y.label)));
}

/** Where a pool's page is in the Trade page's place (nav.tab): "pool:<stem>:<id>". */
export function poolTab(stem: string, pool: string): string {
  return `pool:${stem}:${pool}`;
}
export function parsePoolTab(tab: string): { stem: string; pool: string } | null {
  const m = /^pool:([a-z0-9]{1,32}):([0-9a-f]{64})$/.exec(tab);
  return m ? { stem: m[1], pool: m[2] } : null;
}
