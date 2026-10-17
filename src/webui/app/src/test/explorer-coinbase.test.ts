// The block explorer of Chains: the coinbase and drivechain columns of the list (explorer.blocks with details) and
// the Coinbase and Drivechain activity sections of a block (explorer.block), from what the node decodes.
import { fireEvent, render, screen, waitFor, within } from '@testing-library/svelte';
import { afterEach, beforeEach, describe, expect, it } from 'vitest';
import Explorer from '../pages/Explorer.svelte';
import { setTransport, type BridgeRequest } from '../lib/bridge';
import { heightRuns, outputKindText, shortAddress, summaryText, voteEntryText, voteText, type BlockDetail, type BlockSummary } from '../lib/explorer';
import { nav } from '../lib/nav.svelte';
import { app, node, prefs } from '../lib/state.svelte';

const SUMMARY: BlockSummary = { size: 250, weight: 1000, subsidy: 5_000_000_000, reward: 5_000_025_000, fees: 25_000, payout: 'rchn1qminerpayoutaddressxxxxxxxxxxxxxxxxx', payouts: 1, bmm: [1, 2], votes: 1, voteForm: 'one', acks: 1, proposals: 0, bundles: 0, escrow: 1 };

describe('the words of the explorer', () => {
  it('batches heights into runs of consecutive blocks, 100 at most each', () => {
    expect(heightRuns([30, 29, 28, 10, 9, 31])).toEqual([
      { from: 31, count: 4 },
      { from: 10, count: 2 },
    ]);
    expect(heightRuns(Array.from({ length: 250 }, (_, i) => i))).toEqual([
      { from: 249, count: 100 },
      { from: 149, count: 100 },
      { from: 49, count: 50 },
    ]);
    expect(heightRuns([])).toEqual([]);
  });

  it('says what a coinbase did in a few words', () => {
    expect(summaryText(SUMMARY)).toBe('2 BMM · 1 vote · 1 ack · 1 escrow change');
    expect(summaryText({ ...SUMMARY, bmm: [], votes: 0, voteForm: '', acks: 0, escrow: 0 })).toBe('');
    expect(summaryText({ ...SUMMARY, bmm: [], voteForm: 'repeat', acks: 0, escrow: 0, proposals: 2, bundles: 1 })).toBe('votes repeated · 2 proposals · 1 bundle');
    expect(voteText('one', 0)).toBe('abstained');
    expect(voteText('leading', 0)).toBe('upvotes leaders');
    expect(voteText('', 3)).toBe('');
  });

  it('names the outputs and the votes', () => {
    expect(outputKindText({ kind: 'bmm' })).toBe('M7 BMM accept');
    expect(outputKindText({ kind: 'vote' })).toBe('M4 bundle votes');
    expect(outputKindText({ kind: 'payout' })).toBe('Miner payout');
    expect(voteEntryText({ vote: 'upvote', index: 0 })).toBe('Upvote bundle #0');
    expect(voteEntryText({ vote: 'downvote' })).toBe('Downvote all');
    expect(shortAddress('rchn1qminerpayoutaddressxxxxxxxxxxxxxxxxx')).toBe('rchn1qmi…xxxxxx');
    expect(shortAddress('short')).toBe('short');
  });
});

const hashOf = (h: number) => h.toString(16).padStart(64, '0');
const DETAIL: BlockDetail = {
  hash: hashOf(30),
  height: 30,
  active: true,
  subsidy: 5_000_000_000,
  reward: 5_000_025_000,
  fees: 25_000,
  unclaimed: 0,
  coinbase: 'cb'.repeat(32),
  script: '011e' + Buffer.from('/chains pool/').toString('hex'),
  minScore: 30,
  outputs: [
    { n: 0, value: 5_000_025_000, type: 'witness_v0_keyhash', kind: 'payout', address: 'rchn1qminerpayoutaddressxxxxxxxxxxxxxxxxx' },
    { n: 1, value: 0, type: 'nulldata', kind: 'witness', hash: 'aa'.repeat(32) },
    { n: 2, value: 0, type: 'nulldata', kind: 'bmm', slot: 1, title: 'Thunder', hash: 'b1'.repeat(32) },
    { n: 3, value: 0, type: 'nulldata', kind: 'vote', form: 'one', count: 2 },
    { n: 4, value: 0, type: 'nulldata', kind: 'ack', slot: 3, title: 'BitNames', hash: 'a3'.repeat(32) },
  ],
  outputCount: 5,
  bmm: [{ slot: 1, title: 'Thunder', hash: 'b1'.repeat(32), request: 'e1'.repeat(32), bid: 25_000 }],
  proposals: [],
  acks: [{ slot: 3, title: 'BitNames', hash: 'a3'.repeat(32) }],
  activations: [],
  votes: {
    form: 'one',
    known: true,
    entries: [
      { slot: 1, title: 'Thunder', vote: 'upvote', index: 0, bundle: 'bd'.repeat(32) },
      { slot: 2, title: 'Hivemind', vote: 'abstain' },
    ],
  },
  events: true,
  proposed: [],
  closed: [],
  pending: [{ slot: 1, title: 'Thunder', count: 1, bundles: [{ hash: 'bd'.repeat(32), score: 12 }] }],
  escrow: [{ slot: 1, title: 'Thunder', kind: 'deposit', txid: 'de'.repeat(32), amount: 200_000_000, total: 1_200_000_000, destination: 's1_erin_b91ea9' }],
  escrowCount: 1,
};

describe('the explorer of Chains', () => {
  let calls: BridgeRequest[] = [];
  beforeEach(() => {
    app.info = { name: 'Chains', version: 'test', bugreport: '', network: 'regtest', networkLabel: 'regtest', datadir: '/tmp/x', walletEnabled: true, devtools: false, test: true, platform: 'linux', blockSpacing: 600 };
    prefs.unit = 'CHN';
    prefs.mask = false;
    prefs.uiMode = 'advanced';
    prefs.explorerCount = 25;
    prefs.explorerPageSize = 10;
    nav.tab = '';
    node.tip = { height: 30, time: 1_700_000_000, hash: hashOf(30), progress: 1, state: 'synced', ibd: false, loadingBlocks: false };
    calls = [];
    setTransport(async (r) => {
      calls.push(r);
      switch (r.method) {
        case 'explorer.blocks': {
          const { count, from, details } = r.params as { count: number; from?: number; details?: boolean };
          if (count < 1 || count > (details ? 100 : 10000)) throw { code: -32600, message: 'count' };
          const top = Math.min(from ?? 30, 30);
          return Array.from({ length: Math.max(0, Math.min(count, top + 1)) }, (_, i) => top - i).map((h) => {
            const row: unknown[] = [h, hashOf(h), 1_700_000_000 + h * 60, 2, 1];
            if (details) row.push(h === 30 ? SUMMARY : { ...SUMMARY, bmm: [], votes: 0, voteForm: '', acks: 0, escrow: 0, fees: 0, reward: SUMMARY.subsidy });
            return row;
          });
        }
        case 'explorer.block':
          return DETAIL;
        case 'getblock':
          return { height: 30, time: 1_700_000_000, nTx: 2, size: 250, weight: 1000, difficulty: 1, hash: hashOf(30), tx: ['cb'.repeat(32)], merkleroot: '00', confirmations: 1, bits: '207fffff', nonce: 1, version: 1, mediantime: 1_700_000_000, chainwork: '00' };
        case 'getrawmempool':
          return {};
        case 'getblockstats':
          return { subsidy: 50, totalfee: 0, avgfeerate: 0 };
        default:
          throw { code: -32601, message: `No ${r.method}` };
      }
    });
  });
  afterEach(() => setTransport(null));

  it('lists the reward, the fees, the BMM and the coinbase of the blocks shown, read in one call', async () => {
    render(Explorer);
    const table = await waitFor(() => screen.getByRole('grid', { name: 'Blocks' }));
    await waitFor(() => expect(table.textContent).toContain('2 BMM · 1 vote · 1 ack · 1 escrow change'));
    for (const label of ['Reward', 'Fees', 'BMM', 'Coinbase']) expect(within(table).getByText(label)).toBeTruthy();
    expect(table.textContent).toContain('50.00025');
    expect(table.textContent).toContain('rchn1qmi…xxxxxx');
    // The rows of the page and of the ribbon with their summaries: in calls of runs of heights, each block asked
    // once (never a getblock per row).
    const detailed = calls.filter((c) => c.method === 'explorer.blocks' && (c.params as { details?: boolean }).details).map((c) => c.params as { count: number; from: number });
    const asked = detailed.flatMap(({ count, from }) => Array.from({ length: count }, (_, i) => from - i));
    expect(new Set(asked).size).toBe(asked.length);
    expect(asked).toContain(21);
    expect(detailed.length).toBeLessThanOrEqual(2);
    expect(calls.some((c) => c.method === 'getblock')).toBe(false);
  });

  it('hides the extra columns in Simple mode', async () => {
    prefs.uiMode = 'simple';
    render(Explorer);
    const table = await waitFor(() => screen.getByRole('grid', { name: 'Blocks' }));
    expect(within(table).getByText('Reward')).toBeTruthy();
    expect(within(table).queryByText('Coinbase')).toBeNull();
    expect(within(table).queryByText('BMM')).toBeNull();
    prefs.uiMode = 'advanced';
  });

  it('shows the coinbase of a block and what it did to the sidechains', async () => {
    render(Explorer);
    const table = await waitFor(() => screen.getByRole('grid', { name: 'Blocks' }));
    const row = await waitFor(() => {
      const found = within(table).getAllByRole('row').find((r) => r.textContent?.includes(hashOf(30)));
      if (!found) throw new Error('the row of block 30');
      return found;
    });
    await fireEvent.dblClick(row);
    const coinbase = await waitFor(() => screen.getByRole('region', { name: 'Coinbase' }));
    expect(coinbase.textContent).toContain('Block subsidy');
    expect(coinbase.textContent).toContain('/chains pool/');
    // The outputs split in two: the payouts on one side, the messages and data on the other.
    const payouts = within(coinbase).getByRole('table', { name: 'Payouts' });
    const messages = within(coinbase).getByRole('table', { name: 'Messages and data' });
    expect(payouts.textContent).toContain('Miner payout');
    expect(messages.textContent).not.toContain('Miner payout');
    for (const kind of ['Witness commitment', 'M7 BMM accept', 'M4 bundle votes', 'M2 proposal ack']) {
      expect(messages.textContent).toContain(kind);
      expect(payouts.textContent).not.toContain(kind);
    }
    const activity = screen.getByRole('region', { name: 'Drivechain activity' });
    const bmm = within(activity).getByRole('table', { name: 'BMM accepts' });
    expect(bmm.textContent).toContain('Thunder');
    expect(bmm.textContent).toContain('0.00025');
    const votes = within(activity).getByRole('table', { name: 'Bundle votes and bundles' });
    expect(votes.textContent).toContain('Upvote bundle #0');
    expect(votes.textContent).toContain('Hivemind');
    expect(votes.textContent).toContain('12 / 30');
    expect(within(activity).getByRole('table', { name: 'Escrow changes' }).textContent).toContain('s1_erin_b91ea9');
    expect(within(activity).getByRole('table', { name: 'Sidechain proposals and acks' }).textContent).toContain('BitNames');
    // The block's host call names the block.
    expect(calls.find((c) => c.method === 'explorer.block')?.params).toEqual({ hash: hashOf(30) });
  });
});
