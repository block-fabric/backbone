// The UX round of the app's pages over every chain (doc/unified-app.md §19): one chain selector in the top bar for every
// page (remembered, Ctrl+] and Ctrl+[, the palette), the sections of a page that apply to the chain shown only, the
// chain of Send and Receive picked in the form and in step with the selector, the balance chart one line per chain in
// its own coin, Hivemind's open positions on Home, and one group per sidechain in the sidebar's Chain section.
import { fireEvent, render, waitFor } from '@testing-library/svelte';
import { beforeAll, beforeEach, describe, expect, it } from 'vitest';
import { setTransport, type BridgeRequest } from '../lib/bridge';
import { nav } from '../lib/nav.svelte';
import { hub } from '../lib/sidechains.svelte';
import { app, prefs, wallets } from '../lib/state.svelte';
import { packs } from '../lib/packs.svelte';
import { buildCommands, moduleCommands, pageAvailable } from '../lib/commands.svelte';
import { handleShortcut } from '../lib/keyboard';
import { modules } from '../lib/modules.svelte';
import { ALL, chainEntries, chainLink, chainState, cycleChain, HOST, loadCapabilities, selectedChain, selectedWith, setSelectedChain } from '../lib/chains.svelte';
import { aggregateItems, chainGroupItems } from '../lib/chaingroups';
import { chainBalanceHistory, forgetAsked, many, positionValue } from '../lib/manychains.svelte';
import { chainColor } from '../lib/chaincolors';
import GlobalChainSelector from '../components/GlobalChainSelector.svelte';
import SidebarChainGroups from '../components/SidebarChainGroups.svelte';
import ChainBalanceChart from '../components/ChainBalanceChart.svelte';
import MarketPositions from '../components/MarketPositions.svelte';
import Send from '../pages/Send.svelte';
import Receive from '../pages/Receive.svelte';
import Staking from '../pages/Staking.svelte';
import Assets from '../pages/Assets.svelte';
import type { SidechainEntry } from '../lib/unified';

const NAMES: Record<string, string> = { layers: 'Layers', thunder: 'Thunder', bitassets: 'BitAssets', hivemind: 'Hivemind' };
function entry(stem: string, over: Partial<SidechainEntry> = {}): SidechainEntry {
  return { stem, name: NAMES[stem] ?? stem, description: '', slot: 7, diskGB: 4, ramMB: 400, staking: false, enabled: true, state: 'running', pid: 1, restarts: 0, ready: true, dataDir: '', defaultDataDir: '', customDataDir: false, hasData: true, program: '', rpcPort: 0, port: 0, wallet: 'shared', ...over };
}
const base = { decimals: 8, assets: false, dex: false, staking: false, names: false, markets: false, shielded: false, send: true, receive: true, deposit: true, withdraw: true, embed: [] as string[] };
const CAPS = [
  { ...base, stem: 'layers', assets: true, dex: true, staking: true, embed: ['trade', 'staking'] },
  { ...base, stem: 'thunder' },
  { ...base, stem: 'bitassets', assets: true, dex: true, embed: ['trade'] },
  { ...base, stem: 'hivemind', markets: true, embed: ['markets', 'decisions'] },
];
const MARKET = 'ee'.repeat(32);
let calls: BridgeRequest[];
let positions: unknown[];

function summary(stem: string) {
  return { stem, name: NAMES[stem], running: true, capabilities: CAPS.find((c) => c.stem === stem), wallet: 'main', wallets: ['main'], height: 42, balances: { trusted: stem === 'layers' ? '20.00000000' : '5.00000000', pending: '0', immature: '0' }, errors: {} };
}

beforeAll(() => {
  const proto = HTMLDialogElement.prototype as unknown as Record<string, unknown>;
  proto.showModal ??= function (this: HTMLDialogElement) {
    this.setAttribute('open', '');
  };
  proto.close ??= function (this: HTMLDialogElement) {
    this.removeAttribute('open');
  };
});

beforeEach(async () => {
  app.phase = 'ready';
  nav.page = 'home';
  nav.tab = '';
  prefs.chain = 'all';
  prefs.chainGroupsFolded = [];
  prefs.chainWallets = {};
  prefs.mask = false;
  prefs.uiMode = 'advanced';
  chainState.caps = {};
  chainState.capsLoaded = false;
  chainState.summaries = {};
  chainState.asked = {};
  chainLink.send = null;
  chainLink.receive = null;
  many.txs = {};
  many.positions = {};
  many.activity = {};
  many.errors = {};
  forgetAsked();
  modules.manifests = {};
  packs.list = [];
  hub.selected = 'chains';
  hub.list = { network: 'regtest', rpcServer: true, chainsDataDir: '/d', chains: [entry('layers'), entry('thunder'), entry('bitassets', { enabled: false, state: 'disabled', ready: false }), entry('hivemind', { enabled: false, state: 'disabled', ready: false })] };
  wallets.list = [{ name: 'demo' }] as never;
  wallets.current = 'demo';
  wallets.balances = { demo: { trusted: 300000000n, untrustedPending: 0n, immature: 0n } };
  calls = [];
  positions = [{ market: MARKET, account: 'rsc1qhm', state: 1, shares: '12.5', text: 'Rain tomorrow?', status: 'trading', answers: 'Y', price: '0.75000000' }];
  setTransport(async (r) => {
    calls.push(r);
    const p = (r.params ?? {}) as Record<string, unknown>;
    switch (r.method) {
      case 'chain.capabilities':
        return structuredClone(CAPS);
      case 'chain.summary':
        return summary(String(p.stem));
      case 'chain.positions':
        return { stem: p.stem, wallet: 'main', positions };
      case 'chain.validate':
        return { address: p.address, own: false };
      case 'chain.staking':
        return { stem: p.stem, wallet: 'main', node: { validator: true, enabled: true, stake: 250100000000, totalstake: 250100000000 }, set: { validators: [{ pubkey: 'ab'.repeat(32), stake: 250100000000, slots: 8, local: true }] }, bonds: [], errors: {} };
      case 'chain.transactions':
        return { stem: p.stem, wallet: 'main', rows: [] };
      case 'wallet.transactions':
        return { height: 1, records: [] };
      default:
        return null;
    }
  });
  await loadCapabilities();
});

describe('one chain selector for every page', () => {
  it('All by default; a chain picked in the top bar is the selection, remembered', async () => {
    render(GlobalChainSelector);
    const button = document.querySelector('[data-global-chain]') as HTMLButtonElement;
    expect(button.getAttribute('data-global-chain')).toBe(ALL);
    expect(button.textContent).toContain('All chains');
    await fireEvent.click(button);
    const options = [...document.querySelectorAll('[data-chain-option]')].map((o) => o.getAttribute('data-chain-option'));
    expect(options).toEqual([ALL, HOST, 'layers', 'thunder']);
    await fireEvent.click(document.querySelector('[data-chain-option="layers"]') as HTMLElement);
    expect(prefs.chain).toBe('layers');
    expect(calls.some((c) => c.method === 'prefs.set' && (c.params as Record<string, unknown>).chain === 'layers')).toBe(true);
    await waitFor(() => expect(document.querySelector('[data-global-chain]')?.getAttribute('data-global-chain')).toBe('layers'));
    expect(document.querySelector('[data-global-chain-list]')).toBeNull();
  });

  it('not there with this app\'s own chain alone', () => {
    hub.list!.chains = [];
    render(GlobalChainSelector);
    expect(document.querySelector('[data-global-chain]')).toBeNull();
  });

  it('Ctrl+] and Ctrl+[ go round All and each chain; the palette has each', () => {
    const key = (k: string) => handleShortcut(new KeyboardEvent('keydown', { key: k, ctrlKey: true }));
    expect(key(']')).toBe(true);
    expect(selectedChain()).toBe(HOST);
    key(']');
    key(']');
    expect(selectedChain()).toBe('thunder');
    key(']');
    expect(selectedChain()).toBe(ALL);
    key('[');
    expect(selectedChain()).toBe('thunder');
    expect(cycleChain(1)).toBe(ALL);
    const ids = moduleCommands().map((c) => c.id);
    expect(ids).toEqual(expect.arrayContaining(['chain.select.all', 'chain.select.chains', 'chain.select.layers', 'chain.next', 'chain.previous', 'chain.layers.trade', 'chain.layers.staking']));
    expect(ids).not.toContain('chain.thunder.staking');
    moduleCommands().find((c) => c.id === 'chain.layers.trade')!.run();
    expect(prefs.chain).toBe('layers');
    expect(nav.page).toBe('assets');
    expect(nav.tab).toBe('trade');
  });
});

describe('the sections of a page follow the chain shown', () => {
  it('a page of what some chains add only while one of them is shown', () => {
    expect(pageAvailable('staking')).toBe(true);
    setSelectedChain('thunder');
    expect(pageAvailable('staking')).toBe(false);
    expect(pageAvailable('assets')).toBe(false);
    expect(buildCommands().some((c) => c.id === 'page.staking')).toBe(false);
    setSelectedChain('layers');
    expect(pageAvailable('staking')).toBe(true);
    expect(selectedWith('assets').map((e) => e.id)).toEqual(['layers']);
  });

  it('Staking with Thunder shown: said, the chains that stake a click away; stakes in the chain\'s coin', async () => {
    setSelectedChain('thunder');
    render(Staking);
    await waitFor(() => expect(document.querySelector('[data-not-on-chain="thunder"]')).toBeTruthy());
    expect(document.body.textContent).toContain('Thunder has no staking');
    await fireEvent.click(document.querySelector('[data-show-chain="layers"]') as HTMLElement);
    expect(prefs.chain).toBe('layers');
    // 250100000000 base units: 2501 of its coin (as its bonds are), never the raw number.
    await waitFor(() => expect(document.querySelector('[data-stake-total]')?.textContent).toContain('2501.00000000'));
    expect(document.body.textContent).not.toContain('250100000000');
  });

  it('Assets → Trade with All and two chains of markets: asks which chain', async () => {
    hub.list!.chains = [entry('layers'), entry('bitassets')];
    nav.tab = 'trade';
    render(Assets);
    await waitFor(() => expect(document.querySelector('[data-trade-pick]')).toBeTruthy());
    await fireEvent.click(document.querySelector('[data-trade-chain="bitassets"]') as HTMLElement);
    expect(prefs.chain).toBe('bitassets');
  });
});

describe('Send and Receive: the chain is the form\'s first field, in step with the top bar', () => {
  it('All: no form until a chain is picked; picking one selects it everywhere; the selector changes the form', async () => {
    render(Send);
    await waitFor(() => expect(document.querySelectorAll('[data-chain-tile]').length).toBe(3));
    expect(document.querySelector('[data-send-wait]')).toBeTruthy();
    expect(document.querySelector('[data-chain-needed]')).toBeTruthy();
    expect(document.querySelector('[data-chain-tile][aria-checked="true"]')).toBeNull();
    // Each tile says what its wallet holds there, in its own coin.
    await waitFor(() => expect(document.querySelector('[data-chain-tile="layers"]')?.textContent).toContain('20.00000000'));
    await fireEvent.click(document.querySelector('[data-chain-tile="layers"]') as HTMLElement);
    expect(prefs.chain).toBe('layers');
    await waitFor(() => expect(document.querySelector('[data-sidechain-send="layers"]')).toBeTruthy());
    expect(document.querySelector('[data-send-asset-list]')).toBeTruthy();
    setSelectedChain('thunder');
    await waitFor(() => expect(document.querySelector('[data-sidechain-send="thunder"]')).toBeTruthy());
    expect(document.querySelector('[data-send-asset-list]')).toBeNull();
    expect(document.querySelector('[data-chain-tile="thunder"]')?.getAttribute('aria-checked')).toBe('true');
    setSelectedChain(HOST);
    await waitFor(() => expect(document.querySelector('[data-sidechain-send]')).toBeNull());
    expect(document.querySelector('[data-send-wait]')).toBeNull();
  });

  it('Receive the same', async () => {
    render(Receive);
    await waitFor(() => expect(document.querySelector('[data-receive-wait]')).toBeTruthy());
    await fireEvent.click(document.querySelector('[data-chain-tile="thunder"]') as HTMLElement);
    await waitFor(() => expect(document.querySelector('[data-sidechain-receive="thunder"]')).toBeTruthy());
    expect(prefs.chain).toBe('thunder');
  });

  it('the tiles\' amounts hidden in privacy mode', async () => {
    prefs.mask = true;
    render(Send);
    await waitFor(() => expect(document.querySelectorAll('[data-chain-tile]').length).toBe(3));
    expect(document.querySelector('[data-chain-tile="layers"]')?.textContent).not.toContain('20.0');
    expect(document.querySelector('[data-chain-tile="layers"]')?.textContent).toContain('••••••');
  });
});

describe('the balance chart: one line per chain, each in its own coin', () => {
  it('a sidechain\'s balance over time from its history, back from what it holds now', () => {
    const points = chainBalanceHistory(
      [
        { txid: 'a'.repeat(64), vout: 0, category: 'receive', amount: '10.00000000', address: '', label: '', confirmations: 3, time: 100, abandoned: false },
        { txid: 'b'.repeat(64), vout: 0, category: 'send', amount: '-4.00000000', fee: '-0.00000141', address: '', label: '', confirmations: 1, time: 200, abandoned: false },
      ],
      { balances: { trusted: '6.00000000', pending: '0.00000000', immature: '0' } } as never,
      300,
    );
    expect(points.map((p) => [p.time, p.balance])).toEqual([
      [99, 141n],
      [100, 1000000141n],
      [200, 600000000n],
      [300, 600000000n],
    ]);
  });

  it('two chains: two lines, a legend with each one\'s balance and unit; never one total', async () => {
    const series = [
      { id: HOST, name: 'Chains', unit: 'CHN', color: chainColor(HOST), points: [{ time: 100, balance: 100000000n }, { time: 200, balance: 300000000n }] },
      { id: 'layers', name: 'Layers', unit: 'LYR', color: chainColor('layers'), points: [{ time: 150, balance: 2000000000n }, { time: 250, balance: 1500000000n }] },
    ];
    render(ChainBalanceChart, { series });
    await waitFor(() => expect(document.querySelectorAll('[data-chart-series]').length).toBe(2));
    expect(document.querySelector('[data-chart-series="chains"]')?.textContent).toContain('3.00000000');
    expect(document.querySelector('[data-chart-series="chains"]')?.textContent).toContain('CHN');
    expect(document.querySelector('[data-chart-series="layers"]')?.textContent).toContain('15.00000000');
    expect(document.querySelector('[data-chart-series="layers"]')?.textContent).toContain('LYR');
    expect(document.querySelectorAll('svg path.line').length).toBe(2);
    expect(document.body.textContent).not.toContain('18.00000000');
    expect(chainColor('layers')).not.toBe(chainColor('thunder'));
  });

  it('hidden in privacy mode, the legend\'s amounts too', async () => {
    prefs.mask = true;
    render(ChainBalanceChart, { series: [{ id: 'layers', name: 'Layers', unit: 'LYR', color: '#000', points: [{ time: 1, balance: 1n }, { time: 2, balance: 2n }] }, { id: 'thunder', name: 'Thunder', unit: 'T', color: '#111', points: [{ time: 1, balance: 1n }, { time: 2, balance: 5n }] }] });
    expect(document.querySelector('svg')).toBeNull();
    expect(document.body.textContent).toContain('Hidden: privacy mode');
    expect(document.querySelector('[data-chart-series="layers"]')?.textContent).toContain('••••••');
  });
});

describe('Hivemind\'s open positions on Home', () => {
  it('none: no card; positions: market, outcome, shares, price, worth now; a link to Markets', async () => {
    hub.list!.chains = [entry('layers'), entry('hivemind')];
    positions = [];
    const first = render(MarketPositions, { card: true });
    await waitFor(() => expect(calls.some((c) => c.method === 'chain.positions')).toBe(true));
    expect(document.querySelector('[data-market-positions]')).toBeNull();
    first.unmount();
    positions = [{ market: MARKET, account: 'rsc1qhm', state: 1, shares: '12.5', text: 'Rain tomorrow?', status: 'trading', answers: 'Y', price: '0.75000000' }, { market: 'not hex', state: 0, shares: '1' }];
    forgetAsked();
    render(MarketPositions, { card: true });
    await waitFor(() => expect(document.querySelector(`[data-position="${MARKET}"]`)).toBeTruthy());
    const row = document.querySelector(`[data-position="${MARKET}"]`)!.textContent!;
    expect(row).toContain('Rain tomorrow?');
    expect(row).toContain('Yes');
    expect(row).toContain('12.5');
    expect(row).toContain('0.75000000');
    expect(row).toContain('9.37500000');
    expect(document.querySelectorAll('[data-position]').length).toBe(1);
    await fireEvent.click(document.querySelector('[data-open-markets]') as HTMLElement);
    expect(nav.page).toBe('markets');
    expect(nav.tab).toBe('positions');
    expect(prefs.chain).toBe('hivemind');
  });

  it('worth exactly shares × price; hidden in privacy mode; not shown with another chain selected', async () => {
    expect(positionValue({ shares: '3', price: '0.33333333' })).toBe('0.99999999');
    expect(positionValue({ shares: '1' })).toBe('');
    hub.list!.chains = [entry('hivemind'), entry('thunder')];
    prefs.mask = true;
    render(MarketPositions, { card: true });
    await waitFor(() => expect(document.querySelector(`[data-position="${MARKET}"]`)).toBeTruthy());
    expect(document.querySelector(`[data-position="${MARKET}"]`)!.textContent).not.toContain('9.375');
    setSelectedChain('thunder');
    await waitFor(() => expect(document.querySelector('[data-market-positions]')).toBeNull());
  });
});

describe('one group per sidechain in the sidebar\'s Chain section', () => {
  it('each chain\'s own features; an item selects its chain; a group folds, remembered', async () => {
    hub.list!.chains = [entry('layers'), entry('thunder'), entry('hivemind')];
    const items = (id: string) => chainGroupItems(chainEntries().find((e) => e.id === id)!).map((i) => i.id);
    expect(items('layers')).toEqual(['assets', 'trade', 'staking', 'move', 'own']);
    expect(items('thunder')).toEqual(['move', 'own']);
    expect(items('hivemind')).toEqual(['markets', 'positions', 'votes', 'move', 'own']);
    expect(chainGroupItems(chainEntries().find((e) => e.id === 'layers')!, true).map((i) => i.id)).toEqual(['assets', 'trade', 'staking', 'move']);
    render(SidebarChainGroups);
    await waitFor(() => expect(document.querySelectorAll('[data-chain-group]').length).toBe(3));
    await fireEvent.click(document.querySelector('[data-chain-item="layers:staking"]') as HTMLElement);
    expect(prefs.chain).toBe('layers');
    expect(nav.page).toBe('staking');
    expect(document.querySelector('[data-chain-item="layers:staking"]')?.getAttribute('aria-current')).toBe('page');
    await fireEvent.click(document.querySelector('[data-chain-group-head="thunder"]') as HTMLElement);
    expect(prefs.chainGroupsFolded).toEqual(['thunder']);
    await waitFor(() => expect(document.querySelector('[data-chain-item="thunder:move"]')).toBeNull());
    await fireEvent.click(document.querySelector('[data-chain-item="hivemind:votes"]') as HTMLElement);
    expect(hub.selected).toBe('hivemind');
    expect(nav.page).toBe('sidechain');
    expect(prefs.chain).toBe('hivemind');
  });

  it('the views over many chains while All is shown: All assets with two chains of assets', () => {
    hub.list!.chains = [entry('layers'), entry('bitassets')];
    expect(aggregateItems(chainEntries()).map((a) => a.label)).toEqual(['All assets']);
    expect(aggregateItems(chainEntries(), 'layers')).toEqual([]);
    hub.list!.chains = [entry('layers')];
    expect(aggregateItems(chainEntries())).toEqual([]);
  });
});
