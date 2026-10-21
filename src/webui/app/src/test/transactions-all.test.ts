// The Transactions page over every chain the top bar shows (doc/unified-app.md §21.4, user 2026-10-21: "a standard view,
// the transactions of all coins selected, a column to know which chain it is from, the same search and features"): the
// app's own table with a Chain column, search, filters, sorting and the export over every chain, a sidechain's row's
// details in the app, what only this app's wallet does on its rows only, and a sidechain's history kept up to date by
// its own latest rows, not read again at a block of Chains.
import { fireEvent, render, screen, waitFor, within } from '@testing-library/svelte';
import { afterEach, beforeAll, beforeEach, describe, expect, it, vi } from 'vitest';
import { setTransport, type BridgeRequest } from '../lib/bridge';
import { nav } from '../lib/nav.svelte';
import { hub } from '../lib/sidechains.svelte';
import { app, node, prefs, wallets } from '../lib/state.svelte';
import { HOST, loadCapabilities, setSelectedChain, chainState } from '../lib/chains.svelte';
import { moduleRoute, modules } from '../lib/modules.svelte';
import { resetWalletData } from '../lib/walletdata.svelte';
import { chainRecords, FULL, LATEST, mergeLatest, resetSideTxs, REFRESH_MS } from '../lib/alltxs.svelte';
import { filterRecords } from '../lib/wallet';
import Transactions from '../pages/Transactions.svelte';
import type { SidechainEntry } from '../lib/unified';

function entry(stem: string, slot: number, over: Partial<SidechainEntry> = {}): SidechainEntry {
  const names: Record<string, string> = { layers: 'Layers', thunder: 'Thunder' };
  return { stem, name: names[stem] ?? stem, description: '', slot, diskGB: 4, ramMB: 400, staking: false, enabled: true, state: 'running', pid: 1, restarts: 0, ready: true, dataDir: '', defaultDataDir: '', customDataDir: false, hasData: true, program: '', rpcPort: 0, port: 0, wallet: 'separate', ...over };
}
const base = { decimals: 8, assets: false, dex: false, staking: false, names: false, markets: false, shielded: false, send: true, receive: true, deposit: true, withdraw: true, embed: [] as string[] };
const CAPS = [
  { ...base, stem: 'layers' },
  { ...base, stem: 'thunder' },
];
const T = 1_700_000_000;
const SIDE: Record<string, unknown[]> = {
  layers: [
    { txid: 'a1'.repeat(32), vout: 0, time: T + 200, category: 'receive', amount: '1.50000000', address: 'rsc1qlayersrent', label: 'Rent', confirmations: 2, abandoned: false },
    { txid: 'a2'.repeat(32), vout: 1, time: T + 100, category: 'send', amount: '-0.50000000', fee: '-0.00001000', address: 'rsc1qshop', label: '', confirmations: 9, abandoned: false },
  ],
  thunder: [{ txid: 'b1'.repeat(32), vout: 0, time: T + 150, category: 'receive', amount: '2.00000000', address: 'tsc1qthunder', label: '', confirmations: 7, abandoned: false }],
};
let calls: BridgeRequest[];

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
  nav.page = 'transactions';
  nav.tab = '';
  prefs.chain = 'all';
  prefs.mask = false;
  prefs.chainWallets = {};
  prefs.unit = 'CHN' as never;
  chainState.caps = {};
  chainState.capsLoaded = false;
  hub.selected = 'chains';
  hub.list = { network: 'regtest', rpcServer: true, chainsDataDir: '/d', chains: [entry('layers', 7), entry('thunder', 2)] };
  modules.routes = {};
  wallets.list = [{ name: 'demo' }] as never;
  wallets.current = 'demo';
  node.tip = { height: 10, time: T, hash: 'h1', progress: 1, state: 'ok', ibd: false, loadingBlocks: false };
  resetWalletData();
  resetSideTxs();
  calls = [];
  setTransport(async (r) => {
    calls.push(r);
    const p = (r.params ?? {}) as Record<string, unknown>;
    switch (r.method) {
      case 'chain.capabilities':
        return structuredClone(CAPS);
      case 'wallet.transactions': {
        const records = [
          ['cc'.repeat(32), 0, 'recv', 'rchn1qalice', 'Alice', T + 300, 250_000_000, 0, 'confirmed', 7, 0, true, 'k3', 4, 'c', 0, 0, 1, ''],
          ['dd'.repeat(32), 0, 'send', 's7_rsc1qdep', '', T + 50, 0, -100_000_000, 'confirmed', 9, 0, true, 'k1', 2, 'c', 0, 0, 1, 'deposit:7'],
        ];
        // Some of them (the part that changed), or the whole history.
        const txids = p.txids as string[] | undefined;
        return { height: 10, maturity: 100, records: txids ? records.filter((r) => txids.includes(String(r[0]))) : records };
      }
      case 'wallet.addresses':
        return [];
      case 'wallet.txActions':
        return { canAbandon: false, canBump: true, hex: '00' };
      case 'chain.transactions':
        return { stem: p.stem, wallet: 'main', rows: structuredClone(SIDE[String(p.stem)] ?? []) };
      case 'chain.transaction':
        return { txid: p.txid, rows: [{ k: 'Status', text: '2 confirmations' }], notes: [], messages: [] };
      default:
        return null;
    }
  });
  await loadCapabilities();
});

afterEach(() => vi.useRealTimers());

const rowsOf = (chain: string) => document.querySelectorAll(`.cellwrap[data-row-chain="${chain}"][data-txid]`);
const headers = () => [...document.querySelectorAll('[role="columnheader"]')].map((h) => h.textContent?.replace(/[▲▼]/g, '').trim());
const rowOf = (txid: string) => document.querySelector(`.cellwrap[data-txid="${txid}"]`)!.closest('.dt-row') as HTMLElement;

describe('the Transactions page over every chain shown', () => {
  it('All: the app\'s own table with a Chain column, every chain\'s rows; one chain: its rows alone, no Chain column', async () => {
    render(Transactions);
    await waitFor(() => expect(rowsOf('layers').length).toBe(2));
    expect(rowsOf(HOST).length).toBe(2);
    expect(rowsOf('thunder').length).toBe(1);
    expect(headers()).toContain('Chain');
    expect(document.querySelector('[data-tx-count]')?.textContent).toContain('5 of 5');
    // Read whole once, from each chain's node through the host.
    const reads = calls.filter((c) => c.method === 'chain.transactions');
    expect(reads.map((c) => (c.params as { stem: string }).stem).sort()).toEqual(['layers', 'thunder']);
    expect((reads[0].params as { count: number }).count).toBe(FULL);
    // A deposit of the wallet said as what it does.
    expect(rowOf('dd'.repeat(32)).textContent).toContain('Deposit into Layers');
    // It fits its width (no sideways scroll; the label cut with an ellipsis), every chain in one coin: the head says it.
    expect(document.querySelector('.table.fit')).toBeTruthy();
    expect(headers()).toContain('Amount (CHN)');
    setSelectedChain('layers');
    await waitFor(() => expect(rowsOf(HOST).length).toBe(0));
    expect(rowsOf('layers').length).toBe(2);
    expect(rowsOf('thunder').length).toBe(0);
    expect(headers()).not.toContain('Chain');
    setSelectedChain(HOST);
    await waitFor(() => expect(rowsOf('layers').length).toBe(0));
    expect(rowsOf(HOST).length).toBe(2);
    expect(headers()).not.toContain('Chain');
  });

  it('one search over every chain: an address, a label, a transaction id or an amount of any of them', async () => {
    render(Transactions);
    await waitFor(() => expect(rowsOf('thunder').length).toBe(1));
    const search = screen.getByLabelText('Search');
    await fireEvent.input(search, { target: { value: 'rent' } });
    await waitFor(() => expect(rowsOf(HOST).length).toBe(0));
    expect(rowsOf('layers').length).toBe(1);
    await fireEvent.input(search, { target: { value: 'b1b1b1' } });
    await waitFor(() => expect(rowsOf('thunder').length).toBe(1));
    expect(rowsOf('layers').length).toBe(0);
    // An amount: "2.5" finds Alice's 2.50000000 on Chains, "1.5" Layers' rent.
    await fireEvent.input(search, { target: { value: '2.5' } });
    await waitFor(() => expect(rowsOf(HOST).length).toBe(1));
    expect(rowsOf('thunder').length).toBe(0);
    await fireEvent.input(search, { target: { value: '1.5' } });
    await waitFor(() => expect(rowsOf('layers').length).toBe(1));
    expect(rowsOf(HOST).length).toBe(0);
  });

  it('the type filter and the sorting over the merged list; the export with a Chain column', async () => {
    const { component } = render(Transactions);
    await waitFor(() => expect(rowsOf('thunder').length).toBe(1));
    await fireEvent.change(screen.getByLabelText('Type'), { target: { value: 'sent' } });
    await waitFor(() => expect(rowsOf('thunder').length).toBe(0));
    // Sent on Chains (the deposit) and on Layers; the fee of a send counted once, in its amount.
    expect([...rowsOf(HOST)].map((e) => e.getAttribute('data-txid'))).toEqual(['dd'.repeat(32)]);
    expect(rowOf('a2'.repeat(32)).textContent).toContain('-0.50001000');
    await fireEvent.change(screen.getByLabelText('Type'), { target: { value: 'all' } });
    await waitFor(() => expect(rowsOf('thunder').length).toBe(1));
    // Sorted by amount, the largest first: Alice's 2.5 (Chains), Thunder's 2, Layers' 1.5 ...
    const amountHeader = [...document.querySelectorAll('[role="columnheader"] button')].find((b) => b.textContent?.startsWith('Amount')) as HTMLElement;
    await fireEvent.click(amountHeader);
    await fireEvent.click(amountHeader);
    await waitFor(() => {
      const order = [...document.querySelectorAll('.dt-row .cellwrap[data-txid]')].map((e) => e.getAttribute('data-txid')!.slice(0, 2));
      expect(order).toEqual(['cc', 'b1', 'a1', 'a2', 'dd']);
    });
    const csv = (component as unknown as { csvText: () => string }).csvText();
    const lines = csv.trim().split('\n');
    expect(lines[0]).toBe('"Chain","Confirmed","Date","Type","Label","Address","Amount (CHN)","ID"');
    expect(lines.length).toBe(6);
    expect(lines.find((l) => l.includes('a1'.repeat(32)))).toMatch(/^"Layers",/);
    expect(lines.find((l) => l.includes('cc'.repeat(32)))).toMatch(/^"Chains",/);
    expect(lines.find((l) => l.includes('dd'.repeat(32)))).toContain('"Deposit into Layers"');
  });

  it('a sidechain\'s row: its details in the app, read from its node; only what its own pages do, not this app\'s wallet\'s', async () => {
    render(Transactions);
    await waitFor(() => expect(rowsOf('layers').length).toBe(2));
    await fireEvent.dblClick(rowOf('a1'.repeat(32)));
    await waitFor(() => expect(calls.some((c) => c.method === 'chain.transaction')).toBe(true));
    expect(calls.find((c) => c.method === 'chain.transaction')!.params).toMatchObject({ stem: 'layers', txid: 'a1'.repeat(32) });
    await waitFor(() => expect(document.querySelector('[data-tx-chain="layers"]')).toBeTruthy());
    await fireEvent.click(screen.getByText('Open in Layers'));
    expect(moduleRoute('layers').page).toBe('transactions');
    // Its menu: copies, details, the explorer, its own pages; no fee bump, abandoning or address book of this wallet.
    await fireEvent.contextMenu(rowOf('a2'.repeat(32)));
    await waitFor(() => expect(screen.getByRole('menu')).toBeTruthy());
    const items = within(screen.getByRole('menu')).getAllByRole('menuitem').map((b) => b.textContent?.trim());
    expect(items).toContain('Copy transaction ID');
    expect(items).toContain('Open in Layers');
    expect(items).not.toContain('Increase transaction fee');
    expect(items).not.toContain('Abandon transaction');
    expect(items).not.toContain('Edit address label');
    expect(calls.some((c) => c.method === 'wallet.txActions')).toBe(false);
    // This app's own row keeps them.
    await fireEvent.keyDown(screen.getByRole('menu'), { key: 'Escape' });
    await fireEvent.contextMenu(rowOf('cc'.repeat(32)));
    await waitFor(() => expect(calls.some((c) => c.method === 'wallet.txActions')).toBe(true));
    await waitFor(() => expect(within(screen.getByRole('menu')).getAllByRole('menuitem').map((b) => b.textContent?.trim())).toContain('Increase transaction fee'));
  });

  it('a block of Chains does not read the sidechains again; their latest rows, every few seconds, do', async () => {
    vi.useFakeTimers({ shouldAdvanceTime: true });
    render(Transactions);
    await waitFor(() => expect(rowsOf('layers').length).toBe(2));
    const reads = () => calls.filter((c) => c.method === 'chain.transactions').length;
    const before = reads();
    node.tip = { height: 11, time: T + 60, hash: 'h2', progress: 1, state: 'ok', ibd: false, loadingBlocks: false };
    wallets.txVersion++;
    await vi.advanceTimersByTimeAsync(1000);
    expect(reads()).toBe(before);
    // A new payment on Layers: its latest rows read, the row added, the others kept as they were.
    const kept = rowOf('a2'.repeat(32));
    SIDE.layers = [{ txid: 'a3'.repeat(32), vout: 0, time: T + 400, category: 'receive', amount: '3.00000000', address: 'rsc1qnew', label: '', confirmations: 0, abandoned: false }, ...SIDE.layers];
    try {
      await vi.advanceTimersByTimeAsync(REFRESH_MS);
      await waitFor(() => expect(rowsOf('layers').length).toBe(3));
      const latest = calls.filter((c) => c.method === 'chain.transactions' && (c.params as { stem: string }).stem === 'layers').at(-1)!;
      expect((latest.params as { count: number }).count).toBe(LATEST);
      expect(rowOf('a2'.repeat(32))).toBe(kept);
    } finally {
      SIDE.layers = SIDE.layers.slice(1);
    }
  });

  it('privacy mode hides the history of every chain', async () => {
    prefs.mask = true;
    render(Transactions);
    await waitFor(() => screen.getByText('The history is hidden while values are masked.'));
    expect(rowsOf('layers').length).toBe(0);
    prefs.mask = false;
  });
});

describe('a sidechain\'s history as records of the app\'s', () => {
  const rows = SIDE.layers as Parameters<typeof chainRecords>[1];

  it('each row a record: its type, its status, its fee once, its chain in its key', () => {
    const r = chainRecords('layers', [
      ...rows,
      { txid: 'a2'.repeat(32), vout: 2, time: 100, category: 'send', amount: '-0.25000000', fee: '-0.00001000', address: 'x', label: '', confirmations: 9, abandoned: false },
      { txid: 'a4'.repeat(32), vout: 0, time: 90, category: 'immature', amount: '0.10000000', address: 'y', label: '', confirmations: 30, abandoned: false },
      { txid: 'zz', vout: 0, time: 90, category: 'receive', amount: '1', address: 'z', label: '', confirmations: 1, abandoned: false },
    ]);
    expect(r.map((x) => x.key)).toEqual([`layers:${'a1'.repeat(32)}:0:receive`, `layers:${'a2'.repeat(32)}:1:send`, `layers:${'a2'.repeat(32)}:2:send`, `layers:${'a4'.repeat(32)}:0:immature`]);
    expect(r.map((x) => x.type)).toEqual(['recv', 'send', 'send', 'mined']);
    expect(r.map((x) => x.status)).toEqual(['confirming', 'confirmed', 'confirmed', 'immature']);
    expect(r[1].amount).toBe(-50_001_000n);
    expect(r[2].amount).toBe(-25_000_000n);
    expect(r[3].maturesIn).toBe(70);
    expect(r.every((x) => x.chain === 'layers')).toBe(true);
    // Searched as the app's own records are, with their amounts.
    expect(filterRecords(r, { type: 'all', search: '0.25', minAmount: 0n, showInactive: true }).map((x) => x.idx)).toEqual([2]);
  });

  it('the latest rows merged into those held: the same objects when unchanged, read whole when they no longer meet', () => {
    const held = chainRecords('layers', rows);
    expect(mergeLatest(held, chainRecords('layers', rows))).toBe(held);
    const confirmed = chainRecords('layers', [{ ...(rows[0] as object), confirmations: 3 } as (typeof rows)[number]]);
    const merged = mergeLatest(held, confirmed)!;
    expect(merged[0]).toBe(confirmed[0]);
    expect(merged[1]).toBe(held[1]);
    const many = chainRecords(
      'layers',
      Array.from({ length: LATEST }, (_, i) => ({ txid: (i + 16).toString(16).padStart(2, '0').repeat(32), vout: 0, time: 1000 + i, category: 'receive', amount: '1.00000000', address: 'q', label: '', confirmations: 1, abandoned: false })),
    );
    expect(mergeLatest(held, many)).toBeNull();
  });
});
