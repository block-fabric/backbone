// The sidechains as modules of one window (doc/unified-app.md §14) and their programs (§15): the switcher and the
// sidebar, the messages of a module's frame (checked), back and forward, the palette, the combined views (portfolio,
// every chain's transactions, the chain of a send, the status bar), the download step of the enable wizard.
import { fireEvent, render, screen, waitFor } from '@testing-library/svelte';
import { afterEach, beforeAll, beforeEach, describe, expect, it, vi } from 'vitest';
import Sidebar from '../components/Sidebar.svelte';
import StatusBar from '../components/StatusBar.svelte';
import Portfolio from '../components/Portfolio.svelte';
import ChainActivity from '../components/ChainActivity.svelte';
import ModuleProgram from '../components/ModuleProgram.svelte';
import ModuleFrames from '../components/ModuleFrames.svelte';
import NotificationBell from '../components/NotificationBell.svelte';
import { dispatch, setTransport, type BridgeRequest } from '../lib/bridge';
import { nav } from '../lib/nav.svelte';
import { hub, refreshSidechains, selectChain } from '../lib/sidechains.svelte';
import { app, prefs, toasts, wallets } from '../lib/state.svelte';
import { onFrameMessage, packs, registerFrame } from '../lib/packs.svelte';
import { buildCommands, palette } from '../lib/commands.svelte';
import { goBack, goForward, history, record, here, resetHistory } from '../lib/history.svelte';
import '../lib/keyboard';
import {
  activeModule,
  addModuleNotice,
  allowRaise,
  chainTxs,
  currentLook,
  mergeChainTxs,
  modules,
  moduleRoute,
  NODE_PAGE,
  openChains,
  openModule,
  clearModal,
  MODAL_MAX_MS,
  parseManifest,
  parseSummary,
  portfolioRows,
} from '../lib/modules.svelte';
import { downloads, proposedRelease, type ReleaseList } from '../lib/downloads.svelte';
import type { SidechainEntry, SidechainList } from '../lib/unified';

function entry(stem: string, over: Partial<SidechainEntry> = {}): SidechainEntry {
  const names: Record<string, string> = { layers: 'Layers', thunder: 'Thunder' };
  return {
    stem,
    name: names[stem] ?? stem,
    description: `${stem} for tests`,
    slot: 7,
    diskGB: 4,
    ramMB: 400,
    staking: stem === 'layers',
    enabled: true,
    state: 'running',
    pid: 1,
    restarts: 0,
    ready: true,
    dataDir: `/data/sidechains/${stem}/data`,
    defaultDataDir: `/data/sidechains/${stem}/data`,
    customDataDir: false,
    hasData: true,
    freeGB: 200,
    program: `/app/${stem}d`,
    rpcPort: 49320,
    port: 49321,
    wallet: 'shared',
    ...over,
  };
}

const MANIFEST = {
  version: 1,
  name: 'Layers',
  unit: 'LYR',
  home: 'home',
  sections: [
    { title: '', pages: ['home'] },
    { title: 'Wallet', pages: ['send', 'receive', 'transactions'] },
    { title: 'Chain', pages: ['trade', 'staking'] },
  ],
  pages: [
    { id: 'home', title: 'Home', icon: 'home', iconPath: '', section: '', summary: 'Its home', wallet: false },
    { id: 'send', title: 'Send', icon: 'send', iconPath: '', section: 'Wallet', summary: 'Send coins', wallet: true, shortcut: 'Alt+2' },
    { id: 'receive', title: 'Receive', icon: 'receive', iconPath: '', section: 'Wallet', summary: 'Get paid', wallet: true },
    { id: 'transactions', title: 'Transactions', icon: 'history', iconPath: '', section: 'Wallet', summary: 'History', wallet: true },
    { id: 'trade', title: 'Trade', icon: 'swap', iconPath: 'M4 8h14M14 4l4 4-4 4', section: 'Chain', summary: 'Swap assets', wallet: true, shortcut: 'Alt+9' },
    { id: 'staking', title: 'Staking', icon: 'stake', iconPath: 'M4 17.5c0 1.4 2.7 2.5 6 2.5', section: 'Chain', summary: 'Stake', wallet: false },
  ],
  commands: [{ id: 'ba.swap', label: 'Swap assets…', page: 'trade', tab: 'swap' }],
};

let state: SidechainList;
let calls: BridgeRequest[];
let posted: { m: unknown; origin: string }[];
let releases: ReleaseList;
let progress: Record<string, unknown>;
const frameWin = { postMessage: (m: unknown, origin: string) => posted.push({ m, origin }) } as unknown as Window;

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
  hub.list = null;
  hub.selected = 'chains';
  nav.page = 'home';
  nav.tab = '';
  modules.manifests = {};
  modules.routes = {};
  modules.summaries = {};
  modules.notices = [];
  modules.modal = {};
  modules.info = {};
  modules.overview = {};
  toasts.list = [];
  palette.open = false;
  packs.list = [{ stem: 'layers', name: 'Layers', hostApi: 1, commit: 'a'.repeat(40) }];
  packs.loaded = true;
  downloads.progress = {};
  downloads.updates = {};
  state = { network: 'regtest', rpcServer: true, chainsDataDir: '/data', chains: [entry('layers'), entry('thunder', { enabled: false, state: 'disabled', ready: false })] };
  calls = [];
  posted = [];
  progress = { state: 'idle', tag: '', done: 0, total: 0, error: '' };
  releases = {
    repo: 'block-fabric/layers',
    platform: 'x86_64-linux-gnu',
    hostApi: 1,
    signatures: 'auto',
    current: null,
    previous: null,
    source: 'none',
    installed: [],
    releases: [
      { tag: '20261021.1', name: 'Layers 20261021.1', prerelease: true, published: '2026-10-21', asset: { name: 'layers-20261021.1-x86_64-linux-gnu.tar.gz', size: 23_000_000 }, sums: true, manifest: true, signed: true, compatible: true, reason: '' },
      { tag: '20261020.1', name: 'Layers 20261020.1', prerelease: true, published: '2026-10-20', asset: { name: 'layers-20261020.1-x86_64-linux-gnu.tar.gz', size: 23_215_009 }, sums: true, manifest: false, signed: false, compatible: false, reason: 'Made before this app could run it: the release has no manifest-layers.json.' },
    ],
    newestCompatible: '20261021.1',
    updateAvailable: false,
  };
  setTransport(async (r) => {
    calls.push(r);
    const p = (r.params ?? {}) as Record<string, unknown>;
    switch (r.method) {
      case 'sidechains.list':
        return structuredClone(state);
      case 'sidechains.overview':
        return { stem: p.stem, name: 'Layers', state: 'running', mainchainHeight: 150, height: 42, mainchainConnected: true, mainchainSeen: 150, synced: true, blocks: [], wallet: { name: 'main', trusted: 2.5, pending: 0.25, immature: 0 } };
      case 'sidechains.rpc':
        if (p.method === 'listtransactions')
          return [
            { txid: 'aa'.repeat(32), time: 1_700_000_100, category: 'receive', amount: 1.25, address: 'lyr1qxyz', label: '', confirmations: 3 },
            { txid: 'zz', time: 1, category: 'receive', amount: 1 },
          ];
        throw { code: -32601, message: 'no' };
      case 'pack.info':
        return { stem: p.stem, pack: packs.list.find((x) => x.stem === p.stem) ?? null, running: true, compatible: true, reason: '', hostApi: 1 };
      case 'pack.list':
        return packs.list;
      case 'modules.releases':
        return structuredClone(releases);
      case 'modules.progress':
        return structuredClone(progress);
      case 'modules.install':
        progress = { state: 'downloading', tag: p.tag, done: 11_500_000, total: 23_000_000, error: '' };
        return true;
      case 'modules.checkUpdates':
        return { updates: [], errors: [] };
      default:
        throw { code: -32601, message: `No ${r.method}` };
    }
  });
  await refreshSidechains();
  registerFrame('layers', frameWin);
  resetHistory();
});

afterEach(() => {
  registerFrame('layers', null);
  setTransport(null);
});

const say = (type: string, data: unknown) => onFrameMessage({ source: frameWin, data: { chainsModule: 1, type, data } });

describe('what a module\'s frame says is checked', () => {
  it('takes a manifest of plain pages, drops what does not fit', () => {
    const m = parseManifest({
      ...MANIFEST,
      pages: [
        ...MANIFEST.pages,
        { id: '../evil', title: 'x', icon: 'home', iconPath: '', section: '', summary: '', wallet: false },
        { id: 'bidi', title: 'Pay‮me', icon: 'nope', iconPath: '<script>', section: '', summary: '', wallet: false, shortcut: 'Ctrl+Q' },
      ],
    })!;
    expect(m.pages.map((p) => p.id)).toEqual(['home', 'send', 'receive', 'transactions', 'trade', 'staking', 'bidi']);
    const bidi = m.pages.find((p) => p.id === 'bidi')!;
    expect(bidi.title).toBe('Pay me');
    expect(bidi.icon).toBe('dot');
    expect(bidi.iconPath).toBe('');
    expect(bidi.shortcut).toBeUndefined();
    expect(m.pages.find((p) => p.id === 'trade')!.iconPath).toBe('M4 8h14M14 4l4 4-4 4');
    expect(parseManifest({ pages: [], sections: [] })).toBeNull();
    expect(parseManifest('hello')).toBeNull();
  });

  it('takes amounts as decimals only, a few assets', () => {
    const s = parseSummary({ wallet: 'main', trusted: '1.50000000', pending: '1e9', assets: Array.from({ length: 30 }, (_, i) => ({ name: `A${i}`, amount: '1' })) })!;
    expect(s.trusted).toBe('1.50000000');
    expect(s.pending).toBeNull();
    expect(s.assets.length).toBe(20);
  });

  it('takes messages only from the registered frame, for its own module; names its notices after it; limits them', () => {
    const stranger = { postMessage: () => undefined } as unknown as Window;
    expect(onFrameMessage({ source: stranger, data: { chainsModule: 1, type: 'manifest', data: MANIFEST } })).toBe(false);
    expect(modules.manifests.thunder).toBeUndefined();
    expect(say('manifest', MANIFEST)).toBe(true);
    expect(modules.manifests.layers.pages.length).toBe(6);
    expect(say('toast', { kind: 'success', title: 'Sent' })).toBe(true);
    expect(toasts.list.at(-1)?.title).toBe('Layers · Sent');
    for (let i = 0; i < 10; i++) say('toast', { kind: 'info', title: `t${i}` });
    expect(toasts.list.filter((t) => t.title.startsWith('Layers ·')).length).toBeLessThanOrEqual(6);
    expect(allowRaise('other', 0)).toBe(true);
    expect(say('notice', { kind: 'tx', wallet: 'main', txid: 'ab'.repeat(32) })).toBe(true);
    expect(say('notice', { kind: 'tx', wallet: 'main', txid: 'ab'.repeat(32) })).toBe(false);
    expect(say('unknown', {})).toBe(false);
    // Not shown: it cannot dim the app.
    expect(say('modal', { open: true })).toBe(false);
    expect(modules.modal.layers).toBeFalsy();
  });
});

describe('one shell: the module switcher and the sidebar', () => {
  it('switching to a module swaps the sidebar\'s pages for its own, and back', async () => {
    say('manifest', MANIFEST);
    render(Sidebar);
    await waitFor(() => expect(document.querySelector('.switcher .chain[data-chain="layers"]')).toBeTruthy());
    // The app's own pages first.
    expect(document.querySelector('[data-module-page]')).toBeNull();
    await fireEvent.click(document.querySelector('.chain[data-chain="layers"]') as HTMLElement);
    expect(hub.selected).toBe('layers');
    expect(nav.page).toBe('sidechain');
    await waitFor(() => expect(document.querySelectorAll('[data-module-page]').length).toBe(7));
    // One navigation: one sidebar, the module's pages in it, its wallet card.
    expect(document.querySelectorAll('nav.sidebar').length).toBe(1);
    expect(document.querySelector('[data-module-page="staking"]')).toBeTruthy();
    expect(document.querySelector('.module-card')).toBeTruthy();
    // A page of the module: the frame is told, the sidebar marks it.
    posted = [];
    await fireEvent.click(document.querySelector('[data-module-page="trade"]') as HTMLElement);
    expect(moduleRoute('layers')).toEqual({ page: 'trade', tab: '' });
    expect(posted.some((p) => (p.m as { type?: string }).type === 'navigate' && (p.m as { data: { page: string } }).data.page === 'trade')).toBe(true);
    expect(posted.every((p) => p.origin === 'chains-pack://layers')).toBe(true);
    await waitFor(() => expect(document.querySelector('[data-module-page="trade"]')?.getAttribute('aria-current')).toBe('page'));
    // Back to the app: its own pages again.
    await fireEvent.click(document.querySelector('.chain[data-chain="chains"]') as HTMLElement);
    expect(hub.selected).toBe('chains');
    expect(nav.page).toBe('home');
    await waitFor(() => expect(document.querySelector('[data-module-page]')).toBeNull());
    // The module kept its place.
    openModule('layers');
    expect(moduleRoute('layers').page).toBe('trade');
  });

  it('a link inside the module moves the app with it', () => {
    say('manifest', MANIFEST);
    openModule('layers', 'home');
    say('route', { page: 'staking', tab: 'bonds' });
    expect(moduleRoute('layers')).toEqual({ page: 'staking', tab: 'bonds' });
    expect(say('route', { page: '../../x' })).toBe(false);
    expect(activeModule()).toBe('layers');
  });

  it('shows the frame chromeless in the main area once its node runs; its node page is the app\'s', async () => {
    selectChain('layers');
    render(ModuleFrames);
    await waitFor(() => expect(document.querySelector('iframe.module-frame[data-module="layers"]')).toBeTruthy());
    const frame = document.querySelector('iframe.module-frame') as HTMLIFrameElement;
    expect(frame.getAttribute('sandbox')).toBe('allow-scripts allow-same-origin');
    expect(frame.hidden).toBe(false);
    openModule('layers', NODE_PAGE);
    await waitFor(() => expect(frame.hidden).toBe(true));
  });
});

describe('back and forward are the app\'s', () => {
  it('goes back across the app\'s pages and a module\'s', () => {
    say('manifest', MANIFEST);
    record(here());
    openModule('layers', 'trade');
    record(here());
    openModule('layers', 'staking');
    record(here());
    expect(history.canBack).toBe(true);
    goBack();
    record(here());
    expect(moduleRoute('layers').page).toBe('trade');
    goBack();
    record(here());
    expect(hub.selected).toBe('chains');
    expect(nav.page).toBe('home');
    expect(history.canForward).toBe(true);
    goForward();
    record(here());
    expect(activeModule()).toBe('layers');
    expect(moduleRoute('layers').page).toBe('trade');
  });

  it('takes the app\'s shortcuts pressed in a frame: the palette, back, a page of the module', () => {
    say('manifest', MANIFEST);
    modules.info.layers = { stem: 'layers', pack: packs.list[0], running: true, compatible: true, reason: '' };
    vi.spyOn(document, 'hasFocus').mockReturnValue(true);
    openModule('layers', 'home');
    expect(say('key', { key: 'k', ctrl: true })).toBe(true);
    expect(palette.open).toBe(true);
    expect(say('key', { key: '9', alt: true })).toBe(true);
    expect(moduleRoute('layers').page).toBe('trade');
  });

  it('never lets a frame quit, minimize, toggle the mask or the sidebar; only the module shown, in focus, a few a second', () => {
    say('manifest', MANIFEST);
    modules.info.layers = { stem: 'layers', pack: packs.list[0], running: true, compatible: true, reason: '' };
    const focus = vi.spyOn(document, 'hasFocus').mockReturnValue(true);
    openModule('layers', 'home');
    const before = { mask: prefs.mask, collapsed: prefs.sidebarCollapsed, font: prefs.fontAdjust };
    for (const k of [{ key: 'q', ctrl: true }, { key: 'm', ctrl: true }, { key: 'M', ctrl: true, shift: true }, { key: 'b', ctrl: true }, { key: 'w', ctrl: true }, { key: '+', ctrl: true }, { key: 'Escape' }]) {
      expect(say('key', k)).toBe(false);
    }
    expect(calls.some((c) => c.method === 'app.quit' || c.method === 'window.minimize')).toBe(false);
    expect({ mask: prefs.mask, collapsed: prefs.sidebarCollapsed, font: prefs.fontAdjust }).toEqual(before);
    // The window not in focus: nothing.
    focus.mockReturnValue(false);
    expect(say('key', { key: '9', alt: true })).toBe(false);
    focus.mockReturnValue(true);
    // Not the module shown: nothing.
    openChains('home');
    expect(say('key', { key: 'k', ctrl: true })).toBe(false);
    openModule('layers', 'home');
    // A flood: cut.
    let taken = 0;
    for (let i = 0; i < 30; i++) if (say('key', { key: '9', alt: true })) taken++;
    expect(taken).toBeLessThanOrEqual(8);
    focus.mockRestore();
  });

  it('a dim of the module shown lifts on a click, on Escape, when leaving it, and after a while; never elsewhere', async () => {
    modules.info.layers = { stem: 'layers', pack: packs.list[0], running: true, compatible: true, reason: '' };
    openModule('layers', 'home');
    expect(say('modal', { open: true })).toBe(true);
    expect(modules.modal.layers).toBe(true);
    // Leaving the module lifts it (Shell does it on the move; here the call it makes).
    clearModal('layers');
    expect(modules.modal.layers).toBe(false);
    // A click on the dim: lifted, and refused again for a few seconds.
    say('modal', { open: true });
    clearModal('layers', 3000);
    expect(say('modal', { open: true })).toBe(false);
    // At most MODAL_MAX_MS.
    vi.useFakeTimers();
    try {
      clearModal('layers');
      vi.setSystemTime(Date.now() + 5000);
      expect(say('modal', { open: true })).toBe(true);
      vi.advanceTimersByTime(MODAL_MAX_MS + 1);
      expect(modules.modal.layers).toBe(false);
    } finally {
      vi.useRealTimers();
    }
  });

  it('puts the modules and their pages in the palette, named after them', () => {
    say('manifest', MANIFEST);
    const cmds = buildCommands();
    const trade = cmds.find((c) => c.id === 'module.layers.trade')!;
    expect(trade.arg).toBe('Layers: Trade');
    trade.run();
    expect(activeModule()).toBe('layers');
    expect(cmds.some((c) => c.id === 'module.layers.cmd.ba.swap')).toBe(true);
    expect(cmds.some((c) => c.id === 'module.thunder')).toBe(false);
  });
});

describe('the views across modules', () => {
  it('the combined portfolio: the app\'s row and each module\'s, with its assets', async () => {
    say('summary', { wallet: 'main', trusted: '3.00000000', pending: '0.00000000', assets: [{ name: 'GOLD', amount: '12' }] });
    expect(portfolioRows(state.chains.filter((c) => c.enabled))[0]).toMatchObject({ stem: 'layers', trusted: '3.00000000', assets: [{ name: 'GOLD', amount: '12' }] });
    render(Portfolio);
    await waitFor(() => expect(document.querySelector('[data-portfolio-row="layers"]')).toBeTruthy());
    expect(document.querySelector('[data-portfolio-row="chains"]')).toBeTruthy();
    expect(document.querySelector('[data-portfolio-row="layers"]')?.textContent).toContain('GOLD');
    await fireEvent.click(document.querySelector('[data-portfolio-row="layers"] .link') as HTMLElement);
    expect(activeModule()).toBe('layers');
    expect(moduleRoute('layers').page).toBe('home');
  });

  it('hides a module\'s amounts in privacy mode, as the app\'s own', async () => {
    say('summary', { wallet: 'main', trusted: '3.00000000', pending: '0.10000000', assets: [{ name: 'GOLD', amount: '12' }] });
    prefs.mask = true;
    try {
      render(Portfolio);
      await waitFor(() => expect(document.querySelector('[data-portfolio-row="layers"]')).toBeTruthy());
      const row = document.querySelector('[data-portfolio-row="layers"]')!;
      expect(row.textContent).not.toContain('3.00000000');
      expect(row.textContent).not.toContain('12');
      expect(row.textContent).toContain('••••••');
      openModule('layers', 'home');
      render(Sidebar);
      await waitFor(() => expect(document.querySelector('[data-field="module-balance"]')).toBeTruthy());
      expect(document.querySelector('[data-field="module-balance"]')?.textContent).not.toContain('3.0');
      render(ChainActivity, { chain: 'layers' });
      await waitFor(() => expect(document.querySelector('tr[data-chain-tx="layers"]')).toBeTruthy());
      expect(document.querySelector('tr[data-chain-tx="layers"]')?.textContent).not.toContain('1.25');
    } finally {
      prefs.mask = false;
    }
  });

  it('every chain\'s transactions, newest first, a module\'s read from its node', async () => {
    expect(chainTxs('layers', 'Layers', [{ txid: 'zz', amount: 1 }, { txid: 'ab'.repeat(32), amount: 'x' }])).toEqual([]);
    const merged = mergeChainTxs([chainTxs('a', 'A', [{ txid: 'aa'.repeat(32), time: 5, amount: 1 }]), chainTxs('b', 'B', [{ txid: 'bb'.repeat(32), time: 9, amount: -1 }])]);
    expect(merged.map((x) => x.chain)).toEqual(['b', 'a']);
    render(ChainActivity, { chain: 'layers' });
    await waitFor(() => expect(document.querySelectorAll('tr[data-chain-tx="layers"]').length).toBe(1));
    const call = calls.find((c) => c.method === 'sidechains.rpc')!;
    expect(call.params).toMatchObject({ stem: 'layers', method: 'listtransactions', wallet: 'main' });
    await fireEvent.click(document.querySelector('tr[data-chain-tx="layers"] .chip') as HTMLElement);
    expect(moduleRoute('layers').page).toBe('transactions');
  });

  it('one bell: a module\'s notices named after it', async () => {
    addModuleNotice('layers', { kind: 'system', title: 'Bond matured', text: 'Its coins are free' });
    render(NotificationBell);
    await fireEvent.click(document.querySelector('.bell') as HTMLElement);
    await waitFor(() => expect(document.querySelector('[data-module-notice="layers"]')).toBeTruthy());
    expect(document.querySelector('[data-module-notice="layers"]')?.textContent).toContain('Layers · Bond matured');
  });

  it('the status bar: the mainchain\'s state, and the module shown\'s', async () => {
    selectChain('layers');
    modules.overview.layers = { stem: 'layers', name: 'Layers', state: 'running', mainchainHeight: 10, height: 42, mainchainConnected: true, mainchainSeen: 10, synced: true };
    render(StatusBar);
    await waitFor(() => expect(document.querySelector('[data-module-sync="layers"]')).toBeTruthy());
    expect(document.querySelector('[data-module-sync="layers"]')?.textContent).toContain('In step with');
  });

  it('the look of the app, as tokens, for a frame', () => {
    document.documentElement.style.setProperty('--bg', '#010203');
    document.documentElement.dataset.theme = 'midnight';
    prefs.fontAdjust = 1;
    const l = currentLook();
    expect(l.theme).toBe('midnight');
    expect((l.tokens as Record<string, string>).bg).toBe('#010203');
    expect(l.fontAdjust).toBe(1);
    void wallets;
    void dispatch;
  });
});

describe('the program of a module: the download step', () => {
  it('proposes the newest release it can use; one it cannot is greyed with why; downloads with progress', async () => {
    expect(proposedRelease(releases)?.tag).toBe('20261021.1');
    let ready: boolean | null = null;
    render(ModuleProgram, { stem: 'layers', name: 'Layers', onready: (r: boolean) => (ready = r) });
    await waitFor(() => expect(document.querySelector('[data-release="20261020.1"]')).toBeTruthy());
    expect(ready).toBe(false);
    const old = document.querySelector('[data-release="20261020.1"]') as HTMLElement;
    expect(old.dataset.compatible).toBe('no');
    expect(old.textContent).toContain('no manifest-layers.json');
    expect((old.querySelector('input') as HTMLInputElement).disabled).toBe(true);
    expect(document.querySelector('[data-release="20261021.1"]')?.textContent).toContain('signed');
    await fireEvent.click(document.querySelector('[data-action="download"]') as HTMLElement);
    await waitFor(() => expect(document.querySelector('[data-progress="downloading"]')).toBeTruthy());
    expect(calls.some((c) => c.method === 'modules.install' && (c.params as Record<string, unknown>).tag === '20261021.1')).toBe(true);
    expect(document.querySelector('progress')).toBeTruthy();
    expect(document.querySelector('[data-action="cancel-download"]')).toBeTruthy();
    // Done (the host's event): the list again, its program there.
    progress = { state: 'done', tag: '20261021.1', done: 23_000_000, total: 23_000_000, error: '' };
    releases = { ...releases, current: '20261021.1', source: 'download', installed: ['20261021.1'] };
    dispatch([{ type: 'modules', data: { stem: 'layers' } }]);
    await waitFor(() => expect(ready).toBe(true));
    expect(document.querySelector('[data-module-program="layers"]')?.getAttribute('data-source')).toBe('download');
  });

  it('in Settings: an update the user approves, and the way back', async () => {
    releases = { ...releases, current: '20261020.9', previous: '20261019.4', source: 'download', updateAvailable: true };
    render(ModuleProgram, { stem: 'layers', name: 'Layers', mode: 'settings' });
    await waitFor(() => expect(document.querySelector('[data-update-available="20261021.1"]')).toBeTruthy());
    expect(document.querySelector('[data-action="rollback"]')?.textContent).toContain('20261019.4');
    expect(document.querySelector('[data-action="download"]')?.textContent).toContain('Update to 20261021.1');
  });
});
