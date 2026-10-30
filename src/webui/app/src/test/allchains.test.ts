// Every chain of the catalog first-class in the app's own pages (doc/unified-app.md §25): zSide's private coins on Home and
// Overview, its Send from its private coins and to yourself (shield, unshield); BitNames' names held, found by the palette
// (Ctrl+K) and the explorer's search, opened in its own directory; a module's frame told the page it was asked once it
// listens (a first visit); the validator key of a chain that stakes said in Settings' list.
import { fireEvent, render, waitFor } from '@testing-library/svelte';
import { beforeAll, beforeEach, describe, expect, it } from 'vitest';
import { setTransport, type BridgeRequest } from '../lib/bridge';
import { nav } from '../lib/nav.svelte';
import { hub } from '../lib/sidechains.svelte';
import { app, prefs, toasts, wallets } from '../lib/state.svelte';
import { chainState, loadCapabilities, openName, searchChain } from '../lib/chains.svelte';
import { frameMade, modules, moduleTabOk, onModuleMessage, openModule, setTeller } from '../lib/modules.svelte';
import { palette } from '../lib/commands.svelte';
import SidechainSend from '../components/SidechainSend.svelte';
import ChainsOverview from '../components/ChainsOverview.svelte';
import CommandPalette from '../components/CommandPalette.svelte';
import AllExplorer from '../components/AllExplorer.svelte';
import type { SidechainEntry } from '../lib/unified';

function entry(stem: string, over: Partial<SidechainEntry> = {}): SidechainEntry {
  const names: Record<string, string> = { zside: 'zSide', bitnames: 'BitNames', fastblocks: 'FastBlocks' };
  return { stem, name: names[stem] ?? stem, description: '', slot: 6, diskGB: 3, ramMB: 500, staking: false, enabled: true, state: 'running', pid: 1, restarts: 0, ready: true, dataDir: '', defaultDataDir: '', customDataDir: false, hasData: true, program: '', rpcPort: 0, port: 0, wallet: 'separate', ...over };
}

const BASE = { decimals: 8, assets: false, dex: false, staking: false, names: false, markets: false, shielded: false, send: true, receive: true, deposit: true, withdraw: true, embed: [] as string[] };
const CAPS = [
  { ...BASE, stem: 'zside', shielded: true, embed: ['private'] },
  { ...BASE, stem: 'bitnames', names: true, embed: ['names', 'register', 'directory'] },
  { ...BASE, stem: 'fastblocks', staking: true, embed: ['staking'] },
];

let calls: BridgeRequest[];
let told: { stem: string; type: string; data: unknown }[];

function summary(stem: string) {
  return {
    stem,
    name: { zside: 'zSide', bitnames: 'BitNames', fastblocks: 'FastBlocks' }[stem] ?? stem,
    running: true,
    capabilities: CAPS.find((c) => c.stem === stem),
    wallet: 'main',
    wallets: ['main'],
    walletMode: 'separate',
    height: 12,
    balances: { trusted: '27.99990000', pending: '0', immature: '0' },
    ...(stem === 'zside' ? { shielded: { available: '10.00000000', spendable: '9.50000000', pending: '0' } } : {}),
    ...(stem === 'bitnames' ? { names: { count: 2, reservations: 1 } } : {}),
    withdrawals: { count: 0, amount: '0' },
    errors: {},
  };
}

beforeAll(() => {
  Element.prototype.scrollIntoView ??= function () {};
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
  prefs.mask = false;
  toasts.list = [];
  chainState.caps = {};
  chainState.capsLoaded = false;
  chainState.summaries = {};
  modules.routes = {};
  modules.embed = null;
  hub.selected = 'chains';
  hub.list = { network: 'regtest', rpcServer: true, chainsDataDir: '/d', chains: [entry('fastblocks', { staking: true, validatorKey: true }), entry('bitnames'), entry('zside', { phraseKeys: false })] };
  wallets.list = [{ name: 'demo' }] as never;
  wallets.current = 'demo';
  calls = [];
  told = [];
  setTeller((stem, type, data) => {
    told.push({ stem, type, data });
    return true;
  });
  setTransport(async (r) => {
    calls.push(r);
    const p = (r.params ?? {}) as Record<string, unknown>;
    switch (r.method) {
      case 'chain.capabilities':
        return structuredClone(CAPS);
      case 'chain.summary':
        return summary(String(p.stem));
      case 'chain.validate':
        return { address: p.address, own: false, ...(p.shielded ? { shielded: true } : {}) };
      case 'chain.send':
      case 'chain.shield':
        return { stem: p.stem, wallet: 'main', txid: 'ab'.repeat(32) };
      case 'chain.search':
        if (p.stem === 'bitnames' && p.q === 'mallory') return { results: [{ kind: 'name', name: '\u202emallory', seq: '0001-0002', owner: 'rsc1q' }, { kind: 'name', name: 'mallory', seq: '<b>', owner: 'rsc1q' }] };
        return p.stem === 'bitnames' && p.q === 'alice' ? { stem: 'bitnames', q: 'alice', results: [{ kind: 'name', name: 'alice', namehash: 'aa'.repeat(32), seq: '0001-0001', owner: 'rsc1qowner', mine: true, height: 9 }] } : { results: [] };
      case 'prefs.set':
        return {};
      default:
        return null;
    }
  });
  await loadCapabilities();
});

describe('zSide: its coins and its private coins', () => {
  it('both on Overview: the private coins in what the chain also holds', async () => {
    chainState.summaries.zside = summary('zside') as never;
    chainState.summaries.bitnames = summary('bitnames') as never;
    render(ChainsOverview, { page: 'overview', panel: true });
    await waitFor(() => expect(document.querySelector('[data-chain-card="zside"] [data-card-private]')).toBeTruthy());
    expect(document.querySelector('[data-chain-card="zside"] [data-card-private]')!.textContent).toContain('10.00000000');
    // BitNames: its names, and the reservation waiting.
    expect(document.querySelector('[data-chain-card="bitnames"] [data-card-names]')!.textContent).toMatch(/2 names.*1 reservation/);
    expect(document.body.textContent).toContain('Also holds');
  });

  it('Send: from its private coins with a memo; to yourself, shielded or unshielded, its own question', async () => {
    render(SidechainSend, { stem: 'zside', name: 'zSide' });
    await waitFor(() => expect(document.querySelector('[data-send-from-pick="private"]')).toBeTruthy());
    // The two accounts, each with what it holds; the coins first.
    expect(document.querySelector('[data-send-from-pick="coins"]')!.getAttribute('aria-checked')).toBe('true');
    expect(document.querySelector('[data-send-from-pick="private"]')!.textContent).toContain('9.50000000');
    // To yourself from the coins: shield, no address asked.
    await fireEvent.click(document.querySelector('[data-send-to-self]')!);
    expect(document.querySelector('[data-send-self="shield"]')).toBeTruthy();
    expect(document.querySelector('[data-send-address]')).toBeNull();
    await fireEvent.input(document.querySelector('[data-send-amount]')!, { target: { value: '3' } });
    await waitFor(() => expect((document.querySelector('[data-send-go]') as HTMLButtonElement).disabled).toBe(false));
    expect(document.querySelector('[data-send-go]')!.textContent).toContain('Shield');
    await fireEvent.click(document.querySelector('[data-send-go]')!);
    await waitFor(() => expect(calls.some((c) => c.method === 'chain.shield')).toBe(true));
    expect(calls.find((c) => c.method === 'chain.shield')!.params).toMatchObject({ stem: 'zside', direction: 'shield', amount: '3' });
    // Done: the next one pays someone again.
    await waitFor(() => expect(document.querySelector('[data-send-address]')).toBeTruthy());
    // From the private coins to an address, with a memo.
    await fireEvent.click(document.querySelector('[data-send-from-pick="private"]')!);
    await fireEvent.input(document.querySelector('[data-send-address]')!, { target: { value: 'zs1qprivate' } });
    await fireEvent.input(document.querySelector('[data-send-amount]')!, { target: { value: '1.5' } });
    await fireEvent.input(document.querySelector('#send-memo-zside')!, { target: { value: 'For the coffee' } });
    await waitFor(() => expect(document.querySelector('[data-address-ok]')).toBeTruthy(), { timeout: 3000 });
    await fireEvent.click(document.querySelector('[data-send-go]')!);
    await waitFor(() => expect(calls.some((c) => c.method === 'chain.send')).toBe(true));
    expect(calls.find((c) => c.method === 'chain.send')!.params).toMatchObject({ stem: 'zside', address: 'zs1qprivate', amount: '1.5', shielded: true, memo: 'For the coffee' });
    // From the private coins to yourself: unshield.
    await fireEvent.click(document.querySelector('[data-send-from-pick="private"]')!);
    await fireEvent.click(document.querySelector('[data-send-to-self]')!);
    expect(document.querySelector('[data-send-self="unshield"]')).toBeTruthy();
    await fireEvent.input(document.querySelector('[data-send-amount]')!, { target: { value: '0.5' } });
    await waitFor(() => expect((document.querySelector('[data-send-go]') as HTMLButtonElement).disabled).toBe(false));
    await fireEvent.click(document.querySelector('[data-send-go]')!);
    await waitFor(() => expect(calls.filter((c) => c.method === 'chain.shield').length).toBe(2));
    expect(calls.filter((c) => c.method === 'chain.shield')[1].params).toMatchObject({ direction: 'unshield', amount: '0.5' });
  });
});

describe('BitNames: names found and opened', () => {
  it('the palette (Ctrl+K) looks a name up on each chain of names, and opens its directory at it', async () => {
    render(CommandPalette);
    palette.open = true;
    await waitFor(() => expect(document.querySelector('dialog.palette input')).toBeTruthy());
    await fireEvent.input(document.querySelector('dialog.palette input')!, { target: { value: 'alice' } });
    await waitFor(() => expect([...document.querySelectorAll('dialog.palette .option')].some((o) => /alice — a name on BitNames · #0001-0001 · yours/.test(o.textContent ?? ''))).toBe(true), { timeout: 3000 });
    // Asked of BitNames only (the chain with names), never of zSide.
    expect(calls.filter((c) => c.method === 'chain.search').map((c) => (c.params as Record<string, unknown>).stem)).toEqual(['bitnames']);
    // No empty group of the pages beside it.
    expect(document.body.textContent).not.toContain('0 results');
    const option = [...document.querySelectorAll('dialog.palette .option')].find((o) => /alice/.test(o.textContent ?? '')) as HTMLElement;
    await fireEvent.click(option);
    expect(prefs.chain).toBe('bitnames');
    expect(modules.routes.bitnames).toEqual({ page: 'directory', tab: 'name:alice' });
    expect(told.some((m) => m.stem === 'bitnames' && m.type === 'navigate' && JSON.stringify(m.data) === JSON.stringify({ page: 'directory', tab: 'name:alice' }))).toBe(true);
    palette.open = false;
  });

  it("a digit-only text or another shape is not looked up as a name; a name tab only of the names' characters", async () => {
    render(CommandPalette);
    palette.open = true;
    await waitFor(() => expect(document.querySelector('dialog.palette input')).toBeTruthy());
    for (const q of ['12345', 'Bad Name', '<img>']) {
      await fireEvent.input(document.querySelector('dialog.palette input')!, { target: { value: q } });
      await new Promise((r) => setTimeout(r, 400));
    }
    expect(calls.some((c) => c.method === 'chain.search')).toBe(false);
    palette.open = false;
    expect(moduleTabOk('name:blog.core')).toBe(true);
    expect(moduleTabOk('name:#1739-0029')).toBe(true);
    expect(moduleTabOk('name:<img>')).toBe(false);
    expect(moduleTabOk('name:Alice')).toBe(false);
    openName('bitnames', 'x y');
    expect(modules.routes.bitnames).toBeUndefined();
  });

  it("the explorer's search over every chain names the name found, yours, and opens it in its chain's directory", async () => {
    render(AllExplorer);
    const box = await waitFor(() => document.querySelector('[data-all-search]') as HTMLInputElement);
    expect(box.placeholder).toContain('name');
    await fireEvent.input(box, { target: { value: 'alice' } });
    await fireEvent.submit(box.closest('form')!);
    await waitFor(() => expect(document.querySelector('[data-found="bitnames:name"]')).toBeTruthy());
    const row = document.querySelector('[data-found="bitnames:name"]')!;
    expect(row.textContent).toMatch(/Name alice.*yours.*#0001-0001/);
    await fireEvent.click(row);
    expect(modules.routes.bitnames).toEqual({ page: 'directory', tab: 'name:alice' });
    expect(await searchChain('zside', 'alice')).toEqual([]);
    // A node's answer that is not a name's (a mark turning text around, markup for its number): not listed.
    expect(await searchChain('bitnames', 'mallory')).toEqual([]);
  });
});

describe("a module's frame made at its first visit", () => {
  it('is told the page asked as soon as it listens, and its own first place does not move the app', () => {
    frameMade('bitnames');
    openModule('bitnames', 'directory', 'name:alice');
    told = [];
    // Its first word: its home (it did not hear the app yet).
    expect(onModuleMessage('bitnames', 'route', { page: 'home', tab: '' })).toBe(true);
    expect(modules.routes.bitnames).toEqual({ page: 'directory', tab: 'name:alice' });
    expect(told).toEqual([{ stem: 'bitnames', type: 'navigate', data: { page: 'directory', tab: 'name:alice' } }]);
    // Listening now: where its links go, the app follows.
    onModuleMessage('bitnames', 'route', { page: 'names', tab: '' });
    expect(modules.routes.bitnames).toEqual({ page: 'names', tab: '' });
    expect(told.length).toBe(1);
  });
});
