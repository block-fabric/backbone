// The app's own pages over every chain (doc/unified-app.md §18): the adapter's calls of each chain, the filters of the
// read views and the chain of the action pages (remembered per page), the pages of what sidechains add shown only
// while a chain that has it is on, exact amounts, a wrong-chain address said before any send, an asset send, the
// wallets of each chain, the coins into and out of a sidechain, staking, assets, and a module's page inside an app's.
import { fireEvent, render, screen, waitFor } from '@testing-library/svelte';
import { beforeAll, beforeEach, describe, expect, it } from 'vitest';
import { setTransport, type BridgeRequest } from '../lib/bridge';
import { nav } from '../lib/nav.svelte';
import { hub } from '../lib/sidechains.svelte';
import { app, prefs, toasts, wallets } from '../lib/state.svelte';
import { applyPrefs } from '../lib/state.svelte';
import { packs } from '../lib/packs.svelte';
import { pageAvailable } from '../lib/commands.svelte';
import { modules, setEmbed, embeddedIn } from '../lib/modules.svelte';
import {
  actionChain,
  ALL,
  amountProblem,
  chainEntries,
  chainHost,
  chainLink,
  chainState,
  chainsWith,
  cleanChainViews,
  creditDeposits,
  deposits,
  formatUnits,
  HOST,
  loadCapabilities,
  mergeAssets,
  mergeTxs,
  noteDeposit,
  parseUnits,
  pendingDeposits,
  rowsOf,
  selectedChain,
  setActionChain,
  setSelectedChain,
  sidechainTxs,
  viewFilter,
} from '../lib/chains.svelte';
import SidechainSend from '../components/SidechainSend.svelte';
import SidechainReceive from '../components/SidechainReceive.svelte';
import ChainWallets from '../components/ChainWallets.svelte';
import ChainPeg from '../components/ChainPeg.svelte';
import Staking from '../pages/Staking.svelte';
import Assets from '../pages/Assets.svelte';
import Send from '../pages/Send.svelte';
import Transactions from '../pages/Transactions.svelte';
import type { SidechainEntry } from '../lib/unified';

function entry(stem: string, over: Partial<SidechainEntry> = {}): SidechainEntry {
  const names: Record<string, string> = { layers: 'Layers', thunder: 'Thunder', bitassets: 'BitAssets', fastblocks: 'FastBlocks' };
  return {
    stem,
    name: names[stem] ?? stem,
    description: '',
    slot: 7,
    diskGB: 4,
    ramMB: 400,
    staking: false,
    enabled: true,
    state: 'running',
    pid: 1,
    restarts: 0,
    ready: true,
    dataDir: '',
    defaultDataDir: '',
    customDataDir: false,
    hasData: true,
    program: '',
    rpcPort: 0,
    port: 0,
    wallet: 'shared',
    ...over,
  };
}

const CAPS = [
  { stem: 'layers', decimals: 8, assets: true, dex: true, staking: true, names: false, markets: false, shielded: false, send: true, receive: true, deposit: true, withdraw: true, embed: ['trade', 'staking'] },
  { stem: 'thunder', decimals: 8, assets: false, dex: false, staking: false, names: false, markets: false, shielded: false, send: true, receive: true, deposit: true, withdraw: true, embed: [] },
  { stem: 'bitassets', decimals: 8, assets: true, dex: true, staking: false, names: false, markets: false, shielded: false, send: true, receive: true, deposit: true, withdraw: true, embed: ['trade'] },
];
const GOLD = 'cc'.repeat(32);
const SILVER = 'dd'.repeat(32);

let calls: BridgeRequest[];
/** What chain.validate says of each address. */
let wrong: Record<string, string>;

function summary(stem: string) {
  const name = { layers: 'Layers', thunder: 'Thunder', bitassets: 'BitAssets' }[stem] ?? stem;
  return {
    stem,
    name,
    running: true,
    capabilities: CAPS.find((c) => c.stem === stem),
    wallet: 'main',
    wallets: ['main', 'savings'],
    walletMode: stem === 'thunder' ? 'separate' : 'shared',
    height: 42,
    balances: { trusted: stem === 'layers' ? '20.00000000' : '5.00000000', pending: '0.10000000', immature: '0' },
    ...(stem === 'layers' ? { assets: [{ id: GOLD, label: 'GOLD', decimals: 2, balance: '250.5', pending: '0', control: true, supply: '1000' }] } : {}),
    ...(stem === 'bitassets' ? { assets: [{ id: SILVER, label: 'SILVER', decimals: 0, balance: '7', pending: '0', control: false, supply: '100' }] } : {}),
    withdrawals: { count: stem === 'layers' ? 1 : 0, amount: '1.00000000' },
    errors: {},
  };
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
  toasts.list = [];
  chainState.caps = {};
  chainState.capsLoaded = false;
  chainState.summaries = {};
  chainState.asked = {};
  deposits.list = [];
  chainLink.send = null;
  chainLink.receive = null;
  chainLink.peg = null;
  modules.embed = null;
  modules.embedRect = null;
  packs.list = [];
  hub.selected = 'chains';
  hub.list = { network: 'regtest', rpcServer: true, chainsDataDir: '/d', chains: [entry('layers', { staking: true }), entry('thunder'), entry('bitassets', { enabled: false, state: 'disabled', ready: false })] };
  wallets.list = [{ name: 'demo' }] as never;
  wallets.current = 'demo';
  calls = [];
  wrong = {};
  setTransport(async (r) => {
    calls.push(r);
    const p = (r.params ?? {}) as Record<string, unknown>;
    switch (r.method) {
      case 'chain.capabilities':
        return structuredClone(CAPS);
      case 'chain.summary':
        return summary(String(p.stem));
      case 'chain.validate':
        if (wrong[String(p.address)]) throw { code: -5, message: wrong[String(p.address)] };
        return { address: p.address, own: false };
      case 'chain.send':
        return { stem: p.stem, wallet: p.wallet, txid: 'ab'.repeat(32) };
      case 'chain.receive':
        return { address: 'rsc1qnewaddress', wallet: 'main' };
      case 'chain.depositAddress':
        return { depositaddress: 's7_rsc1qdep_abcdef', address: 'rsc1qdep', slot: 7, wallet: 'main' };
      case 'chain.wallets':
        return { stem: p.stem, mode: p.stem === 'thunder' ? 'separate' : 'shared', selected: 'main', wallets: [{ name: 'main', loaded: true, trusted: '20.00000000' }, { name: 'savings', loaded: false }] };
      case 'chain.wallet.load':
      case 'chain.wallet.create':
        return { name: p.name };
      case 'chain.peg':
        return { stem: p.stem, wallet: 'main', mainchain: { state: 'ok', slot: 7, connected: true }, bmm: { mining: true, blocks: 4 }, withdrawals: [{ txid: 'ee'.repeat(32), vout: 0, amount: '1.00000000', status: 'waiting', mine: true }] };
      case 'chain.deposit':
        return { txid: 'ff'.repeat(32), address: 'rsc1qdep', stem: p.stem };
      case 'chain.withdraw':
        return { txid: '11'.repeat(32) };
      case 'chain.staking':
        return { stem: p.stem, wallet: 'main', node: { validator: true, enabled: true, stake: 5, totalstake: 5, blocksmade: 3 }, chain: { active: true, epoch: 2, slot: 9, staking: { bonded: '5.00000000' } }, set: { validators: [{ pubkey: 'ab'.repeat(32), stake: 5, slots: 8, local: true }] }, bonds: [{ validator: 'ab'.repeat(32), bonded: '5.00000000', inset: true }], errors: {} };
      case 'chain.transactions':
        return { stem: p.stem, wallet: 'main', rows: [{ txid: (p.stem === 'layers' ? 'a1' : 'b2').repeat(32), vout: 0, time: p.stem === 'layers' ? 200 : 100, category: 'receive', amount: '1.50000000', address: 'rsc1qdep', label: '', confirmations: 1, abandoned: false }] };
      case 'prefs.set':
        return {};
      default:
        return null;
    }
  });
  await loadCapabilities();
});

describe('the chains of the app\'s pages', () => {
  it('the app\'s own chain first, then each sidechain turned on, with its table', () => {
    const e = chainEntries();
    expect(e.map((x) => x.id)).toEqual([HOST, 'layers', 'thunder']);
    expect(e[0].host).toBe(true);
    expect(e[1].caps?.assets).toBe(true);
    expect(chainsWith('staking').map((x) => x.id)).toEqual(['layers']);
    expect(chainsWith('assets').map((x) => x.id)).toEqual(['layers']);
    expect(chainsWith('names')).toEqual([]);
  });

  it('the pages of what sidechains add only while a chain that has it is on', () => {
    expect(pageAvailable('staking')).toBe(true);
    expect(pageAvailable('assets')).toBe(true);
    expect(pageAvailable('names')).toBe(false);
    expect(pageAvailable('markets')).toBe(false);
    hub.list!.chains = [entry('thunder')];
    expect(pageAvailable('staking')).toBe(false);
    expect(pageAvailable('assets')).toBe(false);
  });

  it('one selection for every page: All by default, remembered, a chain turned off forgotten', () => {
    expect(selectedChain()).toBe(ALL);
    expect(viewFilter('transactions')).toBe(ALL);
    setSelectedChain('thunder');
    expect(viewFilter('transactions')).toBe('thunder');
    expect(viewFilter('home')).toBe('thunder');
    expect(calls.some((c) => c.method === 'prefs.set' && (c.params as Record<string, unknown>).chain === 'thunder')).toBe(true);
    setSelectedChain('Not a chain!');
    expect(selectedChain()).toBe('thunder');
    hub.list!.chains = [entry('layers')];
    expect(selectedChain()).toBe(ALL);
  });

  it('action views: the chain a link asks for, else the one selected; none with All (the form asks)', () => {
    expect(actionChain('send')).toBeNull();
    setActionChain('send', 'layers');
    expect(prefs.chain).toBe('layers');
    expect(actionChain('send')).toBe('layers');
    expect(actionChain('receive')).toBe('layers');
    expect(actionChain('send', chainEntries(), 'thunder')).toBe('thunder');
    expect(actionChain('send', chainEntries(), 'nowhere')).toBe('layers');
    setSelectedChain(HOST);
    expect(actionChain('receive')).toBe(HOST);
  });

  it('the stored preferences checked when read back', () => {
    expect(cleanChainViews({ home: 'all', send: 'layers', bad: 'x y', 'Not A Key': 'all', n: 5 })).toEqual({ home: 'all', send: 'layers' });
    expect(cleanChainViews(['all'])).toEqual({});
    applyPrefs({ chain: 'thunder', chainGroupsFolded: ['layers', '<b>', 5, 'layers'], chainWallets: { layers: 'savings', thunder: '../x' } });
    expect(prefs.chain).toBe('thunder');
    expect(prefs.chainGroupsFolded).toEqual(['layers']);
    expect(prefs.chainWallets).toEqual({ layers: 'savings' });
    applyPrefs({ chain: '<script>' });
    expect(prefs.chain).toBe('all');
  });

  it('amounts exact, as text', () => {
    expect(parseUnits('1.5', 8)).toBe(150000000n);
    expect(parseUnits('.25', 2)).toBe(25n);
    expect(parseUnits('1.', 8)).toBeNull();
    expect(parseUnits('1e3', 8)).toBeNull();
    expect(parseUnits('-1', 8)).toBeNull();
    expect(parseUnits('1.234', 2)).toBeNull();
    expect(formatUnits(-141n, 8)).toBe('-0.00000141');
    expect(formatUnits(2505n, 2)).toBe('25.05');
    expect(amountProblem('0', 8)).toBe('More than zero');
    expect(amountProblem('251', 2, '250.5')).toBe('More than the wallet holds');
    expect(amountProblem('250.5', 2, '250.5')).toBeNull();
    expect(amountProblem('1.5', 0)).toBe('A whole number');
  });

  it('the transactions of every chain in one list, newest first; one chain by the filter', () => {
    const a = sidechainTxs('layers', 'Layers', [{ txid: 'a1'.repeat(32), vout: 0, time: 200, category: 'send', amount: '-1.00000001', address: '', label: '', confirmations: 0, abandoned: false }, { txid: 'zz', vout: 0, time: 1, category: 'x', amount: '1', address: '', label: '', confirmations: 0, abandoned: false }]);
    const b = sidechainTxs('thunder', 'Thunder', [{ txid: 'b2'.repeat(32), vout: 0, time: 300, category: 'receive', amount: 'NaN', address: '', label: '', confirmations: 0, abandoned: false }, { txid: 'b3'.repeat(32), vout: 0, time: 100, category: 'receive', amount: '2', address: '', label: '', confirmations: 0, abandoned: false }]);
    expect(a).toHaveLength(1);
    expect(b).toHaveLength(1);
    const merged = mergeTxs([b, a]);
    expect(merged.map((x) => x.chain)).toEqual(['layers', 'thunder']);
    expect(merged[0].amount).toBe('-1.00000001');
    expect(rowsOf(merged, 'thunder').map((x) => x.chain)).toEqual(['thunder']);
    expect(rowsOf(merged, ALL)).toHaveLength(2);
  });

  it('the assets of many chains in one list', () => {
    const list = mergeAssets([summary('layers'), summary('bitassets')] as never);
    expect(list.map((x) => [x.chain, x.label])).toEqual([
      ['layers', 'GOLD'],
      ['bitassets', 'SILVER'],
    ]);
  });

  it('a deposit on its way until the sidechain\'s wallet shows it', () => {
    noteDeposit({ stem: 'layers', txid: 'ff'.repeat(32), address: 'rsc1qdep', amount: '2' });
    noteDeposit({ stem: 'layers', txid: 'not hex', address: 'x', amount: '1' });
    expect(pendingDeposits('layers')).toHaveLength(1);
    expect(creditDeposits('layers', [{ address: 'rsc1qother', amount: '2', category: 'receive' }])).toBe(0);
    expect(creditDeposits('layers', [{ address: 'rsc1qdep', amount: '2.00000000', category: 'receive' }])).toBe(1);
    expect(pendingDeposits()).toHaveLength(0);
  });
});

describe('the adapter\'s calls', () => {
  it('each method of the host, with the chain and the wallet chosen for it', async () => {
    prefs.chainWallets = { layers: 'savings' };
    await chainHost.summary('layers', 'savings');
    await chainHost.send({ stem: 'layers', wallet: 'savings', address: 'rsc1q', amount: '1.5', asset: GOLD });
    await chainHost.withdraw('layers', 'rchn1q', '2', '0.0002', 'savings');
    await chainHost.deposit('thunder', '3');
    const by = (m: string) => calls.find((c) => c.method === m)!.params as Record<string, unknown>;
    expect(by('chain.summary')).toEqual({ stem: 'layers', wallet: 'savings' });
    expect(by('chain.send')).toEqual({ stem: 'layers', wallet: 'savings', address: 'rsc1q', amount: '1.5', asset: GOLD });
    expect(by('chain.withdraw')).toEqual({ stem: 'layers', wallet: 'savings', address: 'rchn1q', amount: '2', fee: '0.0002' });
    expect(by('chain.deposit')).toEqual({ stem: 'thunder', amount: '3' });
  });
});

describe('Send on a sidechain', () => {
  it('the form\'s chain tiles: the app\'s own form, or the chain\'s', async () => {
    render(Send);
    await waitFor(() => expect(document.querySelectorAll('[data-chain-tile]').length).toBe(3));
    expect([...document.querySelectorAll('[data-chain-tile]')].map((e) => e.getAttribute('data-chain-tile'))).toEqual([HOST, 'layers', 'thunder']);
    await fireEvent.click(document.querySelector('[data-chain-tile="layers"]') as HTMLElement);
    await waitFor(() => expect(document.querySelector('[data-sidechain-send="layers"]')).toBeTruthy());
    expect(prefs.chain).toBe('layers');
  });

  it('a wrong-chain address said before anything is sent; then an asset sent on the chain', async () => {
    wrong['rchn1qmain'] = 'This is an address of Chains, not of Layers.';
    render(SidechainSend, { stem: 'layers', name: 'Layers' });
    await waitFor(() => expect(document.querySelector('[data-send-asset]')).toBeTruthy());
    const address = document.querySelector('[data-send-address]') as HTMLInputElement;
    const amount = document.querySelector('[data-send-amount]') as HTMLInputElement;
    await fireEvent.input(address, { target: { value: 'rchn1qmain' } });
    await fireEvent.input(amount, { target: { value: '1' } });
    await waitFor(() => expect(document.querySelector('[data-address-error]')?.textContent).toContain('not of Layers'), { timeout: 2000 });
    expect((document.querySelector('[data-send-go]') as HTMLButtonElement).disabled).toBe(true);
    expect(calls.some((c) => c.method === 'chain.send')).toBe(false);
    // A deposit address: said at once, not even asked.
    await fireEvent.input(address, { target: { value: 's7_rsc1qdep_abcdef' } });
    await waitFor(() => expect(document.querySelector('[data-address-error]')?.textContent).toContain('deposit address'));
    // The asset: its decimals, its balance.
    const asset = document.querySelector('[data-send-asset]') as HTMLSelectElement;
    asset.value = GOLD;
    await fireEvent.change(asset);
    await fireEvent.input(amount, { target: { value: '1.234' } });
    await waitFor(() => expect(document.body.textContent).toContain('A number with at most 2 decimals'));
    await fireEvent.input(amount, { target: { value: '25.5' } });
    await fireEvent.input(address, { target: { value: 'rsc1qlayers' } });
    await waitFor(() => expect(document.querySelector('[data-address-ok]')).toBeTruthy(), { timeout: 2000 });
    await fireEvent.click(document.querySelector('[data-send-go]') as HTMLElement);
    await waitFor(() => expect(document.querySelector('[data-send-done]')).toBeTruthy());
    expect(calls.find((c) => c.method === 'chain.send')!.params).toEqual({ stem: 'layers', wallet: 'main', address: 'rsc1qlayers', amount: '25.5', asset: GOLD });
  });

  it('a link to send an asset on its chain (an asset\'s row, a chain\'s card)', async () => {
    chainLink.send = { chain: 'layers', asset: GOLD };
    render(SidechainSend, { stem: 'layers', name: 'Layers' });
    await waitFor(() => expect((document.querySelector('[data-send-asset]') as HTMLSelectElement)?.value).toBe(GOLD));
    expect(chainLink.send).toBeNull();
  });

  it('a chain without assets has no asset picker', async () => {
    render(SidechainSend, { stem: 'thunder', name: 'Thunder' });
    await waitFor(() => expect(document.querySelector('[data-send-amount]')).toBeTruthy());
    expect(document.querySelector('[data-send-asset]')).toBeNull();
  });
});

describe('Receive, wallets, coins in and out, staking, assets', () => {
  it('Receive: a new address of the chain\'s wallet, its deposit address', async () => {
    render(SidechainReceive, { stem: 'layers', name: 'Layers' });
    await fireEvent.click(await waitFor(() => document.querySelector('[data-receive-new]') as HTMLElement));
    await waitFor(() => expect(document.querySelector('[data-receive-address="rsc1qnewaddress"]')).toBeTruthy());
    expect(calls.find((c) => c.method === 'chain.receive')!.params).toMatchObject({ stem: 'layers' });
    await fireEvent.click(document.querySelector('[data-receive-deposit-address]') as HTMLElement);
    await waitFor(() => expect(document.body.textContent).toContain('s7_rsc1qdep_abcdef'));
    await fireEvent.click(document.querySelector('[data-receive-deposit-go]') as HTMLElement);
    expect(nav.page).toBe('sidechains');
    expect(chainLink.peg).toBe('layers');
  });

  it('the wallets of a chain: its own backup said, open, use', async () => {
    render(ChainWallets, { stem: 'thunder', name: 'Thunder', running: true });
    await waitFor(() => expect(document.querySelector('[data-chain-wallet="savings"]')).toBeTruthy());
    expect(document.body.textContent).toContain('Separate backup');
    await fireEvent.click(screen.getByText('Open'));
    await waitFor(() => expect(calls.some((c) => c.method === 'chain.wallet.load' && (c.params as Record<string, unknown>).name === 'savings')).toBe(true));
  });

  it('coins into and out of a sidechain: a deposit noted on its way, a withdrawal to an address of the app\'s chain', async () => {
    render(ChainPeg, { stem: 'layers', name: 'Layers', running: true });
    await waitFor(() => expect(document.querySelector('[data-peg-bmm]')?.textContent).toContain('4'));
    const amount = document.querySelector('[data-peg-deposit-amount]') as HTMLInputElement;
    await fireEvent.input(amount, { target: { value: '2' } });
    await fireEvent.submit(document.querySelector('[data-peg-deposit="layers"]') as HTMLFormElement);
    await waitFor(() => expect(pendingDeposits('layers')).toHaveLength(1));
    expect(calls.find((c) => c.method === 'chain.deposit')!.params).toMatchObject({ stem: 'layers', amount: '2' });
    await waitFor(() => expect(document.querySelector('[data-peg-withdrawal]')).toBeTruthy());
  });

  it('Staking: the whole staking page of a chain that stakes (its own, under the title), a tab for each that stakes', async () => {
    render(Staking);
    await waitFor(() => expect(document.querySelector('[data-staking-chain="layers"]')).toBeTruthy());
    // Its own page in its frame, laid here without its title (headless), not a summary of the app's.
    const embed = document.querySelector('[data-staking-chain="layers"] [data-module-embed="layers"]');
    expect(embed?.getAttribute('data-embed-page')).toBe('staking');
    expect(document.querySelector('[data-staking-tab="layers"]')?.getAttribute('aria-selected')).toBe('true');
    expect(document.querySelector('[data-staking-tab="thunder"]')).toBeNull();
    expect(document.querySelector('[data-staking-chain="thunder"]')).toBeNull();
    // Nothing asked of every chain: the chain's own page asks its node.
    expect(calls.some((c) => c.method === 'chain.staking')).toBe(false);
  });

  it('Assets: the assets of every chain that has them, each sent on its chain', async () => {
    render(Assets);
    await waitFor(() => expect(document.querySelector(`[data-asset="${GOLD}"]`)).toBeTruthy());
    await fireEvent.click(document.querySelector(`[data-asset-send="${GOLD}"]`) as HTMLElement);
    expect(nav.page).toBe('send');
    expect(chainLink.send).toEqual({ chain: 'layers', asset: GOLD });
    expect(prefs.chain).toBe('layers');
  });

  it('Transactions: every chain by default with a Chain column; the app\'s own page by the top bar\'s selection', async () => {
    render(Transactions);
    await waitFor(() => expect(document.querySelectorAll('tr[data-chain-tx]').length).toBeGreaterThanOrEqual(2));
    expect(document.querySelector('[data-chain-chips]')).toBeNull();
    setSelectedChain(HOST);
    await waitFor(() => expect(document.querySelector('[data-chain-activity]')).toBeNull());
  });
});

describe('a module\'s page inside an app\'s page', () => {
  it('only checked pages, told to the module, lifted when left', () => {
    setEmbed({ stem: 'layers', page: 'trade' });
    expect(modules.embed).toEqual({ stem: 'layers', page: 'trade', tab: '' });
    expect(embeddedIn('layers')).toBe(false);
    modules.embedRect = { top: 10, left: 10, width: 300, height: 200, clipTop: 0, clipBottom: 0 };
    expect(embeddedIn('layers')).toBe(true);
    expect(embeddedIn('thunder')).toBe(false);
    setEmbed({ stem: 'Layers!', page: 'trade' });
    expect(modules.embed?.stem).toBe('layers');
    setEmbed({ stem: 'layers', page: '../x' });
    expect(modules.embed?.page).toBe('trade');
    setEmbed(null);
    expect(modules.embed).toBeNull();
    expect(modules.embedRect).toBeNull();
  });
});
