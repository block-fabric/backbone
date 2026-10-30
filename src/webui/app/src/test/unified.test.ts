// The unified app (doc/unified-app.md): the logic of Settings → Sidechains, the chain switcher and the
// wizards (lib/unified.ts), and the components against a pretend host.
import { fireEvent, render, screen, waitFor, within } from '@testing-library/svelte';
import { afterEach, beforeAll, beforeEach, describe, expect, it } from 'vitest';
import SidebarChainGroups from '../components/SidebarChainGroups.svelte';
import EnableWizard from '../components/EnableWizard.svelte';
import FirstRunWizard from '../components/FirstRunWizard.svelte';
import SidechainSettings from '../components/SidechainSettings.svelte';
import SidechainView from '../pages/SidechainView.svelte';
import ModuleFrames from '../components/ModuleFrames.svelte';
import { modules, NODE_PAGE, openModule } from '../lib/modules.svelte';
import { dispatch, setTransport, type BridgeRequest } from '../lib/bridge';
import { nav } from '../lib/nav.svelte';
import { hub, refreshSidechains, selectChain, sidechainsToRun } from '../lib/sidechains.svelte';
import { buildCommands } from '../lib/commands.svelte';
import { seed } from '../lib/seed.svelte';
import { app, prefs } from '../lib/state.svelte';
import { deliver, installPackRoute, onFrameMessage, packs, packUrl, registerFrame, route, wrap } from '../lib/packs.svelte';
import {
  canContinue,
  defaultChoices,
  describeState,
  describeSync,
  diskCheck,
  enablePlan,
  enableSteps,
  firstRunSteps,
  nextStep,
  previousStep,
  restoreWords,
  switcherItems,
  totalResources,
  type SidechainEntry,
  type SidechainList,
} from '../lib/unified';

function entry(stem: string, over: Partial<SidechainEntry> = {}): SidechainEntry {
  const names: Record<string, string> = { layers: 'Layers', fastblocks: 'FastBlocks', thunder: 'Thunder' };
  return {
    stem,
    name: names[stem] ?? stem,
    description: `${stem} for tests`,
    slot: 7,
    diskGB: 4,
    ramMB: 400,
    staking: stem !== 'thunder',
    enabled: false,
    state: 'disabled',
    pid: 0,
    restarts: 0,
    ready: false,
    dataDir: `/data/sidechains/${stem}/data`,
    defaultDataDir: `/data/sidechains/${stem}/data`,
    customDataDir: false,
    hasData: false,
    freeGB: 200,
    program: `/app/${stem}d`,
    rpcPort: 49320,
    port: 49321,
    wallet: 'shared',
    ...over,
  };
}

function listOf(...chains: SidechainEntry[]): SidechainList {
  return { network: 'regtest', rpcServer: false, chainsDataDir: '/data', chains };
}

describe('words and the switcher', () => {
  it('says how a sidechain is doing in plain words', () => {
    expect(describeState({ state: 'disabled', enabled: false })).toMatchObject({ key: 'Off', tone: 'off' });
    expect(describeState({ state: 'running', enabled: true, ready: true })).toMatchObject({ key: 'Running', tone: 'ok' });
    expect(describeState({ state: 'running', enabled: true, ready: false }).tone).toBe('busy');
    expect(describeState({ state: 'crashed', enabled: true }).tone).toBe('warn');
    expect(describeState({ state: 'failed', enabled: true }).tone).toBe('bad');
    expect(describeState({ state: 'stopped', enabled: true }).key).toBe('Waiting to start');
  });

  it('says whether it is in step with Chains', () => {
    const base = { stem: 'layers', name: 'Layers', state: 'running' as const, mainchainHeight: 150 };
    expect(describeSync(null).key).toBe('Loading…');
    expect(describeSync({ ...base, height: 3, mainchainConnected: false }).tone).toBe('warn');
    expect(describeSync({ ...base, height: 3, mainchainConnected: true, mainchainSeen: 150, synced: true })).toMatchObject({ key: 'In step with {chain}', tone: 'ok' });
    const behind = describeSync({ ...base, height: 3, mainchainConnected: true, mainchainSeen: 90, synced: false });
    expect(behind.tone).toBe('busy');
    expect(behind.args).toEqual([90, 150]);
    expect(describeSync({ ...base, state: 'crashed' }).tone).toBe('warn');
  });

  it('has no switcher for Chains alone, and Chains first with every sidechain on', () => {
    expect(switcherItems(null)).toEqual([]);
    expect(switcherItems(listOf(entry('layers'), entry('thunder')))).toEqual([]);
    const items = switcherItems(listOf(entry('layers', { enabled: true, state: 'running', ready: true }), entry('thunder'), entry('fastblocks', { enabled: true, state: 'crashed' })));
    expect(items.map((i) => i.id)).toEqual(['chains', 'layers', 'fastblocks']);
    expect(items[1].tone).toBe('ok');
    expect(items[2].tone).toBe('warn');
    // One being stopped stays until it stopped.
    expect(switcherItems(listOf(entry('layers', { enabled: false, state: 'stopping' }))).map((i) => i.id)).toEqual(['chains', 'layers']);
  });

  it('checks the room where the data goes', () => {
    expect(diskCheck(4, undefined).tone).toBe('warn');
    expect(diskCheck(4, 2).tone).toBe('bad');
    expect(diskCheck(4, 8).tone).toBe('warn');
    expect(diskCheck(4, 200)).toMatchObject({ tone: 'ok', args: [200] });
  });
});

describe('the enable wizard', () => {
  it('asks about staking only on a chain that stakes', () => {
    expect(enableSteps(entry('layers'))).toEqual(['about', 'program', 'storage', 'wallet', 'staking', 'review']);
    expect(enableSteps(entry('thunder'))).toEqual(['about', 'program', 'storage', 'wallet', 'review']);
    const steps = enableSteps(entry('thunder'));
    expect(nextStep(steps, 'wallet')).toBe('review');
    expect(nextStep(steps, 'review')).toBeNull();
    expect(previousStep(steps, 'about')).toBeNull();
  });

  it('needs a folder that can be used before going on', () => {
    const c = defaultChoices(entry('layers'));
    expect(c.storage).toBe('default');
    expect(canContinue('storage', c)).toBe(true);
    c.storage = 'custom';
    expect(canContinue('storage', c)).toBe(false);
    c.customPath = '/mnt/disk2/layers';
    expect(canContinue('storage', c)).toBe(true);
    c.customProblem = 'That folder holds the data of Chains: choose another one.';
    expect(canContinue('storage', c)).toBe(false);
  });

  it('asks the host to move the data only when the folder changes', () => {
    const e = entry('layers', { hasData: true });
    const c = defaultChoices(e);
    expect(enablePlan(e, c)).toEqual({ setDir: null, wallet: 'shared', stake: false });
    c.storage = 'custom';
    c.customPath = '/mnt/disk2/layers';
    c.wallet = 'separate';
    expect(enablePlan(e, c)).toEqual({ setDir: { dir: '/mnt/disk2/layers', move: true }, wallet: 'separate', stake: false });
    // A validator of this computer: only for a chain that stakes, when chosen.
    c.stake = true;
    expect(enablePlan(entry('thunder', { staking: false }), c).stake).toBe(false);
    expect(enablePlan(entry('fastblocks', { staking: true }), c).stake).toBe(true);
    // Already there: nothing to move; back to the default: null dir.
    const custom = entry('layers', { customDataDir: true, dataDir: '/mnt/disk2/layers' });
    expect(enablePlan(custom, defaultChoices(custom)).setDir).toBeNull();
    const back = { ...defaultChoices(custom), storage: 'default' as const };
    expect(enablePlan(custom, back).setDir).toEqual({ dir: null, move: false });
  });
});

describe('the first-start guide', () => {
  it('has a backup step for a new wallet, a restore step otherwise, storage only with sidechains', () => {
    expect(firstRunSteps({ wallet: 'create', picked: [], storage: 'unified' })).toEqual(['welcome', 'wallet', 'backup', 'network', 'sidechains', 'sync']);
    expect(firstRunSteps({ wallet: 'restore', picked: ['layers'], storage: 'unified' })).toEqual(['welcome', 'wallet', 'restore', 'network', 'sidechains', 'storage', 'sync']);
  });
  it('adds up what the sidechains picked take', () => {
    const list = [entry('layers'), entry('thunder', { diskGB: 10, ramMB: 600 }), entry('fastblocks', { diskGB: 3 })];
    expect(totalResources(list, ['layers', 'thunder'])).toEqual({ diskGB: 14, ramMB: 1000 });
    expect(totalResources(list, [])).toEqual({ diskGB: 0, ramMB: 0 });
  });
  it('takes 12 or 24 words to restore', () => {
    expect(restoreWords('  Abandon  ability able ').words).toEqual(['abandon', 'ability', 'able']);
    expect(restoreWords('a b c').ok).toBe(false);
    expect(restoreWords(Array(12).fill('word').join(' ')).ok).toBe(true);
    expect(restoreWords(Array(24).fill('word').join('\n')).ok).toBe(true);
  });
});

// ---- The components, against a pretend host ----

beforeAll(() => {
  const proto = HTMLDialogElement.prototype as unknown as Record<string, unknown>;
  proto.showModal ??= function (this: HTMLDialogElement) {
    this.setAttribute('open', '');
  };
  proto.close ??= function (this: HTMLDialogElement) {
    this.removeAttribute('open');
  };
});

let state: SidechainList;
let calls: BridgeRequest[];

beforeEach(() => {
  app.phase = 'ready';
  prefs.sidebarAllPages = false;
  hub.list = null;
  hub.selected = 'chains';
  hub.wizard = null;
  modules.info = {};
  modules.routes = {};
  modules.manifests = {};
  nav.page = 'home';
  state = listOf(entry('layers'), entry('fastblocks'), entry('thunder', { program: '' }));
  calls = [];
  packList = [];
  packCompatible = true;
  packs.list = [];
  packs.loaded = false;
  setTransport(async (r) => {
    calls.push(r);
    const p = (r.params ?? {}) as Record<string, unknown>;
    switch (r.method) {
      case 'sidechains.list':
        return structuredClone(state);
      case 'sidechains.enable': {
        const e = state.chains.find((c) => c.stem === p.stem)!;
        Object.assign(e, { enabled: true, state: 'running', ready: true, pid: 4242, wallet: p.wallet });
        return true;
      }
      case 'sidechains.disable': {
        const e = state.chains.find((c) => c.stem === p.stem)!;
        Object.assign(e, { enabled: false, state: 'disabled', ready: false, pid: 0 });
        return true;
      }
      case 'sidechains.overview':
        return { stem: p.stem, name: 'Layers', state: 'running', mainchainHeight: 150, height: 12, mainchainConnected: true, mainchainSeen: 150, synced: true, blocks: [{ height: 12, hash: 'ab'.repeat(32), time: 1_700_000_000, txs: 1 }], wallet: { name: 'main', trusted: 1.5, pending: 0, immature: 0 } };
      case 'dialog.ask':
        return true;
      case 'pack.list':
        return packList;
      case 'pack.info':
        return { stem: p.stem, pack: packList.find((x) => x.stem === p.stem) ?? null, running: true, compatible: packCompatible, reason: packCompatible ? '' : 'daemon', hostApi: packCompatible ? 1 : 0 };
      case 'pack.call':
        return { routed: p };
      default:
        throw { code: -32601, message: `No ${r.method}` };
    }
  });
});

afterEach(() => {
  setTransport(null);
});

let packList: { stem: string; name: string; hostApi: number; commit: string }[] = [];
let packCompatible = true;

describe('the pages of a sidechain in a frame (§13)', () => {
  it('wraps a request of the frame as the app\'s own, its id kept apart', () => {
    expect(wrap('layers', { rid: '7', kind: 'rpc', method: 'getblockcount', params: [] })).toEqual({ kind: 'host', method: 'pack.call', params: { stem: 'layers', request: { kind: 'rpc', method: 'getblockcount', params: [] } }, rid: 'pack:layers:7' });
    expect(wrap('layers', { rid: '8', kind: 'host', method: 'wallet.info', params: { wallet: 'main' }, exact: true })).toMatchObject({ rid: 'pack:layers:8', exact: true, params: { request: { kind: 'host', method: 'wallet.info' } } });
    expect(wrap('layers', { rid: '7', kind: 'cancel' })).toEqual({ kind: 'cancel', rid: 'pack:layers:7' });
    expect(wrap('thunder', { kind: 'rpc', method: 'getbalances', wallet: 'w' })).toEqual({ kind: 'host', method: 'pack.call', params: { stem: 'thunder', request: { kind: 'rpc', method: 'getbalances', params: [], wallet: 'w' } } });
  });

  it('takes a frame\'s messages only from that frame, for its own sidechain; refuses what is not a request', async () => {
    installPackRoute();
    // The page itself offers the frames nothing: no route on the window.
    expect((window as unknown as Record<string, unknown>).__chainsPackRoute).toBeUndefined();
    const replies: unknown[] = [];
    const layers = { postMessage: (m: unknown, origin: string) => replies.push({ m, origin }) } as unknown as Window;
    const stranger = { postMessage: () => replies.push('stranger') } as unknown as Window;
    registerFrame('layers', layers);
    // A frame that names another sidechain, or claims to be another: its own sidechain all the same.
    expect(onFrameMessage({ source: layers, data: { chainsPack: 1, id: 1, stem: 'thunder', request: { rid: '1', kind: 'host', method: 'wallet.info', params: { wallet: 'main' } } } })).toBe(true);
    await waitFor(() => expect(replies.length).toBe(1));
    expect(replies[0]).toEqual({ m: { chainsPackReply: 1, id: 1, ok: true, value: { routed: { stem: 'layers', request: { kind: 'host', method: 'wallet.info', params: { wallet: 'main' } } } } }, origin: 'chains-pack://layers' });
    expect(calls.at(-1)?.rid).toBe('pack:layers:1');
    // A window that is no frame of this page: dropped, not answered.
    expect(onFrameMessage({ source: stranger, data: { chainsPack: 1, id: 2, request: { kind: 'host', method: 'wallet.info' } } })).toBe(false);
    expect(onFrameMessage({ source: layers, data: 'hello' })).toBe(false);
    // A request of the tests, or of no kind: refused, said to the frame.
    onFrameMessage({ source: layers, data: { chainsPack: 1, id: 3, request: { kind: 'testrpc', method: 'stop' } } });
    await waitFor(() => expect(replies.length).toBe(2));
    expect(replies[1]).toMatchObject({ m: { chainsPackReply: 1, id: 3, ok: false, error: { code: -32600 } } });
    await expect(route('../x', { kind: 'host', method: 'a' })).rejects.toMatchObject({ code: -32600 });
    expect(replies).not.toContain('stranger');
    registerFrame('layers', null);
    expect(packUrl('layers')).toBe('chains-pack://layers/packs/layers/index.html');
  });

  it('shows its pages once its node runs and speaks this version, chromeless; read-only with "update needed" else', async () => {
    packList = [{ stem: 'layers', name: 'Layers', hostApi: 1, commit: 'a'.repeat(40) }];
    state.chains[0] = entry('layers', { enabled: true, state: 'running', ready: true });
    await refreshSidechains();
    selectChain('layers');
    const frames = render(ModuleFrames);
    await waitFor(() => expect(document.querySelector('iframe.module-frame')).toBeTruthy());
    const frame = document.querySelector('iframe.module-frame') as HTMLIFrameElement;
    expect(frame.getAttribute('src')).toBe('chains-pack://layers/packs/layers/index.html');
    expect(frame.getAttribute('sandbox')).toBe('allow-scripts allow-same-origin');
    expect(frame.title).toBe('Layers');
    expect(frame.hidden).toBe(false);
    // Its node: the app's own page, the frame kept but hidden.
    openModule('layers', NODE_PAGE);
    await waitFor(() => expect(frame.hidden).toBe(true));
    const view = render(SidechainView);
    await waitFor(() => expect(document.querySelector('[data-field="height"]')).toBeTruthy());
    view.unmount();
    frames.unmount();
    // A node of another version: read-only, update needed, no frame.
    packCompatible = false;
    modules.info = {};
    render(ModuleFrames);
    render(SidechainView);
    await waitFor(() => expect(document.querySelector('[data-reason="daemon"]')).toBeTruthy());
    expect(document.querySelector('iframe.module-frame')).toBeNull();
    selectChain('chains');
  });
});

describe('Settings → Sidechains', () => {
  it('lists the sidechains, each off, with what it takes and where its data goes', async () => {
    render(SidechainSettings);
    await waitFor(() => expect(document.querySelectorAll('.card').length).toBe(3));
    const layers = document.querySelector('.card[data-stem="layers"]') as HTMLElement;
    expect(within(layers).getByText('Layers')).toBeTruthy();
    expect(layers.textContent).toContain('About 4 GB of disk and 400 MB of memory');
    expect(layers.textContent).toContain('/data/sidechains/layers/data');
    expect(layers.querySelector('[data-state="disabled"]')?.textContent).toBe('Off');
    // A program missing: said so.
    expect(document.querySelector('.card[data-stem="thunder"]')?.textContent).toContain('not on this computer yet');
    expect(screen.getByText(/Nothing more runs while every sidechain is off/)).toBeTruthy();
  });

  it('turns a sidechain on through its wizard', async () => {
    render(SidechainSettings);
    await waitFor(() => expect(document.querySelector('.card[data-stem="layers"]')).toBeTruthy());
    const box = document.querySelector('.card[data-stem="layers"] input[type="checkbox"]') as HTMLInputElement;
    await fireEvent.click(box);
    await waitFor(() => expect(document.querySelector('[data-wizard="enable"]')).toBeTruthy());
    expect(box.checked).toBe(false);
    const wizard = () => document.querySelector('[data-wizard="enable"]') as HTMLElement;
    const next = () => document.querySelector('[data-action="next"]') as HTMLButtonElement;
    for (const expected of ['about', 'program', 'storage', 'wallet', 'staking', 'review']) {
      expect(wizard().dataset.step).toBe(expected);
      if (expected !== 'review') await fireEvent.click(next());
    }
    await fireEvent.click(document.querySelector('[data-action="turn-on"]') as HTMLButtonElement);
    await waitFor(() => expect(wizard().dataset.step).toBe('done'));
    expect(calls.some((c) => c.method === 'sidechains.enable' && (c.params as Record<string, unknown>).wallet === 'shared')).toBe(true);
    expect(calls.some((c) => c.method === 'sidechains.setDataDir')).toBe(false);
  });

  it('turns a sidechain off after asking', async () => {
    state.chains[0] = entry('layers', { enabled: true, state: 'running', ready: true });
    render(SidechainSettings);
    await waitFor(() => expect((document.querySelector('.card[data-stem="layers"] input[type="checkbox"]') as HTMLInputElement)?.checked).toBe(true));
    await fireEvent.click(document.querySelector('.card[data-stem="layers"] input[type="checkbox"]') as HTMLInputElement);
    // The question of the page (Turn off?), answered yes by the pretend dialogs.
    const { answer, dialogs } = await import('../lib/dialogs.svelte');
    await waitFor(() => expect(dialogs.queue.length).toBe(1));
    answer(dialogs.queue[0].id, { choice: 'yes' });
    await waitFor(() => expect(calls.some((c) => c.method === 'sidechains.disable')).toBe(true));
  });
});

describe('the chains of the sidebar and the overview', () => {
  it('shows nothing for Chains alone', async () => {
    await refreshSidechains();
    render(SidebarChainGroups);
    await waitFor(() => expect(calls.some((c) => c.method === 'sidechains.list')).toBe(true));
    expect(document.querySelector('[data-chain-group]')).toBeNull();
  });

  it('shows a group per sidechain on, and opens a sidechain read-only', async () => {
    state.chains[0] = entry('layers', { enabled: true, state: 'running', ready: true });
    await refreshSidechains();
    render(SidebarChainGroups);
    prefs.sidebarAllPages = true;
    await waitFor(() => expect(document.querySelectorAll('[data-chain-group]').length).toBe(1));
    await fireEvent.click(document.querySelector('[data-chain-item="layers:own"]') as HTMLElement);
    expect(hub.selected).toBe('layers');
    expect(nav.page).toBe('sidechain');
    render(SidechainView);
    await waitFor(() => expect(document.querySelector('[data-field="height"]')?.textContent).toBe('12'));
    expect(document.querySelector('[data-field="sync"]')?.textContent).toBe('In step with eCash');
    expect(document.querySelector('[data-field="balance"]')?.textContent).toBe('1.50000000');
    expect(document.querySelector('tr[data-height="12"]')).toBeTruthy();
    // Without its pages built in (no pack.info here): its node, read-only.
    expect(screen.getByText(/Its pages open once its node runs|not in this version/)).toBeTruthy();
    // Back to Chains: Home.
    selectChain('chains');
    expect(nav.page).toBe('home');
  });

  it('follows the events of the host, and goes back to Chains when the one shown is turned off', async () => {
    state.chains[0] = entry('layers', { enabled: true, state: 'running', ready: true });
    await refreshSidechains();
    selectChain('layers');
    state.chains[0] = entry('layers');
    await refreshSidechains();
    expect(hub.selected).toBe('chains');
    expect(nav.page).toBe('home');
    // An event of the host: a refresh.
    render(ModuleFrames);
    await waitFor(() => expect(calls.filter((c) => c.method === 'sidechains.list').length).toBeGreaterThan(2));
    const before = calls.filter((c) => c.method === 'sidechains.list').length;
    dispatch([{ type: 'sidechains', data: { stem: 'layers' } }]);
    await waitFor(() => expect(calls.filter((c) => c.method === 'sidechains.list').length).toBe(before + 1), { timeout: 2000 });
  });
});

describe('the wizards render their steps', () => {
  it('the enable wizard of a chain without staking', async () => {
    render(EnableWizard, { entry: entry('thunder', { program: '' }), onclose: () => {} });
    const wizard = () => document.querySelector('[data-wizard="enable"]') as HTMLElement;
    expect(wizard().dataset.step).toBe('about');
    expect(wizard().textContent).toContain('200 GB free: enough room.');
    await fireEvent.click(document.querySelector('[data-action="next"]') as HTMLButtonElement);
    // Its program is not on this computer: Next waits for its download (lib/downloads, tested in modules.test.ts).
    expect(wizard().dataset.step).toBe('program');
    expect((document.querySelector('[data-action="next"]') as HTMLButtonElement).disabled).toBe(true);
  });

  it('the enable wizard: a folder of one\'s own waits for one', async () => {
    render(EnableWizard, { entry: entry('layers'), onclose: () => {} });
    const wizard = () => document.querySelector('[data-wizard="enable"]') as HTMLElement;
    await fireEvent.click(document.querySelector('[data-action="next"]') as HTMLButtonElement);
    expect(wizard().dataset.step).toBe('program');
    await fireEvent.click(document.querySelector('[data-action="next"]') as HTMLButtonElement);
    expect(wizard().dataset.step).toBe('storage');
    await fireEvent.click(screen.getByText('A folder of my choice'));
    expect((document.querySelector('[data-action="next"]') as HTMLButtonElement).disabled).toBe(true);
  });

  it('the first-start guide: with a phrase made already (Setup guide of Settings), straight on; sidechains picked are turned on', async () => {
    hub.list = structuredClone(state);
    // The phrase itself is made in the host's own window (test/seed.test.ts): here it exists, unlocked.
    seed.status = { exists: true, unlocked: true, nodeReady: true, firstRun: false, offer: false, minPassphrase: 8, chains: [] };
    render(FirstRunWizard, { onclose: () => {} });
    const wizard = () => document.querySelector('[data-wizard="firstrun"]') as HTMLElement;
    const next = () => document.querySelector('[data-action="next"]') as HTMLButtonElement;
    expect(wizard().dataset.step).toBe('welcome');
    await fireEvent.click(next());
    await fireEvent.click(next());
    expect(wizard().dataset.step).toBe('backup');
    expect(wizard().textContent).toContain('You have a backup phrase on this computer');
    await fireEvent.click(next());
    expect(wizard().dataset.step).toBe('network');
    expect(wizard().textContent).toContain('regtest');
    await fireEvent.click(next());
    expect(wizard().dataset.step).toBe('sidechains');
    // Thunder's program is not here: it cannot be picked.
    expect((document.querySelector('.pick[data-stem="thunder"] input') as HTMLInputElement).disabled).toBe(true);
    await fireEvent.click(document.querySelector('.pick[data-stem="layers"] input') as HTMLInputElement);
    expect(wizard().textContent).toContain('Together: about 4 GB of disk and 400 MB of memory');
    await fireEvent.click(next());
    expect(wizard().dataset.step).toBe('storage');
    await fireEvent.click(next());
    expect(wizard().dataset.step).toBe('sync');
    await fireEvent.click(document.querySelector('[data-action="finish"]') as HTMLButtonElement);
    await waitFor(() => expect(document.querySelector('.progress li[data-stem="layers"]')?.getAttribute('data-status')).toBe('on'));
    expect(calls.filter((c) => c.method === 'sidechains.enable').map((c) => (c.params as Record<string, unknown>).stem)).toEqual(['layers']);
  });
});

describe('Backbone on eCash: no sidechain to run yet (doc/unified-app.md "Backbone (eCash)")', () => {
  it('offers nothing to turn on: no card, no guide, no command, no sidechain step in the first start', async () => {
    state = { network: 'ecash', rpcServer: false, chainsDataDir: '/data', chains: [] };
    render(SidechainSettings);
    await waitFor(() => expect(document.querySelector('[data-no-sidechains]')).toBeTruthy());
    expect(document.querySelectorAll('.card').length).toBe(0);
    expect(screen.queryByText('Setup guide')).toBeNull();
    expect(sidechainsToRun()).toBe(false);
    expect(buildCommands().some((c) => c.id === 'chains.run' || c.id === 'use.moveGuide')).toBe(false);
    expect(firstRunSteps({ wallet: 'create', picked: [], storage: 'unified' }, false)).not.toContain('sidechains');
    // With the Chains catalog (regtest), they are offered.
    state = listOf(entry('layers'));
    await refreshSidechains();
    expect(sidechainsToRun()).toBe(true);
    expect(buildCommands().some((c) => c.id === 'chains.run')).toBe(true);
  });

  it('before the host has said, nothing is offered on an eCash network', () => {
    hub.list = null;
    app.info = { ...(app.info ?? {}), network: 'ecash' } as typeof app.info;
    expect(sidechainsToRun()).toBe(false);
    app.info = { ...(app.info ?? {}), network: 'regtest' } as typeof app.info;
    expect(sidechainsToRun()).toBe(true);
  });
});
