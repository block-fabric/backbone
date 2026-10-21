// The Trade page: the DEX of every chain that has one (doc/unified-app.md §22). Exact amounts (the least received, the
// second amount of liquidity, a share taken out), the estimates said as such, the page in the Wallet section only while
// a chain with a DEX is on, Swap with its chain tiles in step with the selector, the quote and its route, the swap sent
// with exactly what was shown, pools and a pool's page, liquidity in and out, the new-pool wizard (kinds offered only
// when the chain says they apply), positions over every chain, the place of limit orders, the market index searched and
// sorted, privacy mode, Simple mode, and only the chain shown read.
import { fireEvent, render, waitFor } from '@testing-library/svelte';
import { beforeAll, beforeEach, describe, expect, it } from 'vitest';
import { setTransport, type BridgeRequest } from '../lib/bridge';
import { nav, PAGES } from '../lib/nav.svelte';
import { hub } from '../lib/sidechains.svelte';
import { app, prefs, wallets } from '../lib/state.svelte';
import { modules } from '../lib/modules.svelte';
import { pageAvailable } from '../lib/commands.svelte';
import { NAV_SECTIONS } from '../lib/layout';
import { simpleNavPages } from '../lib/uimode.svelte';
import { chainState, loadCapabilities } from '../lib/chains.svelte';
import {
  aprEstimate,
  COIN_ID,
  coinPrices,
  dex,
  indexRows,
  minReceived,
  oriented,
  pairedAmount,
  parsePoolTab,
  poolTab,
  poolValue,
  searchRows,
  shareOf,
  slippageBps,
  sortRows,
  stale,
  type Pool,
} from '../lib/dex.svelte';
import Trade from '../pages/Trade.svelte';
import type { SidechainEntry } from '../lib/unified';

const NAMES: Record<string, string> = { layers: 'Layers', thunder: 'Thunder', bitassets: 'BitAssets' };
function entry(stem: string, over: Partial<SidechainEntry> = {}): SidechainEntry {
  return { stem, name: NAMES[stem] ?? stem, description: '', slot: 7, diskGB: 4, ramMB: 400, staking: false, enabled: true, state: 'running', pid: 1, restarts: 0, ready: true, dataDir: '', defaultDataDir: '', customDataDir: false, hasData: true, program: '', rpcPort: 0, port: 0, wallet: 'shared', ...over };
}
const base = { decimals: 8, assets: false, dex: false, staking: false, names: false, markets: false, shielded: false, send: true, receive: true, deposit: true, withdraw: true, embed: [] as string[] };
const CAPS = [
  { ...base, stem: 'layers', assets: true, dex: true, staking: true, embed: ['trade'] },
  { ...base, stem: 'thunder' },
  { ...base, stem: 'bitassets', assets: true, dex: true, embed: ['trade'] },
];
const GOLD = 'c'.repeat(64);
const SILV = '5'.repeat(64);
const P1 = '7'.repeat(64);
const P2 = '8'.repeat(64);

function pool(over: Partial<Pool>): Pool {
  return { pool: P1, a: { id: GOLD, label: 'GOLD' }, b: { id: COIN_ID, label: 'CHN' }, reserveA: '10000.00', reserveB: '20.00000000', price: '0.002', shares: '447213', volumeA: '500.00', volumeB: '1.00000000', swaps: 7, created: 12, abandoned: false, curve: 'constant-product', fee: 3000, amp: 0, weightA: 0, legacy: true, ...over };
}
const POOLS = [pool({}), pool({ pool: P2, a: { id: COIN_ID, label: 'CHN' }, b: { id: SILV, label: 'SILV' }, reserveA: '20.00000000', reserveB: '5000.0000', price: '250', volumeA: '0', volumeB: '0', swaps: 0, created: 30, curve: 'stable', fee: 500, amp: 100, legacy: false })];
const QUOTE = {
  stem: 'layers',
  in: GOLD,
  out: SILV,
  amountIn: '10.00',
  amountOut: '12.3456',
  decimalsIn: 2,
  decimalsOut: 4,
  via: 'route',
  routes: [
    {
      path: [GOLD, COIN_ID, SILV],
      labels: ['GOLD', 'CHN', 'SILV'],
      amountOut: '12.3456',
      priceImpact: '0.4210',
      fees: 3500,
      hops: [
        { pool: P1, from: GOLD, to: COIN_ID, amountOut: '0.01990000', curve: 'constant-product', fee: 3000, amp: 0, weightA: 0 },
        { pool: P2, from: COIN_ID, to: SILV, amountOut: '12.3456', curve: 'stable', fee: 500, amp: 100, weightA: 0 },
      ],
    },
    { path: [GOLD, SILV], labels: ['GOLD', 'SILV'], amountOut: '11.0000', priceImpact: '1.0000', fees: 3000, hops: [{ pool: '9'.repeat(64), from: GOLD, to: SILV, amountOut: '11.0000', curve: 'constant-product', fee: 3000 }] },
  ],
};

let calls: BridgeRequest[];
let curvesActive: boolean;

function summary(stem: string) {
  return {
    stem,
    name: NAMES[stem],
    running: true,
    capabilities: CAPS.find((c) => c.stem === stem),
    wallet: 'main',
    wallets: ['main'],
    height: 42,
    balances: { trusted: '20.00000000', pending: '0', immature: '0' },
    assets: stem === 'thunder' ? undefined : [{ id: GOLD, label: 'GOLD', decimals: 2, balance: '250.50', pending: '0', control: true, supply: '1000000' }, { id: SILV, label: 'SILV', decimals: 4, balance: '0.0000', pending: '0', control: false, supply: '5000' }],
    errors: {},
  };
}

const ofMethod = (m: string) => calls.filter((c) => c.method === m);
const P = (c: BridgeRequest) => c.params as Record<string, unknown>;

beforeAll(() => {
  const proto = HTMLDialogElement.prototype as unknown as Record<string, unknown>;
  proto.showModal ??= function (this: HTMLDialogElement) {
    this.setAttribute('open', '');
  };
});

beforeEach(async () => {
  app.phase = 'ready';
  nav.page = 'trade';
  nav.tab = '';
  prefs.chain = 'layers';
  prefs.chainWallets = {};
  prefs.mask = false;
  prefs.uiMode = 'advanced';
  prefs.dexSlippage = 50;
  chainState.caps = {};
  chainState.capsLoaded = false;
  chainState.summaries = {};
  chainState.asked = {};
  for (const k of ['info', 'pools', 'positions', 'charts', 'errors'] as const) dex[k] = {};
  for (const s of ['layers', 'bitassets']) stale(`info:${s}`, `pools:${s}`, `positions:${s}`, `chart:${s}:${P1}`);
  modules.manifests = {};
  hub.selected = 'chains';
  hub.list = { network: 'regtest', rpcServer: true, chainsDataDir: '/d', chains: [entry('layers'), entry('thunder'), entry('bitassets', { enabled: false, state: 'disabled', ready: false })] };
  wallets.list = [{ name: 'demo' }] as never;
  wallets.current = 'demo';
  wallets.balances = { demo: { trusted: 300000000n, untrustedPending: 0n, immature: 0n } };
  calls = [];
  curvesActive = false;
  setTransport(async (r) => {
    calls.push(r);
    const p = (r.params ?? {}) as Record<string, unknown>;
    switch (r.method) {
      case 'chain.capabilities':
        return structuredClone(CAPS);
      case 'chain.summary':
        return summary(String(p.stem));
      case 'chain.dex.info':
        return {
          stem: p.stem,
          features: { pair: true, liquidity: true, route: p.stem === 'layers', twap: p.stem === 'layers', curves: curvesActive, orders: false, stats: false },
          active: true,
          maxHops: p.stem === 'layers' ? 4 : 1,
          curvesActive,
          curves: curvesActive ? ['constant-product', 'stable', 'weighted'] : ['constant-product'],
          feeTiers: curvesActive ? [100, 500, 3000, 10000] : [3000],
          amp: { min: 1, max: 10000 },
          weights: curvesActive ? [2, 5, 10, 20, 25, 40, 60, 75, 80, 90, 95, 98] : [],
        };
      case 'chain.dex.pools':
        return { stem: p.stem, height: 40000, blockSeconds: '2.000', pools: structuredClone(POOLS) };
      case 'chain.dex.positions':
        return { stem: p.stem, wallet: 'main', positions: [{ pool: P1, a: { id: GOLD, label: 'GOLD' }, b: { id: COIN_ID, label: 'CHN' }, shares: '1000', percent: '2.5000', valueA: '250.00', valueB: '0.50000000', kind: 'shares' }] };
      case 'chain.dex.quote':
        return structuredClone(QUOTE);
      case 'chain.dex.chart':
        return { stem: p.stem, pool: p.pool, labelA: 'GOLD', spot: '0.002', from: 800, to: 2000, points: [{ t: 900, price: '0.0019' }, { t: 1400, price: '0.0020' }, { t: 1900, price: '0.0021' }] };
      case 'chain.dex.swap':
        return { txid: 'ab'.repeat(32), quote: '12.3456', minOut: p.minOut };
      case 'chain.dex.addLiquidity':
      case 'chain.dex.removeLiquidity':
      case 'chain.dex.createPool':
        return { txid: 'cd'.repeat(32) };
      default:
        return null;
    }
  });
  await loadCapabilities();
});

describe('exact amounts and estimates', () => {
  it('the least received: less the slippage, rounded down, never below one unit; slippage typed in percent', () => {
    expect(minReceived('12.3456', 4, 50)).toBe('12.2838');
    expect(minReceived('0.0001', 4, 50)).toBe('0.0001');
    expect(minReceived('100', 0, 100)).toBe('99');
    expect(minReceived('1.5', 2, 0)).toBe('1.50');
    expect(minReceived('abc', 2, 50)).toBeNull();
    expect(slippageBps('0.5')).toBe(50);
    expect(slippageBps('12.34')).toBe(1234);
    for (const bad of ['0', '0.001', '51', '-1', '1e2', '']) expect(slippageBps(bad), bad).toBeNull();
  });

  it('the second amount of liquidity at the pool\'s price, exactly, rounded up; a share of a position rounded down', () => {
    // 100 GOLD at 10000 GOLD for 20 CHN: 0.2 CHN.
    expect(pairedAmount('100', 2, '10000.00', '20.00000000', 8)).toBe('0.20000000');
    // 1 GOLD for 0.002 CHN; 0.01 GOLD: 0.00002 CHN.
    expect(pairedAmount('0.01', 2, '10000.00', '20.00000000', 8)).toBe('0.00002000');
    // Rounded up: 1 unit of 3 for 1 of 7 in reserves (7/3 units).
    expect(pairedAmount('1', 0, '3', '7', 0)).toBe('3');
    expect(pairedAmount('x', 2, '1', '1', 2)).toBeNull();
    expect(shareOf('250.00', '50')).toBe('125.00');
    expect(shareOf('0.50000000', '33.33')).toBe('0.16665000');
  });

  it('a pool with the coin second, its value in the coin and a yearly rate, said as estimates', () => {
    const o = oriented(POOLS[1]);
    expect(o.a.label).toBe('SILV');
    expect(o.b.id).toBe(COIN_ID);
    expect(o.reserveA).toBe('5000.0000');
    const prices = coinPrices(POOLS);
    expect(prices.get(GOLD)).toBeCloseTo(0.002);
    expect(poolValue(POOLS[0], prices)).toBeCloseTo(40);
    // 40000 - 12 blocks of 2 s: about 22 hours: too young for a rate.
    expect(aprEstimate(POOLS[0], prices, 40000, 2)).toBeNull();
    // A year of blocks: (500 * 0.002 + 1) * 0.003 = 0.006 of fees over 40: 0.015 %.
    expect(aprEstimate(POOLS[0], prices, 12 + 365 * 43200, 2)).toBeCloseTo(0.015, 3);
  });

  it('the market index: rows per chain, searched by ticker or hash, sorted', () => {
    const rows = indexRows('layers', { stem: 'layers', height: 40000, blockSeconds: '2', pools: POOLS });
    expect(rows.map((r) => r.pair)).toEqual(['GOLD / CHN', 'SILV / CHN']);
    expect(searchRows(rows, 'silv').map((r) => r.pair)).toEqual(['SILV / CHN']);
    expect(searchRows(rows, 'gold chn').length).toBe(1);
    expect(searchRows(rows, P2.slice(0, 10)).length).toBe(1);
    expect(sortRows(rows, 'pair', true).map((r) => r.pair)).toEqual(['SILV / CHN', 'GOLD / CHN']);
    expect(sortRows(rows, 'fee', false)[0].pool.fee).toBe(500);
  });

  it('a pool\'s place in the page is checked', () => {
    expect(parsePoolTab(poolTab('layers', P1))).toEqual({ stem: 'layers', pool: P1 });
    expect(parsePoolTab('pool:layers:xyz')).toBeNull();
    expect(parsePoolTab(`pool:LAYERS:${P1}`)).toBeNull();
  });
});

describe('where Trade is', () => {
  it('in the Wallet section after Receive, only while a chain with a DEX is on; Swap in Simple mode', () => {
    expect(NAV_SECTIONS[0].pages.slice(0, 4)).toEqual(['overview', 'send', 'receive', 'trade']);
    expect(PAGES.trade.simpleTitle).toBe('Swap');
    expect(pageAvailable('trade')).toBe(true);
    prefs.uiMode = 'simple';
    expect(simpleNavPages()).toContain('trade');
    hub.list = { ...hub.list!, chains: [entry('thunder')] };
    expect(pageAvailable('trade')).toBe(false);
    expect(simpleNavPages()).not.toContain('trade');
  });
});

describe('Swap', () => {
  it('tiles of the chains with a DEX, in step with the selector; a quote, its route, the least received, then the swap as shown', async () => {
    render(Trade);
    await waitFor(() => expect(document.querySelector('[data-swap="layers"]')).not.toBeNull());
    const tiles = [...document.querySelectorAll('[data-chain-tile]')].map((e) => e.getAttribute('data-chain-tile'));
    expect(tiles).toEqual(['layers']);
    await waitFor(() => expect(document.querySelector('[data-swap-amount]')).not.toBeNull());
    // GOLD to SILV.
    await fireEvent.click(document.querySelector('[data-asset-picker="pay"] [data-asset-picked]')!);
    await fireEvent.click(document.querySelector(`[data-asset-picker="pay"] [data-asset-option="${GOLD}"]`)!);
    await fireEvent.click(document.querySelector('[data-asset-picker="get"] [data-asset-picked]')!);
    await fireEvent.click(document.querySelector(`[data-asset-picker="get"] [data-asset-option="${SILV}"]`)!);
    await fireEvent.input(document.querySelector('[data-swap-amount]')!, { target: { value: '10' } });
    await waitFor(() => expect(ofMethod('chain.dex.quote').length).toBe(1), { timeout: 2000 });
    expect(P(ofMethod('chain.dex.quote')[0])).toMatchObject({ stem: 'layers', in: GOLD, out: SILV, amount: '10', maxHops: 4 });
    await waitFor(() => expect(document.querySelector('[data-swap-least]')?.textContent).toContain('12.2838'));
    expect(document.querySelector('[data-route-path]')?.textContent).toMatch(/GOLD.*CHN.*SILV/);
    expect(document.querySelector('[data-route-hop="1"] [data-hop-curve]')?.getAttribute('data-hop-curve')).toBe('stable');
    expect(document.querySelector('[data-route-hop="1"] [data-hop-fee]')?.textContent).toContain('0.05 %');
    expect(document.querySelector('[data-swap-impact]')?.textContent).toContain('0.42 %');
    // Another route chosen, then back to the best.
    await fireEvent.click(document.querySelector('[data-swap-route="1"]')!);
    await waitFor(() => expect(document.querySelector('[data-swap-least]')?.textContent).toContain('10.9450'));
    await fireEvent.click(document.querySelector('[data-swap-route="0"]')!);
    // A slippage of 1 %: the least is 12.2221.
    await fireEvent.click(document.querySelector('[data-swap-slippage="100"]')!);
    await waitFor(() => expect(document.querySelector('[data-swap-least]')?.textContent).toContain('12.2221'));
    expect(prefs.dexSlippage).toBe(100);
    await fireEvent.click(document.querySelector('[data-swap-go]')!);
    await waitFor(() => expect(ofMethod('chain.dex.swap').length).toBe(1));
    expect(P(ofMethod('chain.dex.swap')[0])).toMatchObject({ stem: 'layers', in: GOLD, out: SILV, amount: '10.00', path: [GOLD, COIN_ID, SILV], slippage: 100, minOut: '12.2221' });
    await waitFor(() => expect(document.querySelector('[data-swap-done]')).not.toBeNull());
  });

  it('more than held: said, quoted for its price, not sent; masked in privacy mode', async () => {
    prefs.mask = true;
    render(Trade);
    await waitFor(() => expect(document.querySelector('[data-swap-amount]')).not.toBeNull());
    expect(document.querySelector('[data-swap] [data-module-amount]')?.textContent).toContain('••••••');
    await fireEvent.input(document.querySelector('[data-swap-amount]')!, { target: { value: '25' } });
    await waitFor(() => expect(document.querySelector('[data-swap] .error')?.textContent).toContain('More than the wallet holds'));
    await waitFor(() => expect(ofMethod('chain.dex.quote').length).toBe(1), { timeout: 2000 });
    await waitFor(() => expect(document.querySelector('[data-swap-least]')?.textContent).toContain('••••••'));
    expect((document.querySelector('[data-swap-go]') as HTMLButtonElement).disabled).toBe(true);
  });

  it('All in the top bar: the chain is asked first; a chain without a DEX is said, with those that have one', async () => {
    prefs.chain = 'all';
    render(Trade);
    await waitFor(() => expect(document.querySelector('[data-chain-needed]')).not.toBeNull());
    expect(document.querySelector('[data-swap]')).toBeNull();
    await fireEvent.click(document.querySelector('[data-chain-tile="layers"]')!);
    expect(prefs.chain).toBe('layers');
    prefs.chain = 'thunder';
    await waitFor(() => expect(document.querySelector('[data-not-on-chain="thunder"]')?.textContent).toContain('Thunder has no DEX'));
    expect(document.querySelector('[data-show-chain="layers"]')).not.toBeNull();
  });

  it('only the chain shown is read: its pools, never another chain\'s', async () => {
    hub.list = { ...hub.list!, chains: [entry('layers'), entry('bitassets')] };
    render(Trade);
    await waitFor(() => expect(ofMethod('chain.dex.pools').length).toBeGreaterThan(0));
    await new Promise((r) => setTimeout(r, 50));
    expect(ofMethod('chain.dex.pools').every((c) => P(c).stem === 'layers')).toBe(true);
    expect(ofMethod('chain.dex.positions').length).toBe(0);
    expect(ofMethod('chain.dex.chart').length).toBe(0);
  });

  it('Simple mode: Swap alone, no tabs, no route panel, the route in one line', async () => {
    prefs.uiMode = 'simple';
    nav.tab = 'pools';
    render(Trade);
    await waitFor(() => expect(document.querySelector('[data-swap-amount]')).not.toBeNull());
    expect(document.querySelector('h1')?.textContent).toBe('Swap');
    expect(document.querySelector('[data-trade-tab]')).toBeNull();
    expect(document.querySelector('[data-swap-route-panel]')).toBeNull();
    expect(document.querySelector('[data-swap-slippage]')).toBeNull();
  });
});

describe('Pools, a pool, liquidity, a new pool', () => {
  it('the list: each pool\'s curve and fee tier, its liquidity about in the coin; a row opens its page', async () => {
    nav.tab = 'pools';
    render(Trade);
    await waitFor(() => expect(document.querySelectorAll('[data-pool-row]').length).toBe(2));
    const second = document.querySelector(`[data-pool-row="${P2}"]`)!;
    expect(second.querySelector('[data-pool-curve]')?.getAttribute('data-pool-curve')).toBe('stable');
    expect(second.textContent).toContain('Stable · A 100');
    expect(second.querySelector('[data-pool-fee]')?.textContent).toContain('0.05 %');
    expect(second.textContent).toContain('SILV / CHN');
    expect(document.querySelector(`[data-pool-row="${P1}"]`)?.textContent).toContain('40.00');
    await fireEvent.click(document.querySelector(`[data-pool-row="${P1}"]`)!);
    expect(nav.tab).toBe(poolTab('layers', P1));
  });

  it('a pool\'s page: its price over time, what it holds, the wallet\'s share; liquidity in at its price and out by a share', async () => {
    nav.tab = poolTab('layers', P1);
    render(Trade);
    await waitFor(() => expect(document.querySelector('[data-pool-page]')?.textContent).toContain('GOLD / CHN'));
    await waitFor(() => expect(document.querySelector('[data-price-chart="3"]')).not.toBeNull());
    expect(document.querySelector('[data-pool-price]')?.textContent).toContain('0.002');
    await waitFor(() => expect(document.querySelector('[data-pool-share]')?.textContent).toContain('2.5000'));
    await fireEvent.input(document.querySelector('[data-liq-amount-a]')!, { target: { value: '100' } });
    await waitFor(() => expect((document.querySelector('[data-liq-amount-b]') as HTMLInputElement).value).toBe('0.20000000'));
    await fireEvent.click(document.querySelector('[data-liq-add]')!);
    await waitFor(() => expect(ofMethod('chain.dex.addLiquidity').length).toBe(1));
    expect(P(ofMethod('chain.dex.addLiquidity')[0])).toMatchObject({ stem: 'layers', a: GOLD, b: COIN_ID, amountA: '100', amountB: '0.20000000', setsPrice: false });
    await fireEvent.click(document.querySelector('[data-liq-tab="remove"]')!);
    await fireEvent.click(document.querySelector('[data-liq-pct="50"]')!);
    await waitFor(() => expect(document.querySelector('[data-liquidity="remove"]')?.textContent).toContain('125.00'));
    await fireEvent.click(document.querySelector('[data-liq-remove]')!);
    await waitFor(() => expect(ofMethod('chain.dex.removeLiquidity').length).toBe(1));
    expect(P(ofMethod('chain.dex.removeLiquidity')[0])).toMatchObject({ stem: 'layers', pool: P1, percent: '50' });
  });

  it('a new pool: only constant product at 0.30 % until the chain says its pool kinds apply', async () => {
    nav.tab = 'create';
    render(Trade);
    await waitFor(() => expect(document.querySelector('[data-create-pool="pair"]')).not.toBeNull());
    // GOLD and SILV.
    await fireEvent.click(document.querySelector('[data-asset-picker="b"] [data-asset-picked]')!);
    await fireEvent.click(document.querySelector(`[data-asset-picker="b"] [data-asset-option="${SILV}"]`)!);
    await fireEvent.click(document.querySelector('[data-pool-next]')!);
    expect((document.querySelector('[data-curve="stable"]') as HTMLButtonElement).disabled).toBe(true);
    expect((document.querySelector('[data-fee-tier="500"]') as HTMLButtonElement).disabled).toBe(true);
    expect(document.querySelector('[data-curves-off]')).not.toBeNull();
    await fireEvent.click(document.querySelector('[data-pool-next]')!);
    await fireEvent.input(document.querySelector('[data-pool-amount="a"]')!, { target: { value: '100' } });
    await fireEvent.input(document.querySelector('[data-pool-amount="b"]')!, { target: { value: '0' } });
    expect((document.querySelector('[data-pool-next]') as HTMLButtonElement).disabled).toBe(true);
    await fireEvent.input(document.querySelector('[data-pool-amount="b"]')!, { target: { value: '200' } });
    expect(document.querySelector('[data-pool-amount="b"]')!.parentElement?.textContent).toContain('More than the wallet holds');
  });

  it('with the pool kinds: a stable pool with its amplification and fee tier', async () => {
    curvesActive = true;
    nav.tab = 'create';
    render(Trade);
    await waitFor(() => expect(document.querySelector('[data-pool-next]')).not.toBeNull());
    await waitFor(() => expect(dex.info.layers?.curvesActive).toBe(true));
    await fireEvent.click(document.querySelector('[data-pool-next]')!);
    await fireEvent.click(document.querySelector('[data-curve="stable"]')!);
    await fireEvent.click(document.querySelector('[data-fee-tier="500"]')!);
    expect(document.querySelector('[data-pool-amp]')).not.toBeNull();
    await fireEvent.click(document.querySelector('[data-pool-next]')!);
    await fireEvent.input(document.querySelector('[data-pool-amount="a"]')!, { target: { value: '100' } });
    await fireEvent.input(document.querySelector('[data-pool-amount="b"]')!, { target: { value: '1' } });
    await fireEvent.click(document.querySelector('[data-pool-next]')!);
    expect(document.querySelector('[data-pool-review]')?.textContent).toContain('Stable · A 100');
    await fireEvent.click(document.querySelector('[data-pool-go]')!);
    await waitFor(() => expect(ofMethod('chain.dex.createPool').length).toBe(1));
    expect(P(ofMethod('chain.dex.createPool')[0])).toMatchObject({ stem: 'layers', a: GOLD, b: COIN_ID, amountA: '100', amountB: '1', curve: 'stable', fee: 500, amp: 100 });
  });
});

describe('Positions, Orders, Markets', () => {
  it('the wallet\'s liquidity on every chain with a DEX (All): a Chain column, full range, fees in what it is worth', async () => {
    prefs.chain = 'all';
    hub.list = { ...hub.list!, chains: [entry('layers'), entry('bitassets')] };
    nav.tab = 'positions';
    render(Trade);
    await waitFor(() => expect(document.querySelectorAll('[data-position]').length).toBe(2));
    expect(new Set(ofMethod('chain.dex.positions').map((c) => P(c).stem))).toEqual(new Set(['layers', 'bitassets']));
    expect(document.querySelector('[data-positions] [data-chain-badge="bitassets"]')).not.toBeNull();
    expect(document.querySelector('[data-position-range]')?.textContent).toContain('Full range');
  });

  it('limit orders: their place laid out, nothing that can be sent', async () => {
    nav.tab = 'orders';
    render(Trade);
    await waitFor(() => expect(document.querySelector('[data-orders]')).not.toBeNull());
    expect(document.querySelector('[data-orders]')?.textContent).toContain('phase B');
    expect([...document.querySelectorAll('[data-orders] .btn.primary')].every((b) => (b as HTMLButtonElement).disabled)).toBe(true);
  });

  it('the market index: every pair, searched, sorted; 24-hour figures said to come later', async () => {
    nav.tab = 'markets';
    render(Trade);
    await waitFor(() => expect(document.querySelectorAll('[data-market-row]').length).toBe(2));
    await fireEvent.input(document.querySelector('[data-market-search]')!, { target: { value: 'silv' } });
    await waitFor(() => expect(document.querySelectorAll('[data-market-row]').length).toBe(1));
    await fireEvent.input(document.querySelector('[data-market-search]')!, { target: { value: '' } });
    await fireEvent.click(document.querySelector('[data-market-sort="pair"]')!);
    await waitFor(() => expect(document.querySelector('[data-market-row]')?.textContent).toContain('GOLD'));
    await fireEvent.click(document.querySelector('[data-market-sort="pair"]')!);
    await waitFor(() => expect(document.querySelector('[data-market-row]')?.textContent).toContain('SILV'));
    expect(document.querySelector('[data-market-index]')?.textContent).toContain('24-hour change and volume come with');
  });
});
