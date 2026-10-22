// What a prediction market came to on the app's own Markets page (doc/unified-app.md §24): the stage of each market and
// its outcomes as bars from what the chain's node says through the adapter (chain.markets: the markets, the decisions
// they are about, the clock of periods). A market that paid out has no price from its node, only its shares: its bars
// are what a share of each outcome paid, the one that won checked and marked Won, the faint mark of each last price
// before trading closed, when it paid; a market voted on shows its last prices; nothing reads NaN. Home's open positions
// say a market voted on in words, its price the final one.
import { render, waitFor, within } from '@testing-library/svelte';
import { beforeEach, describe, expect, it } from 'vitest';
import { setTransport, type BridgeRequest } from '../lib/bridge';
import { nav } from '../lib/nav.svelte';
import { hub } from '../lib/sidechains.svelte';
import { app, prefs, wallets } from '../lib/state.svelte';
import { chainState, loadCapabilities, type ChainDecision, type ChainMarket } from '../lib/chains.svelte';
import { forgetAsked, many } from '../lib/manychains.svelte';
import { lastPrices, lmsrPrices, marketView, optionTexts, statePayout } from '../lib/marketresults';
import Markets from '../pages/Markets.svelte';
import MarketPositions from '../components/MarketPositions.svelte';
import type { SidechainEntry } from '../lib/unified';

const id = (c: string) => c.repeat(64);
const base = { decimals: 8, assets: false, dex: false, staking: false, names: false, markets: false, shielded: false, send: true, receive: true, deposit: true, withdraw: true, embed: [] as string[] };
const CAPS = [{ ...base, stem: 'hivemind', markets: true, embed: ['markets'] }];
function entry(stem: string): SidechainEntry {
  return { stem, name: 'Hivemind', description: '', slot: 9, diskGB: 4, ramMB: 400, staking: false, enabled: true, state: 'running', pid: 1, restarts: 0, ready: true, dataDir: '', defaultDataDir: '', customDataDir: false, hasData: true, program: '', rpcPort: 0, port: 0, wallet: 'shared' };
}

const resolved = (c: string, text: string, outcome: string, extra: Partial<ChainDecision> = {}): ChainDecision => ({ id: id(c), text, scaled: false, eventperiod: 0, closeperiod: 1, status: 'resolved', outcome, answer: outcome === '1.00000000' ? 'yes' : 'no', ...extra });
const DECISIONS: ChainDecision[] = [
  { id: id('1'), text: 'Will it rain tomorrow?', scaled: false, eventperiod: 4, closeperiod: 5, status: 'open' },
  resolved('2', 'Did the bridge open?', '1.00000000'),
  resolved('3', 'Did it snow?', '0.00000000'),
  resolved('4', 'How many goals?', '0.40000000', { scaled: true, min: 0, max: 10, answer: 4 }),
  resolved('5', 'Who won the race? — Ann', '1.00000000', { closeperiod: 2 }),
  resolved('6', 'Who won the race? — Bob', '0.00000000', { closeperiod: 2 }),
  { id: id('7'), text: 'Was the launch on time?', scaled: false, eventperiod: 2, closeperiod: 3, status: 'voting' },
];
const st = (shares: string[], prices?: string[]) => shares.map((q, s) => ({ state: s, answers: '', shares: q, ...(prices ? { price: prices[s] } : {}) }));
const MARKETS: ChainMarket[] = [
  { id: id('a'), text: '[Weather] Rain tomorrow?', status: 'trading', liquidity: '1.00000000', height: 30, decisions: [id('1')], states: st(['0', '2'], ['0.11920292', '0.88079708']) },
  { id: id('b'), text: 'The bridge', status: 'settled', liquidity: '1.00000000', height: 10, decisions: [id('2')], states: st(['1.00000000', '3.00000000']) },
  { id: id('c'), text: 'Snow on Sunday', status: 'settled', liquidity: '1.00000000', height: 11, decisions: [id('3')], states: st(['0', '2.00000000']) },
  { id: id('d'), text: 'Goals in the final', status: 'settled', liquidity: '1.00000000', height: 12, decisions: [id('4')], states: st(['1.00000000', '0.50000000']) },
  { id: id('e'), text: 'The race', status: 'settled', liquidity: '1.00000000', height: 13, decisions: [id('5'), id('6')], states: st(['0', '2.00000000', '1.00000000', '0']) },
  { id: id('f'), text: 'The launch', status: 'closed', liquidity: '1.00000000', height: 20, decisions: [id('7')], states: st(['0.5', '1.5'], ['0.26894142', '0.73105858']) },
];
let calls: BridgeRequest[];
let reply: { markets: ChainMarket[]; decisions?: ChainDecision[]; info?: Record<string, unknown> };
let positions: unknown[];

beforeEach(async () => {
  app.phase = 'ready';
  nav.page = 'markets';
  nav.tab = '';
  prefs.chain = 'all';
  prefs.mask = false;
  prefs.uiMode = 'advanced';
  chainState.caps = {};
  chainState.capsLoaded = false;
  chainState.summaries = {};
  chainState.asked = {};
  many.positions = {};
  many.errors = {};
  forgetAsked();
  hub.selected = 'chains';
  hub.list = { network: 'regtest', rpcServer: true, chainsDataDir: '/d', chains: [entry('hivemind')] };
  wallets.list = [{ name: 'demo' }] as never;
  wallets.current = 'demo';
  calls = [];
  reply = { markets: structuredClone(MARKETS), decisions: structuredClone(DECISIONS), info: { period: 3, phase: 'seal', periodblocks: 6 } };
  positions = [];
  setTransport(async (r) => {
    calls.push(r);
    const p = (r.params ?? {}) as Record<string, unknown>;
    switch (r.method) {
      case 'chain.capabilities':
        return structuredClone(CAPS);
      case 'chain.summary':
        return { stem: p.stem, name: 'Hivemind', running: true, capabilities: CAPS[0], wallet: 'main', wallets: ['main'], height: 20, balances: { trusted: '5', pending: '0', immature: '0' }, errors: {} };
      case 'chain.markets':
        return { stem: p.stem, ...structuredClone(reply) };
      case 'chain.positions':
        return { stem: p.stem, wallet: 'main', positions };
      default:
        return null;
    }
  });
  await loadCapabilities();
});

describe('what a market came to, worked out', () => {
  it('prices a market that paid out from its shares, and what a share of each state paid', () => {
    expect(lastPrices({ liquidity: '1', states: st(['1', '3']) })).toEqual(lmsrPrices([1, 3], 1));
    expect(lastPrices({ liquidity: '1', states: st(['0', '2'], ['0.25', '0.75']) })).toEqual([0.25, 0.75]);
    expect(lastPrices({ liquidity: 'x', states: st(['x']) })).toEqual([0]);
    expect(statePayout([1, 0], 1)).toBe(1);
    expect(statePayout([0.4], 1)).toBeCloseTo(0.4, 12);
    expect(optionTexts(['Who won? — Ann', 'Who won? — Bob'])).toEqual(['Ann', 'Bob']);
  });

  it('every stage, the winner, the favourite, the scale, when it paid', () => {
    const ds = new Map(DECISIONS.map((d) => [d.id!, d]));
    const info = { period: 3, phase: 'seal', periodblocks: 6 };
    const v = (i: number) => marketView(MARKETS[i], ds, info);
    expect(v(0)).toMatchObject({ stage: 'trading', kind: 'binary', winner: null, paid: null });
    const bridge = v(1);
    expect(bridge).toMatchObject({ stage: 'paid', winner: { state: 1, won: true, pays: 1 }, favourite: 1, paid: { period: 2, height: 12 } });
    expect(bridge.rows.map((r) => [r.state, r.pays, r.lost])).toEqual([[1, 1, false], [0, 0, true]]);
    expect(v(2)).toMatchObject({ winner: { state: 0 }, favourite: 1 });
    const goals = v(3);
    expect(goals.kind).toBe('scalar');
    expect(goals.scale).toMatchObject({ min: 0, max: 10, answer: 4 });
    expect(goals.scale!.estimate).toBeCloseTo(10 * lmsrPrices([1, 0.5], 1)[1], 12);
    expect(v(4)).toMatchObject({ kind: 'categorical', winner: { state: 1, label: { options: ['Ann'] } }, paid: { height: 18 } });
    expect(v(5)).toMatchObject({ stage: 'voting', closes: 3 });
    expect(marketView(MARKETS[5], ds, { ...info, phase: 'reveal' }).stage).toBe('revealing');
    expect(marketView(MARKETS[5], ds, { ...info, period: 4 }).stage).toBe('counting');
    // A time-counted chain: the time its period started. Without decisions: no result, its prices kept.
    expect(marketView(MARKETS[1], ds, { period: 3, periodseconds: 600, starttime: 1000 }).paid).toEqual({ period: 2, time: 2200 });
    const bare = marketView(MARKETS[1], new Map(), null);
    expect(bare).toMatchObject({ stage: 'paid', winner: null, tie: false });
    expect(bare.rows.every((r) => r.pays === null && Number.isFinite(r.price))).toBe(true);
  });
});

describe('the Markets page', () => {
  it('each market with its stage and its outcomes as bars; what the ones that paid out came to', async () => {
    const { container } = render(Markets);
    await waitFor(() => expect(container.querySelectorAll('tr[data-market]').length).toBe(6));
    // Those that trade first, then those voted on, then those that paid out (the newest first).
    expect([...container.querySelectorAll('tr[data-market]')].map((r) => r.getAttribute('data-stage'))).toEqual(['trading', 'voting', 'paid', 'paid', 'paid', 'paid']);
    const row = (c: string) => container.querySelector(`tr[data-market="${id(c)}"]`) as HTMLElement;
    const bridge = row('b');
    expect(within(bridge).getByText('Paid out')).toBeTruthy();
    expect(within(bridge).getByText('Paid out at block 12')).toBeTruthy();
    expect(within(bridge).getByText('Won')).toBeTruthy();
    expect(within(bridge).getByText('100.0%')).toBeTruthy();
    expect(within(bridge).getByText('0.0%')).toBeTruthy();
    expect(bridge.querySelectorAll('.mark').length).toBe(2);
    expect(bridge.querySelector('.check')).toBeTruthy();
    expect(within(bridge).getByText('Last price at close: the market called it')).toBeTruthy();
    expect(within(row('c')).getByText('Last price at close: the market had Yes')).toBeTruthy();
    expect(within(row('d')).getByRole('img', { name: /On a scale from 0 to 10: the vote said 4, the market/ })).toBeTruthy();
    expect(within(row('d')).getByText('Resolved: 4')).toBeTruthy();
    expect(within(row('e')).getByText('Ann')).toBeTruthy();
    // A category in brackets is said apart from the title.
    expect(within(row('a')).getByText('Weather')).toBeTruthy();
    expect(within(row('a')).getByText('Rain tomorrow?')).toBeTruthy();
    expect(within(row('e')).getByText('Paid out at block 18')).toBeTruthy();
    const launch = row('f');
    expect(within(launch).getByText('Voting')).toBeTruthy();
    expect(within(launch).getByText('Last prices before trading closed')).toBeTruthy();
    expect(within(launch).getByText('73.1%')).toBeTruthy();
    expect(within(launch).getByText('Voted on until period 3')).toBeTruthy();
    for (const fill of container.querySelectorAll<HTMLElement>('.fill')) expect(fill.style.width).toMatch(/^\d+(\.\d+)?%$/);
    expect(container.textContent).not.toMatch(/NaN|undefined/);
  });

  it('a node of before the decisions were read: the markets with their prices, the bars of those that paid out from their shares', async () => {
    reply = { markets: structuredClone(MARKETS) };
    const { container } = render(Markets);
    await waitFor(() => expect(container.querySelectorAll('tr[data-market]').length).toBe(6));
    expect(container.querySelector('.check')).toBeNull();
    expect(container.textContent).not.toMatch(/NaN|undefined/);
    expect(within(container.querySelector(`tr[data-market="${id('b')}"]`) as HTMLElement).getByText('88.1%')).toBeTruthy();
  });
});

describe('Home\'s open positions', () => {
  it('a market voted on: "Voting", its price the final one', async () => {
    positions = [{ market: id('f'), account: 'rsc1qhm', state: 1, shares: '2', text: 'The launch', status: 'closed', answers: 'Y', price: '0.73105858' }];
    render(MarketPositions, { card: true });
    await waitFor(() => expect(document.querySelector(`[data-position="${id('f')}"]`)).toBeTruthy());
    const row = document.querySelector(`[data-position="${id('f')}"]`) as HTMLElement;
    expect(row.querySelector('[data-position-status="closed"]')?.textContent).toBe('Voting');
    expect(within(row).getByText('final')).toBeTruthy();
    expect(row.textContent).toContain('1.46211716');
  });

  it('a market on several decisions: the option held named; one a resolved decision answered against says Ruled out, worth 0', async () => {
    positions = [
      {
        market: id('e'), account: 'rsc1qhm', state: 2, shares: '1', text: 'The marathon', status: 'closed', answers: 'NY', price: '0.40000000',
        decisions: [{ text: 'Who wins the marathon? — Carl', status: 'voting' }, { text: 'Who wins the marathon? — Dan', status: 'resolved', outcome: '0.00000000' }],
      },
      {
        market: id('f'), account: 'rsc1qhm2', state: 1, shares: '2', text: 'The other race', status: 'closed', answers: 'YN', price: '0.25000000',
        decisions: [{ text: 'Who wins? — Ann', status: 'voting' }, { text: 'Who wins? — Bob', status: 'resolved', outcome: '0.00000000' }],
      },
    ];
    render(MarketPositions, { card: true });
    await waitFor(() => expect(document.querySelectorAll('[data-position]').length).toBe(2));
    const [dan, ann] = [...document.querySelectorAll<HTMLElement>('[data-position]')];
    expect(dan.textContent).toContain('Dan');
    expect(dan.querySelector('[data-ruled-out]')?.textContent).toBe('Ruled out');
    expect(dan.textContent).toContain('0.00000000');
    expect(ann.textContent).toContain('Ann');
    expect(ann.querySelector('[data-ruled-out]')).toBeNull();
    expect(ann.textContent).toContain('0.50000000');
  });
});
