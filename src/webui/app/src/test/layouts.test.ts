import { fireEvent, render, screen, waitFor, within } from '@testing-library/svelte';
import { afterEach, beforeAll, beforeEach, describe, expect, it } from 'vitest';
import { setTransport, type BridgeRequest } from '../lib/bridge';
import { buildCommands, MENU_GROUPS, menuSections, palette, searchCommands, setLayout } from '../lib/commands.svelte';
import { closeDetail, detail } from '../lib/detail.svelte';
import { handleShortcut } from '../lib/keyboard';
import { AREAS, chainBeforeTools, DEFAULT_LAYOUT, HOME_QUICK_ACTIONS, LAYOUTS, NAV_SECTIONS, pageKind } from '../lib/layout';
import { go, nav, PAGES, type PageId } from '../lib/nav.svelte';
import { app, applyPrefs, node, prefs, wallets } from '../lib/state.svelte';
import { resetWalletData } from '../lib/walletdata.svelte';
import Shell from '../views/Shell.svelte';

/** The menu bar of chains-qt as views/Shell.svelte had it before the layouts: every item must stay reachable. */
const OLD_MENUS: Record<string, string[]> = {
  'Your wallet': [
    'Create wallet…',
    'Open wallet…',
    'Close wallet…',
    'Close all wallets…',
    'Migrate wallet…',
    'Restore and migrate wallet file…',
    'Sending addresses',
    'Receiving addresses',
    'Backup wallet…',
    'Restore wallet…',
    'Export watch-only wallet…',
    'Encrypt wallet…',
    'Change passphrase…',
    'Unlock wallet for a while…',
    'Mask values',
  ],
  Banking: ['Send', 'Receive', 'Transactions', 'Open URI…', 'Proof of funds', 'Multisig lounge', 'Load PSBT from file…', 'Load PSBT from clipboard…'],
  'Use Chains': ['Timestamp file', 'Sign message…', 'Verify message…', 'Sidechains'],
  Chains: ['Sidechain nodes', 'Solo mine', 'Sidechain proposals', 'Withdrawal votes'],
  'Crypto tools': ['Block explorer', 'Hash calculator', 'Merkle tree', 'Address decoder'],
  Window: ['Minimize', 'Information', 'Console', 'Network traffic', 'Peers'],
  'This node': [
    'Node window',
    'Options…',
    'Theme: Same as the desktop',
    'Theme: Light',
    'Theme: Dark',
    'Theme: Ocean Teal',
    'Theme: Matrix Green',
    'Theme: Dark (classic Drivechain)',
    'Theme: Custom…',
    'Increase font size',
    'Decrease font size',
    'Command-line options',
    'About Chains',
    'Exit',
  ],
};

const calls: BridgeRequest[] = [];

/** The node as the pages and cards ask it. */
async function fakeNode(r: BridgeRequest): Promise<unknown> {
  calls.push(r);
  switch (r.method) {
    case 'listbanned':
      return [];
    case 'getpeerinfo':
      return [{ id: 7, addr: '10.0.0.7:8333', inbound: false, pingtime: 0.05, synced_blocks: 10, network: 'ipv4', connection_type: 'outbound-full-relay', subver: '/Chains:32/', version: 70016, startingheight: 1, synced_headers: 10, bytessent: 1, bytesrecv: 2, conntime: 1, lastsend: 1, lastrecv: 1 }];
    case 'getblockheader':
      return { height: 10, time: 1_700_000_000, nTx: 1 };
    case 'getmininginfo':
      return { blocks: 10, difficulty: 1, networkhashps: 12345, pooledtx: 0, chain: 'regtest', next: { height: 11, bits: '207fffff', difficulty: 1, target: '7f'.padEnd(64, 'f') } };
    case 'getdrivechaininfo':
      return { activesidechains: 1, proposals: 0, pendingbundles: 0, escrowtotal: 2.5, withdrawalperiod: 100, maxsidechains: 256, withdrawalminscore: 13150 };
    case 'listsidechainproposals':
      return { pending: [], queued: [] };
    case 'listsidechaindeposits':
      return [];
    case 'listactivesidechains':
      return [{ slot: 2, title: 'Thunder', description: 'Fast payments', version: 0, hashid1: '00', hashid2: '00', activationheight: 5, escrow: { txid: 'aa', n: 0, amount: 2.5 }, pendingbundles: 0 }];
    case 'listwithdrawalbundles':
    case 'getsidechainevents':
    case 'wallet.addresses':
      return [];
    case 'getblock':
      return { hash: r.params && (r.params as unknown[])[0], height: 10, time: 1_700_000_000, confirmations: 1, nTx: 1, size: 200, weight: 800, difficulty: 1, tx: ['cc'.repeat(32)] };
    case 'wallet.info':
      return { name: 'w', encrypted: false, locked: false, privateKeysDisabled: false, externalSigner: false, hd: true, canGetAddresses: true, taproot: true, defaultAddressType: 'bech32', confirmTarget: 6, requiredFeePerK: 1000, maxTxFee: 10_000_000, multiwallet: false, balance: 1, unconfirmed: 0, immature: 0 };
    case 'getbalances':
      return { mine: { trusted: 1, untrusted_pending: 0, immature: 0 } };
    case 'wallet.transactions':
      return { height: 10, hash: 'h', records: [['aa'.repeat(32), 0, 'recv', 'bcrt1qexample', 'salary', 1_700_000_000, 250_000_000, 0, 'confirmed', 7, 0, true, 'k1']] };
    case 'wallet.transaction':
      return { txid: 'aa'.repeat(32), coinbase: false, rows: [{ k: 'Status', text: '7 confirmations' }], notes: [], ids: [{ k: 'Transaction ID', text: 'aa'.repeat(32) }], messages: [] };
    default:
      return null;
  }
}

beforeAll(() => {
  const proto = HTMLDialogElement.prototype as unknown as Record<string, unknown>;
  proto.showModal ??= function (this: HTMLDialogElement) {
    this.setAttribute('open', '');
  };
  proto.close ??= function (this: HTMLDialogElement) {
    this.removeAttribute('open');
  };
  window.matchMedia ??= ((query: string) => ({ matches: false, media: query, addEventListener() {}, removeEventListener() {} })) as unknown as typeof window.matchMedia;
  Element.prototype.scrollIntoView ??= function () {};
});

beforeEach(() => {
  app.info = { name: 'Chains', version: 'test', bugreport: '', network: 'regtest', networkLabel: 'regtest', datadir: '/tmp', walletEnabled: true, devtools: false, test: false, platform: 'linux', blockSpacing: 600 };
  prefs.mask = false;
  prefs.loaded = true;
  node.tip = { height: 10, time: 1_700_000_000, hash: 'ab'.repeat(32), progress: 1, state: 'synced', ibd: false, loadingBlocks: false };
  wallets.list = [{ name: 'w', encrypted: false, locked: false, privateKeysDisabled: false, hd: true }];
  wallets.current = 'w';
  wallets.balances.w = { trusted: 100_000_000n, untrustedPending: 0n, immature: 0n };
  calls.length = 0;
  setTransport(fakeNode);
  go('overview');
});

afterEach(() => {
  setTransport(null);
  palette.open = false;
  closeDetail();
  wallets.list = [];
  wallets.current = null;
  resetWalletData();
  prefs.layout = 'sidebar';
});

describe('the commands', () => {
  it('keep every item of the old menu bar, in its menu', () => {
    // Every item of the old menus is still in a menu (the menus were regrouped: chain before tools).
    const all = buildCommands().filter((c) => (MENU_GROUPS as readonly string[]).includes(c.group));
    for (const [menu, labels] of Object.entries(OLD_MENUS)) {
      for (const label of labels) expect(all.map((c) => c.label), `${menu} → ${label}`).toContain(label);
    }
    const groups = [...MENU_GROUPS] as string[];
    expect(groups.indexOf('Chains')).toBeLessThan(groups.indexOf('Tools'));
    const chains = all.filter((c) => c.group === 'Chains').map((c) => c.label);
    for (const label of ['Sidechains', 'Sidechain nodes', 'Solo mine', 'Block explorer']) expect(chains).toContain(label);
  });

  it('reach every page, and every page is in each layout', () => {
    const all = buildCommands();
    const pages = (Object.keys(PAGES) as PageId[]).filter((p) => p !== 'kit' && p !== 'home' && !PAGES[p].tabOf);
    for (const p of pages) {
      expect(all.some((c) => c.id === `page.${p}`), p).toBe(true);
      expect(AREAS.some((a) => a.tabs.some((t) => t.page === p)), `workspace: ${p}`).toBe(true);
      expect(NAV_SECTIONS.some((s) => s.pages.includes(p)), `sidebars: ${p}`).toBe(true);
    }
    expect(LAYOUTS.map((l) => l.id)).toEqual(['workspace', 'dashboard', 'sidebar']);
  });

  it('sidechain nodes: a tab of the Sidechains page, not a page of its own', () => {
    expect(PAGES['sidechain-nodes'].tabOf).toEqual({ page: 'sidechains', tab: 'nodes' });
    expect(NAV_SECTIONS.some((s) => s.pages.includes('sidechain-nodes'))).toBe(false);
    expect(AREAS.some((a) => a.tabs.some((t) => t.page === 'sidechain-nodes'))).toBe(false);
    // Its command, the old way to it and the dashboard's link all lead to the tab.
    const all = buildCommands();
    expect(all.some((c) => c.id === 'page.sidechain-nodes')).toBe(false);
    all.find((c) => c.label === 'Sidechain nodes')!.run();
    expect([nav.page, nav.tab]).toEqual(['sidechains', 'nodes']);
    go('overview');
    go('sidechain-nodes');
    expect([nav.page, nav.tab]).toEqual(['sidechains', 'nodes']);
  });

  it('dashboard: the wallet is switched in the sidebar card only, and the unit in the status bar only', () => {
    prefs.layout = 'dashboard';
    render(Shell);
    const side = screen.getByRole('navigation', { name: 'Pages' });
    const card = within(side).getByRole('region', { name: 'Wallet shown' });
    // No wallet choice in the top bar beside the search.
    const top = document.querySelector('.content-head, header.top, .dash-head') ?? document.body;
    const outside = Array.from(document.querySelectorAll('select')).filter((el) => !card.contains(el) && el.querySelector('option') && /wallet/i.test(el.getAttribute('title') ?? '') && !el.closest('.statusbar, footer'));
    expect(outside.length, top.className).toBe(0);
    // One unit chooser: in the status bar.
    expect(within(card).queryByRole('combobox', { name: 'Unit of amounts' })).toBeNull();
  });

  it('are found by name, words in any order, and run', () => {
    const all = buildCommands();
    expect(searchCommands(all, 'sign message')[0].label).toBe('Sign message…');
    expect(searchCommands(all, 'sign').map((c) => c.label)).toContain('Sign message…');
    expect(searchCommands(all, 'clipboard psbt')[0].id).toBe('banking.psbtClipboard');
    expect(searchCommands(all, 'peers')[0].label).toBe('Peers');
    expect(searchCommands(all, 'zzzz')).toEqual([]);
    searchCommands(all, 'merkle')[0].run();
    expect([nav.page, nav.tab]).toEqual(['crypto', 'merkle']);
  });

  it('switch the layout, with the home of the dashboard', () => {
    go('overview');
    setLayout('dashboard');
    expect(prefs.layout).toBe('dashboard');
    expect(nav.page).toBe('home');
    setLayout('workspace');
    expect(nav.page).toBe('overview');
    expect(buildCommands().find((c) => c.id === 'layout.workspace')?.checked).toBe(true);
  });

  it('keep the layout in the preferences', () => {
    applyPrefs({ layout: 'dashboard' });
    expect(prefs.layout).toBe('dashboard');
    applyPrefs({ layout: 'nonsense' });
    expect(prefs.layout).toBe('dashboard');
  });

  it('open the palette with Ctrl+K, and the node tabs with their keys', () => {
    expect(handleShortcut(new KeyboardEvent('keydown', { key: 'k', ctrlKey: true }))).toBe(true);
    expect(palette.open).toBe(true);
    handleShortcut(new KeyboardEvent('keydown', { key: 'p', ctrlKey: true }));
    expect([nav.page, nav.tab]).toEqual(['node', 'peers']);
  });
});

describe('the layouts', () => {
  it('workspace: areas, sub-tabs, the more menu, a transaction in the detail panel', async () => {
    prefs.layout = 'workspace';
    render(Shell);
    const areas = screen.getByRole('navigation', { name: 'Areas' });
    expect(within(areas).getAllByRole('button').map((b) => b.textContent?.trim())).toEqual(['Wallet', 'Chain', 'Sidechains', 'Tools', 'Node']);
    const subtabs = screen.getByRole('navigation', { name: 'Wallet pages' });
    expect(within(subtabs).getByRole('button', { name: 'Overview' }).getAttribute('aria-current')).toBe('page');
    await fireEvent.click(within(areas).getByRole('button', { name: 'Node' }));
    expect(nav.page).toBe('node');
    expect(screen.getByRole('navigation', { name: 'Node pages' })).toBeTruthy();
    // The menus of chains-qt, in one menu.
    await fireEvent.click(screen.getByRole('button', { name: /^Menus of chains-qt/ }));
    const menu = await waitFor(() => screen.getByRole('menu'));
    expect(within(menu).getByRole('menuitem', { name: 'Hash calculator' })).toBeTruthy();
    await fireEvent.click(within(menu).getByRole('menuitem', { name: 'Hash calculator' }));
    expect([nav.page, nav.tab]).toEqual(['crypto', 'hash']);
    // The history: a click on a row shows it in the panel.
    go('transactions');
    const cell = await waitFor(() => screen.getByText('salary'));
    await fireEvent.click(cell.closest('[role="row"]')!);
    await waitFor(() => expect(detail.item?.kind).toBe('tx'));
    const panel = await waitFor(() => screen.getByRole('complementary', { name: 'Transaction details' }));
    await waitFor(() => expect(panel.textContent).toContain('7 confirmations'));
  });

  it('dashboard: live cards that open their pages and the panel', async () => {
    prefs.layout = 'dashboard';
    go('home');
    render(Shell);
    // The summary row, the chart, merged mining.
    for (const title of ['Balance', 'Network', 'Sidechains', 'Mining', 'Wallet balance', 'Blind merged mining']) {
      expect(screen.getByRole('heading', { name: title })).toBeTruthy();
    }
    // The tables in tabs: the transactions first, with the actions of each row.
    const tabs = screen.getByRole('tablist', { name: 'Activity' });
    expect(within(tabs).getAllByRole('tab').map((t) => t.textContent?.replace(/\d+/g, '').trim())).toEqual(['Transactions', 'Sidechains', 'Blocks', 'Peers']);
    const txRow = (await waitFor(() => screen.getByRole('button', { name: /salary/ }))).closest('tr')!;
    await fireEvent.click(within(txRow).getByRole('button', { name: 'Copy transaction ID' }));
    await waitFor(() => expect(calls.some((c) => c.method === 'clipboard.write')).toBe(true));
    await fireEvent.click(within(txRow).getByRole('button', { name: 'Details' }));
    await waitFor(() => expect(detail.item?.kind).toBe('tx'));
    await fireEvent.click(within(tabs).getByRole('tab', { name: /Sidechains/ }));
    const sidechain = await waitFor(() => screen.getByRole('button', { name: /Thunder/ }));
    await fireEvent.click(sidechain);
    await waitFor(() => screen.getByRole('complementary', { name: 'Sidechain details' }));
    await fireEvent.click(within(tabs).getByRole('tab', { name: /Peers/ }));
    const peer = await waitFor(() => screen.getByRole('button', { name: /10\.0\.0\.7/ }));
    await fireEvent.click(peer);
    await waitFor(() => expect(detail.item).toEqual({ kind: 'peer', id: 7 }));
    await fireEvent.click(screen.getByRole('button', { name: 'Open mining' }));
    expect(nav.page).toBe('mine');
    // Every page in the sidebar, Home first.
    const side = screen.getByRole('navigation', { name: 'Pages' });
    expect(within(side).getAllByRole('button', { name: /Home|Overview|Wallets|Crypto tools|Settings/ }).length).toBeGreaterThanOrEqual(5);
  }, 20000);

  it('dashboard: the Deposit and Withdrawals of a sidechain open the real tabs, the sidechain chosen', async () => {
    prefs.layout = 'dashboard';
    go('home');
    render(Shell);
    for (const [action, tab] of [['Deposit', 'sidechains'], ['Withdrawals', 'withdrawals']] as const) {
      go('home');
      const tabs = await waitFor(() => screen.getByRole('tablist', { name: 'Activity' }));
      await fireEvent.click(within(tabs).getByRole('tab', { name: /Sidechains/ }));
      const row = (await waitFor(() => screen.getByRole('button', { name: /Thunder/ }))).closest('tr')!;
      await fireEvent.click(within(row).getByRole('button', { name: action }));
      expect(nav.page).toBe('sidechains');
      expect(nav.tab).toBe(tab);
      // The Sidechains page took the sidechain it was opened for.
      await waitFor(() => expect(nav.focus).toBeNull());
      await waitFor(() => screen.getByRole('tab', { name: tab === 'sidechains' ? 'Sidechains' : 'Withdrawals', selected: true }));
    }
  }, 20000);

  it('workspace: a peer chosen in the node window shows in the detail panel, and back', async () => {
    prefs.layout = 'workspace';
    go('node', 'peers');
    render(Shell);
    const peer = await waitFor(() => screen.getByText('10.0.0.7:8333'));
    await fireEvent.click(peer.closest('[role="row"]')!);
    await waitFor(() => expect(detail.item).toEqual({ kind: 'peer', id: 7 }));
    // From the panel, the peers tab with that peer chosen.
    await fireEvent.click(await waitFor(() => screen.getByRole('button', { name: 'Open the peers tab' })));
    expect(nav.page).toBe('node');
  }, 20000);

  it('dashboard: Send and Receive in the side panel, the full pages a click away', async () => {
    prefs.layout = 'dashboard';
    go('home');
    render(Shell);
    const balance = screen.getByRole('heading', { name: 'Balance' }).closest('section')!;
    await fireEvent.click(within(balance).getByRole('button', { name: 'Send' }));
    const panel = await waitFor(() => screen.getByRole('complementary', { name: 'Send' }));
    expect(within(panel).getByLabelText('Pay to')).toBeTruthy();
    await waitFor(() => expect(within(panel).getByRole('button', { name: /^Send/ })).toBeTruthy());
    await fireEvent.click(within(panel).getByRole('tab', { name: /Receive/ }));
    const receive = await waitFor(() => screen.getByRole('complementary', { name: 'Receive' }));
    expect(within(receive).getByRole('button', { name: /Create address/ })).toBeTruthy();
    await fireEvent.click(within(receive).getByRole('button', { name: /the Receive page/ }));
    expect(nav.page).toBe('receive');
    expect(screen.queryByRole('complementary', { name: 'Receive' })).toBeNull();
  });

  it('every layout: the notifications and the light/dark switch in the top bar', async () => {
    for (const layout of ['dashboard', 'workspace', 'sidebar'] as const) {
      prefs.layout = layout;
      prefs.theme = 'midnight';
      wallets.newTxs = [];
      const view = render(Shell);
      const toggle = screen.getByRole('button', { name: 'Switch to the light look' });
      await fireEvent.click(toggle);
      expect(prefs.theme).toBe('light');
      await fireEvent.click(screen.getByRole('button', { name: 'Switch to the dark look' }));
      expect(prefs.theme).toBe('midnight');
      expect(screen.getByRole('button', { name: 'Notifications' })).toBeTruthy();
      wallets.newTxs.push({ wallet: 'w', txid: 'aa'.repeat(32) });
      const bell = await waitFor(() => screen.getByRole('button', { name: 'Notifications: 1 new' }));
      await fireEvent.click(bell);
      expect(screen.getByRole('dialog', { name: 'Notifications' })).toBeTruthy();
      view.unmount();
      wallets.newTxs = [];
    }
  }, 30000);

  it('the sidebar: a card of the wallet, Close wallet and Quit at its foot', () => {
    prefs.layout = 'dashboard';
    render(Shell);
    const side = screen.getByRole('navigation', { name: 'Pages' });
    const card = within(side).getByRole('region', { name: 'Wallet shown' });
    expect(within(card).getByText('Not encrypted')).toBeTruthy();
    // The unit is chosen in the status bar only, not in the card.
    expect(within(card).queryByRole('combobox', { name: 'Unit of amounts' })).toBeNull();
    expect(within(side).getByRole('button', { name: /Close wallet/ })).toBeTruthy();
    expect(within(side).getByRole('button', { name: /Quit Chains/ })).toBeTruthy();
  });

  it('compact: an icon rail named for screen readers, opened on demand', async () => {
    prefs.layout = 'sidebar';
    prefs.sidebarExpanded = false;
    render(Shell);
    const rail = screen.getByRole('navigation', { name: 'Pages' });
    const send = within(rail).getByRole('button', { name: 'Send' });
    expect(send.textContent?.trim()).toBe('');
    // The toggle at the foot of the rail (the logo does the same: see below).
    const toggles = within(rail).getAllByRole('button', { name: 'Show the names of the pages' });
    expect(toggles.length).toBe(2);
    await fireEvent.click(toggles[toggles.length - 1]);
    expect(prefs.sidebarExpanded).toBe(true);
    expect(within(rail).getByRole('button', { name: 'Send' }).textContent).toContain('Send');
    expect(screen.queryByRole('menubar')).toBeNull();
  });

  it('compact: the logo grows the sidebar, and shrinks it again', async () => {
    prefs.layout = 'sidebar';
    prefs.sidebarExpanded = false;
    render(Shell);
    const rail = screen.getByRole('navigation', { name: 'Pages' });
    const logo = rail.querySelector('button.brand') as HTMLButtonElement;
    expect(logo).not.toBeNull();
    expect(logo.getAttribute('aria-expanded')).toBe('false');
    await fireEvent.click(logo);
    expect(prefs.sidebarExpanded).toBe(true);
    expect(within(rail).getByRole('button', { name: 'Send' }).textContent).toContain('Send');
    // The page did not change: the logo of the rail is the sidebar's switch, not a link.
    const page = nav.page;
    await fireEvent.click(rail.querySelector('button.brand') as HTMLButtonElement);
    expect(prefs.sidebarExpanded).toBe(false);
    expect(nav.page).toBe(page);
  });

  it('chain before tools, in the sidebars and the workspace', () => {
    const titles = NAV_SECTIONS.map((s) => s.title);
    expect(titles.indexOf('Chain')).toBeLessThan(titles.indexOf('Tools'));
    const areas = AREAS.map((a) => a.id);
    expect(areas.indexOf('chain')).toBeLessThan(areas.indexOf('tools'));
    expect(areas.indexOf('sidechains')).toBeLessThan(areas.indexOf('tools'));
  });

  it('chain before tools, in every list of actions and every menu', () => {
    // The helper finds a list out of order.
    expect(chainBeforeTools(['send', 'psbt', 'mine'] as PageId[], (p) => p)).toEqual({ chain: 'mine', tool: 'psbt' });
    expect(chainBeforeTools(['send', 'mine', 'psbt', 'node'] as PageId[], (p) => p)).toBeNull();
    expect(pageKind('explorer')).toBe('chain');
    expect(pageKind('messages')).toBe('tools');
    const lists: [string, PageId[]][] = [
      ['Home quick actions', HOME_QUICK_ACTIONS.map((q) => q.page)],
      ['sidebar sections', NAV_SECTIONS.flatMap((s) => s.pages)],
      ['workspace areas', AREAS.flatMap((a) => a.tabs.map((tb) => tb.page))],
    ];
    const all = buildCommands();
    lists.push(['Go to', all.filter((c) => c.group === 'Go to').map((c) => c.page!)]);
    for (const section of menuSections(all)) lists.push([`menu ${section.title}`, section.items.flatMap((c) => (c.page ? [c.page] : []))]);
    // The menus themselves: the one of the chain before the one of the tools.
    expect(MENU_GROUPS.indexOf('Chains')).toBeLessThan(MENU_GROUPS.indexOf('Tools'));
    for (const [name, pages] of lists) {
      expect(pages.length, name).toBeGreaterThan(0);
      expect(chainBeforeTools(pages, (p) => p), name).toBeNull();
    }
    // Home: Send and Receive first, then the chain, then the tools.
    expect(HOME_QUICK_ACTIONS.map((q) => q.label)).toEqual(['Send', 'Receive', 'Deposit to a sidechain', 'Block explorer', 'Mine', 'Sign message', 'Load PSBT']);
  });

  it('dashboard: the quick actions of Home in that order, as shown', () => {
    prefs.layout = 'dashboard';
    go('home');
    render(Shell);
    const row = screen.getByRole('navigation', { name: 'Quick actions' });
    const labels = within(row).getAllByRole('button').map((b) => b.textContent?.trim() ?? '');
    expect(labels.slice(0, 7)).toEqual(['Send', 'Receive', 'Deposit to a sidechain', 'Block explorer', 'Mine', 'Sign message', 'Load PSBT']);
    expect(chainBeforeTools(labels.slice(0, 7), (l) => HOME_QUICK_ACTIONS.find((q) => q.label === l)?.page)).toBeNull();
  });

  it('the dashboard is the default layout', () => {
    expect(DEFAULT_LAYOUT).toBe('dashboard');
  });

  it('palette: type, Enter runs the first match', async () => {
    render(Shell);
    palette.open = true;
    const input = await waitFor(() => screen.getByRole('combobox', { name: 'Search pages and actions' }));
    await fireEvent.input(input, { target: { value: 'verify' } });
    expect(within(screen.getByRole('listbox')).getAllByRole('option')[0].textContent).toContain('Verify message');
    await fireEvent.keyDown(input, { key: 'Enter' });
    expect(palette.open).toBe(false);
    expect([nav.page, nav.tab]).toEqual(['messages', 'verify']);
  });
});
