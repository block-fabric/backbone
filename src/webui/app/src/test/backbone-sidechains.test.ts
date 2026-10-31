// Backbone's sidechains (doc/unified-app.md §26, P1): Flush and Thunder offered on eCash where they can run; Flush's DEX
// as the Trade page's (its own Swap and Markets in its frame, never a second DEX page for it), its group with only what
// the app has no page of its own for; Thunder, a drop-in, with its wallet in its own pages (the app's Send, Receive, Move
// coins and Wallets leave it to them, and say where it is).
import { fireEvent, render, waitFor } from '@testing-library/svelte';
import { beforeAll, beforeEach, describe, expect, it } from 'vitest';
import { setTransport, type BridgeRequest } from '../lib/bridge';
import { nav } from '../lib/nav.svelte';
import { hub, refreshSidechains, sidechainsToRun } from '../lib/sidechains.svelte';
import { app, prefs, wallets } from '../lib/state.svelte';
import { modules } from '../lib/modules.svelte';
import { chainEntries, chainState, loadCapabilities, ownWallet } from '../lib/chains.svelte';
import { chainGroupItems } from '../lib/chaingroups';
import { chainColor } from '../lib/chaincolors';
import { firstRunSteps, type SidechainEntry } from '../lib/unified';
import { buildCommands } from '../lib/commands.svelte';
import SidechainSettings from '../components/SidechainSettings.svelte';
import ChainTiles from '../components/ChainTiles.svelte';
import Trade from '../pages/Trade.svelte';
import Send from '../pages/Send.svelte';

const NAMES: Record<string, string> = { flush: 'Flush', thunder: 'Thunder' };
function entry(stem: string, over: Partial<SidechainEntry> = {}): SidechainEntry {
  return {
    stem,
    name: NAMES[stem] ?? stem,
    description: '',
    slot: stem === 'flush' ? 46 : 9,
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
    wallet: stem === 'thunder' ? 'separate' : 'shared',
    ...over,
  };
}
const base = { decimals: 8, assets: false, dex: false, staking: false, names: false, markets: false, shielded: false, send: true, receive: true, deposit: true, withdraw: true, embed: [] as string[] };
// As the host says them on eCash (chainadapter.cpp): Flush's DEX with its own pages, Thunder a drop-in.
const CAPS = [
  { ...base, stem: 'flush', assets: true, dex: true, staking: true, coin: 'ECX', embed: ['trade', 'staking', 'assets', 'my-assets', 'pools', 'launch'], dexFeatures: { pair: true, liquidity: true, route: true, twap: true, curves: true, orders: false, stats: false, pages: true } },
  { ...base, stem: 'thunder', dropin: true, send: false, receive: false, deposit: false, withdraw: false },
];
let calls: BridgeRequest[] = [];

beforeAll(() => {
  HTMLDialogElement.prototype.showModal ??= function (this: HTMLDialogElement) {
    this.setAttribute('open', '');
  };
});

beforeEach(async () => {
  app.phase = 'ready';
  app.info = { ...(app.info ?? {}), network: 'ecash' } as typeof app.info;
  nav.page = 'trade';
  nav.tab = '';
  prefs.chain = 'all';
  prefs.uiMode = 'advanced';
  chainState.caps = {};
  chainState.capsLoaded = false;
  chainState.summaries = {};
  chainState.asked = {};
  modules.manifests = {};
  hub.selected = 'chains';
  hub.list = { network: 'ecash', rpcServer: true, chainsDataDir: '/d', chains: [entry('flush'), entry('thunder')] };
  wallets.list = [{ name: 'demo' }] as never;
  wallets.current = 'demo';
  wallets.balances = { demo: { trusted: 300000000n, untrustedPending: 0n, immature: 0n } };
  calls = [];
  setTransport(async (r) => {
    calls.push(r);
    const p = (r.params ?? {}) as Record<string, unknown>;
    switch (r.method) {
      case 'chain.capabilities':
        return structuredClone(CAPS);
      case 'chain.summary':
        return { stem: p.stem, name: NAMES[String(p.stem)], running: true, capabilities: CAPS.find((c) => c.stem === p.stem), balances: { trusted: '1.00000000', pending: '0', immature: '0' } };
      case 'sidechains.list':
        return structuredClone(hub.list);
      default:
        return null;
    }
  });
  await loadCapabilities();
});

describe('Flush and Thunder on eCash', () => {
  it('are offered where they can run: Settings → Sidechains, the first start, the command to run them', async () => {
    hub.list = { network: 'ecash', rpcServer: false, chainsDataDir: '/d', chains: [entry('flush', { enabled: false, state: 'disabled', ready: false }), entry('thunder', { enabled: false, state: 'disabled', ready: false })] };
    await refreshSidechains();
    expect(sidechainsToRun()).toBe(true);
    expect(firstRunSteps({ wallet: 'create', picked: [], storage: 'unified' }, true)).toContain('sidechains');
    expect(buildCommands().some((c) => c.id === 'chains.run')).toBe(true);
    render(SidechainSettings);
    await waitFor(() => expect(document.querySelector('[data-no-sidechains]')).toBeNull());
    await waitFor(() => expect(document.body.textContent).toContain('Thunder'));
    expect(document.body.textContent).toContain('Flush');
  });

  it('have colours of their own', () => {
    expect(chainColor('flush')).toBe('var(--chain-flush)');
    expect(chainColor('thunder')).toBe('var(--chain-thunder)');
  });
});

describe("Flush's DEX is the Trade page's", () => {
  it('shows its own Swap and Markets as the two tabs, in its frame, and reads nothing of the app\'s DEX views', async () => {
    render(Trade);
    await waitFor(() => expect(document.querySelectorAll('[data-trade-tab]').length).toBe(2));
    expect([...document.querySelectorAll('[data-trade-tab]')].map((b) => b.textContent?.trim())).toEqual(['Swap', 'Markets']);
    // No swap form of the app's (Flush's own is in its frame), no chain tiles for one chain.
    expect(document.querySelector('[data-swap]')).toBeNull();
    expect(document.querySelector('[data-chain-tile]')).toBeNull();
    expect(calls.some((c) => String(c.method).startsWith('chain.dex.'))).toBe(false);
    await fireEvent.click(document.querySelector('[data-trade-tab="pools"]') as HTMLButtonElement);
    expect(nav.tab).toBe('pools');
  });

  it('its group has only what the app has no page of its own for (assets, portfolio, launches)', () => {
    const flush = chainEntries().find((e) => e.id === 'flush')!;
    const ids = chainGroupItems(flush).map((i) => i.id);
    expect(ids).toEqual(['assets', 'my-assets', 'launch']);
    // Trade, Markets and Staking are the app's pages; merged mining (its Bidding panel) waits for P4.
    for (const id of ['trade', 'pools', 'staking', 'bmm', 'peg']) expect(ids).not.toContain(id);
  });
});

describe("Thunder's wallet in its own pages", () => {
  it('is a drop-in: its group leads with its wallet pages', () => {
    const thunder = chainEntries().find((e) => e.id === 'thunder')!;
    expect(ownWallet(thunder)).toBe(true);
    expect(ownWallet(chainEntries().find((e) => e.id === 'flush'))).toBe(false);
    expect(chainGroupItems(thunder).map((i) => i.id)).toEqual(['send', 'receive', 'transactions', 'peg', 'batchpay', 'payments', 'throughput']);
  });

  it('is not offered by the app\'s Send and Receive, and its page says where it is', async () => {
    nav.page = 'send';
    render(ChainTiles, { value: null, label: 'Send on', kind: 'send' });
    await waitFor(() => expect(document.querySelector('[data-chain-tile="flush"]')).toBeTruthy());
    expect(document.querySelector('[data-chain-tile="thunder"]')).toBeNull();
    document.body.innerHTML = '';
    prefs.chain = 'thunder';
    render(Send);
    await waitFor(() => expect(document.querySelector('[data-own-wallet="thunder"]')).toBeTruthy());
    await fireEvent.click(document.querySelector('[data-open-own="thunder"]') as HTMLButtonElement);
    expect(hub.selected).toBe('thunder');
    expect(calls.some((c) => c.method === 'chain.send')).toBe(false);
  });
});
