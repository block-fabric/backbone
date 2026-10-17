// The links of the block explorer: blocks, transactions and addresses open their views, Back returns along them,
// bundles and proposals open the Sidechains page, a sidechain's block hash is no link; the addresses of the open wallet
// are marked "yours" (asked of the wallet in one call for those shown, kept); the address view shows what the wallet
// knows of its own addresses and, of any other, only what the address itself says (no scan of the chain).
import { fireEvent, render, screen, waitFor, within } from '@testing-library/svelte';
import { afterEach, beforeEach, describe, expect, it } from 'vitest';
import Explorer from '../pages/Explorer.svelte';
import { setTransport, type BridgeRequest } from '../lib/bridge';
import { mayBeAddress, scriptKindText, type AddressActivity, type BlockDetail } from '../lib/explorer';
import { nav } from '../lib/nav.svelte';
import { ownerOf, resetOwned } from '../lib/owned.svelte';
import { app, node, prefs, wallets } from '../lib/state.svelte';

const hashOf = (h: number) => h.toString(16).padStart(64, '0');
const PAYOUT = 'rchn1qminerpayoutaddressxxxxxxxxxxxxxxxxx';
const OTHER = 'rchn1qsomeoneelsesaddressyyyyyyyyyyyyyyyyy';
const CB = 'cb'.repeat(32);
const PREV = 'ab'.repeat(32);
const SPENDER = 'ef'.repeat(32);
const BUNDLE = 'bd'.repeat(32);
const DETAIL: BlockDetail = {
  hash: hashOf(30),
  height: 30,
  active: true,
  subsidy: 5_000_000_000,
  reward: 5_000_000_000,
  fees: 0,
  unclaimed: 0,
  coinbase: CB,
  script: '011e',
  minScore: 30,
  outputs: [
    { n: 0, value: 5_000_000_000, type: 'witness_v0_keyhash', kind: 'payout', address: PAYOUT },
    { n: 1, value: 0, type: 'nulldata', kind: 'bmm', slot: 1, title: 'Thunder', hash: 'b1'.repeat(32) },
  ],
  outputCount: 2,
  bmm: [{ slot: 1, title: 'Thunder', hash: 'b1'.repeat(32), request: 'e1'.repeat(32), bid: 25_000 }],
  proposals: [],
  acks: [{ slot: 3, title: 'BitNames', hash: 'a3'.repeat(32) }],
  activations: [],
  votes: { form: 'one', known: true, entries: [{ slot: 1, title: 'Thunder', vote: 'upvote', index: 0, bundle: BUNDLE }] },
  events: true,
  proposed: [],
  closed: [],
  pending: [{ slot: 1, title: 'Thunder', count: 1, bundles: [{ hash: BUNDLE, score: 12 }] }],
  escrow: [{ slot: 1, title: 'Thunder', kind: 'deposit', txid: 'de'.repeat(32), amount: 1, total: 1, destination: OTHER, mainchain: true }],
  escrowCount: 1,
};
const ACTIVITY: AddressActivity = {
  mine: true,
  label: 'Pool payouts',
  received: 10_000_000_000,
  spent: 2_500_000_000,
  balance: 7_500_000_000,
  immature: 0,
  txCount: 2,
  coinCount: 1,
  history: [
    [SPENDER, 31, 1_700_000_100, 0, 2_500_000_000],
    [CB, 30, 1_700_000_000, 5_000_000_000, 0],
  ],
  coins: [[CB, 0, 5_000_000_000, 30, false]],
};

describe('the links of the explorer', () => {
  let calls: BridgeRequest[] = [];
  beforeEach(() => {
    app.info = { name: 'Chains', version: 'test', bugreport: '', network: 'regtest', networkLabel: 'regtest', datadir: '/tmp/x', walletEnabled: true, devtools: false, test: true, platform: 'linux', blockSpacing: 600 };
    prefs.unit = 'CHN';
    prefs.mask = false;
    prefs.uiMode = 'advanced';
    prefs.explorerCount = 25;
    prefs.explorerPageSize = 10;
    nav.page = 'explorer';
    nav.tab = '';
    nav.focus = null;
    wallets.current = 'w';
    resetOwned();
    node.tip = { height: 31, time: 1_700_000_000, hash: hashOf(31), progress: 1, state: 'synced', ibd: false, loadingBlocks: false };
    calls = [];
    setTransport(async (r) => {
      calls.push(r);
      const p = r.params as Record<string, unknown> & unknown[];
      switch (r.method) {
        case 'explorer.blocks': {
          const { count, from } = r.params as { count: number; from?: number };
          const top = Math.min(from ?? 31, 31);
          return Array.from({ length: Math.max(0, Math.min(count, top + 1)) }, (_, i) => top - i).map((h) => [h, hashOf(h), 1_700_000_000 + h * 60, 1, 1]);
        }
        case 'explorer.block':
          return DETAIL;
        case 'getblock':
          return { height: 30, time: 1_700_000_000, nTx: 1, size: 250, weight: 1000, difficulty: 1, hash: hashOf(30), tx: [CB], merkleroot: '00', confirmations: 2, bits: '207fffff', nonce: 1, version: 1, mediantime: 1_700_000_000, chainwork: '00', previousblockhash: hashOf(29) };
        case 'getblockhash':
          return hashOf(p[0] as number);
        case 'getrawtransaction':
          return {
            txid: p[0],
            hash: p[0],
            size: 200,
            vsize: 150,
            weight: 600,
            locktime: 0,
            version: 2,
            vin: [{ txid: PREV, vout: 1, sequence: 0, prevout: { height: 12, value: 1, scriptPubKey: { type: 'witness_v0_keyhash', address: OTHER } } }],
            vout: [{ value: 50, n: 0, scriptPubKey: { type: 'witness_v0_keyhash', address: PAYOUT, asm: '', hex: '' } }],
            blockhash: p[2] ?? hashOf(30),
            confirmations: 2,
            fee: 0.0001,
          };
        case 'gettxspendingprevout':
          return [{ txid: CB, vout: 0, spendingtxid: SPENDER }];
        case 'validateaddress':
          return String(p[0]).startsWith('rchn1') ? { isvalid: true, address: p[0], scriptPubKey: '0014' + '00'.repeat(20), iswitness: true, witness_version: 0, isscript: false } : { isvalid: false };
        case 'wallet.owned':
          return ((r.params as { addresses: string[] }).addresses).filter((a) => a === PAYOUT).map((a) => [a, 'Pool payouts']);
        case 'wallet.address':
          return (r.params as { address: string }).address === PAYOUT ? ACTIVITY : { mine: false };
        case 'getrawmempool':
          return {};
        case 'getblockstats':
          return { subsidy: 50, totalfee: 0, avgfeerate: 0 };
        default:
          throw { code: -32601, message: `No ${r.method}` };
      }
    });
  });
  afterEach(() => {
    setTransport(null);
    wallets.current = null;
    resetOwned();
  });

  async function openBlock30() {
    render(Explorer);
    const table = await waitFor(() => screen.getByRole('grid', { name: 'Blocks' }));
    // The hash of a row is a link to its block.
    const link = await waitFor(() => within(table).getByRole('button', { name: hashOf(30) }));
    await fireEvent.click(link);
    return waitFor(() => screen.getByRole('region', { name: 'Coinbase' }));
  }

  it('marks the payout of the wallet "yours" and opens the address, then Back returns to the block', async () => {
    const coinbase = await openBlock30();
    const payouts = within(coinbase).getByRole('table', { name: 'Payouts' });
    await waitFor(() => expect(payouts.querySelector('[data-ours]')).not.toBeNull());
    expect(payouts.textContent).toContain('yours');
    // The addresses shown asked in one call (the payout, the deposit's destination), never one each.
    const owned = calls.filter((c) => c.method === 'wallet.owned');
    expect(owned.length).toBe(1);
    expect((owned[0].params as { addresses: string[] }).addresses.sort()).toEqual([OTHER, PAYOUT].sort());
    await fireEvent.click(within(payouts).getByRole('button', { name: PAYOUT }));
    const view = await waitFor(() => screen.getByRole('region', { name: 'Address' }));
    await waitFor(() => expect(view.textContent).toContain('Pool payouts'));
    expect(view.textContent).toContain('SegWit (P2WPKH)');
    expect(view.textContent).toContain('75.00');
    const history = within(view).getByRole('table', { name: 'Transactions of the address' });
    expect(history.textContent).toContain('-25.00');
    expect(history.textContent).toContain('+50.00');
    expect(within(view).getByRole('table', { name: 'Coins of the address' }).textContent).toContain('50.00');
    expect(calls.find((c) => c.method === 'wallet.address')?.params).toMatchObject({ wallet: 'w', address: PAYOUT, skip: 0, count: 10 });
    // Back: the block again.
    await fireEvent.click(within(view).getByRole('button', { name: /Back/ }));
    await waitFor(() => screen.getByRole('region', { name: 'Coinbase' }));
    expect(screen.queryByRole('region', { name: 'Address' })).toBeNull();
  });

  it('goes block → transaction → address → transaction, with the coins spent and their spenders linked', async () => {
    const coinbase = await openBlock30();
    await fireEvent.click(within(coinbase).getByRole('button', { name: /^cbcbcbcb/ }));
    const tx = await waitFor(() => screen.getByRole('region', { name: 'Transaction' }));
    // Read with the coins it spends (verbosity 2), its block named.
    expect(calls.find((c) => c.method === 'getrawtransaction')?.params).toEqual([CB, 2, hashOf(30)]);
    await waitFor(() => expect(tx.textContent).toContain('spent by'));
    expect(within(tx).getAllByRole("button").some((b) => b.textContent?.includes(hashOf(30)))).toBe(true);
    // The input's coin: its transaction read at its block's height (no -txindex needed); its address linked.
    expect(within(tx).getByRole('button', { name: OTHER })).toBeTruthy();
    await fireEvent.click(within(tx).getByRole('button', { name: OTHER }));
    const view = await waitFor(() => screen.getByRole('region', { name: 'Address' }));
    await waitFor(() => expect(view.textContent).toContain('Not an address of this wallet'));
    expect(within(view).queryByRole('table')).toBeNull();
    await fireEvent.click(within(view).getByRole('button', { name: /Back/ }));
    const again = await waitFor(() => screen.getByRole('region', { name: 'Transaction' }));
    calls = [];
    await fireEvent.click(within(again).getByRole('button', { name: `${PREV}:1` }));
    await waitFor(() => expect(calls.some((c) => c.method === 'getrawtransaction' && (c.params as unknown[])[0] === PREV)).toBe(true));
    expect(calls.find((c) => c.method === 'getblockhash')?.params).toEqual([12]);
    expect(calls.find((c) => c.method === 'getrawtransaction')?.params).toEqual([PREV, 2, hashOf(12)]);
  });

  it('opens bundles and proposals on the Sidechains page; a sidechain block is no link', async () => {
    await openBlock30();
    const activity = screen.getByRole('region', { name: 'Drivechain activity' });
    const bmm = within(activity).getByRole('table', { name: 'BMM accepts' });
    const side = bmm.querySelector('.hash[title*="Thunder"]');
    expect(side?.tagName).toBe('SPAN');
    expect(side?.getAttribute('title')).toBe('A block of Thunder: see it in its own wallet');
    const votes = within(activity).getByRole('table', { name: 'Bundle votes and bundles' });
    await fireEvent.click(within(votes).getByRole('button', { name: /^bdbdbdbd/ }));
    expect(nav.page).toBe('sidechains');
    expect(nav.tab).toBe('withdrawals');
    expect(nav.focus).toEqual({ page: 'sidechains', key: `1:${BUNDLE}` });
    nav.page = 'explorer';
    const acks = within(activity).getByRole('table', { name: 'Sidechain proposals and acks' });
    await fireEvent.click(within(acks).getByRole('button', { name: /^a3a3a3a3/ }));
    expect(nav.tab).toBe('proposals');
    expect(nav.focus).toEqual({ page: 'sidechains', key: `3:${'a3'.repeat(32)}` });
    // A deposit to an address of this chain: linked.
    expect(within(within(activity).getByRole('table', { name: 'Escrow changes' })).getByRole('button', { name: OTHER })).toBeTruthy();
  });

  it('searches an address; Simple mode shows its balance and transactions only', async () => {
    prefs.uiMode = 'simple';
    render(Explorer);
    const box = screen.getByRole('searchbox', { name: 'Search' });
    await fireEvent.input(box, { target: { value: ` ${PAYOUT} ` } });
    await fireEvent.submit(box.closest('form')!);
    const view = await waitFor(() => screen.getByRole('region', { name: 'Address' }));
    await waitFor(() => expect(within(view).getByRole('table', { name: 'Transactions of the address' })).toBeTruthy());
    expect(within(view).queryByRole('table', { name: 'Coins of the address' })).toBeNull();
    expect(view.textContent).toContain('Holds now');
    expect(view.textContent).not.toContain('Script');
    expect(calls.find((c) => c.method === 'validateaddress')?.params).toEqual([PAYOUT]);
    prefs.uiMode = 'advanced';
  });

  it('asks nothing without a wallet, and the kinds of addresses in words', async () => {
    wallets.current = null;
    expect(ownerOf(PAYOUT)).toBeNull();
    await Promise.resolve();
    await Promise.resolve();
    expect(calls.some((c) => c.method === 'wallet.owned')).toBe(false);
    expect(scriptKindText({ iswitness: true, witness_version: 1 })).toBe('Taproot (P2TR)');
    expect(scriptKindText({ iswitness: true, witness_version: 0, scriptPubKey: '0020' + '00'.repeat(32) })).toBe('SegWit script (P2WSH)');
    expect(scriptKindText({ isscript: true })).toBe('Script (P2SH)');
    expect(scriptKindText({})).toBe('Legacy (P2PKH)');
    expect(mayBeAddress(PAYOUT)).toBe(true);
    expect(mayBeAddress('not an address')).toBe(false);
  });

  it('keeps the answers: the same addresses are not asked again', async () => {
    expect(ownerOf(PAYOUT)).toBeNull();
    expect(ownerOf(OTHER)).toBeNull();
    await waitFor(() => expect(ownerOf(PAYOUT)).toEqual({ label: 'Pool payouts' }));
    expect(ownerOf(OTHER)).toBeNull();
    expect(calls.filter((c) => c.method === 'wallet.owned').length).toBe(1);
    // Another wallet: asked anew.
    wallets.current = 'other';
    expect(ownerOf(PAYOUT)).toBeNull();
    await waitFor(() => expect(calls.filter((c) => c.method === 'wallet.owned').length).toBe(2));
  });
});
