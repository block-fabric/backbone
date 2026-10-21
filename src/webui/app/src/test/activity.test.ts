// The recent activity of many chains in words (doc/unified-app.md §20.3): mining rewards folded per chain, rewards of no
// value left out, what each row is, and the age of a block (the genesis said as such, not "15 years ago").
import { describe, expect, it } from 'vitest';
import { foldActivity, isBid, isEmptyRow, isPegIn, isReward, whatHappened, whatWords } from '../lib/activity';
import { parseRecords } from '../lib/wallet';
import { ageText } from '../lib/format';
import type { ManyChainTx } from '../lib/chains.svelte';

const HOST = 'chains';
let n = 0;
function tx(over: Partial<ManyChainTx>): ManyChainTx {
  n++;
  return { chain: HOST, name: 'Chains', txid: n.toString(16).padStart(64, '0'), time: 1000 + n, category: 'receive', amount: '1.00000000', address: 'addr' + n, label: '', confirmations: 3, vout: 0, ...over };
}

describe('activity', () => {
  it('says what each row is', () => {
    expect(whatHappened(tx({ category: 'mined', amount: '25.00000000' }), HOST)).toBe('Mining reward');
    expect(whatHappened(tx({ category: 'sendOther', amount: '-0.00100000' }), HOST)).toBe('Payment sent');
    expect(whatHappened(tx({ category: 'recv' }), HOST)).toBe('Payment received');
    const credit = tx({ chain: 'layers', category: 'generate', vout: 1 });
    expect(isPegIn(credit, HOST)).toBe(true);
    expect(whatHappened(credit, HOST)).toBe('Coins in from %1');
    expect(isReward(tx({ chain: 'layers', category: 'generate', vout: 0 }), HOST)).toBe(true);
  });

  it('folds the rewards of each chain into one row and leaves out those of no value', () => {
    const rows = [
      tx({ category: 'mined', amount: '25.00000000', time: 10 }),
      tx({ category: 'mined', amount: '25.00100000', time: 20 }),
      tx({ category: 'send', amount: '-1.50000000', time: 15, label: 'rent' }),
      tx({ chain: 'hivemind', name: 'Hivemind', category: 'generate', amount: '0.00000000', time: 30 }),
      tx({ chain: 'hivemind', name: 'Hivemind', category: 'generate', amount: '0.00000000', time: 31 }),
    ];
    expect(isEmptyRow(rows[3], HOST)).toBe(true);
    expect(isEmptyRow(rows[0], HOST)).toBe(false);
    const items = foldActivity(rows, 10, HOST);
    expect(items.map((i) => i.kind)).toEqual(['rewards', 'tx']);
    const r = items[0];
    expect(r.kind === 'rewards' && r.count).toBe(2);
    expect(r.kind === 'rewards' && r.total).toBe('50.00100000');
    const p = items[1];
    expect(p.kind === 'tx' && p.label).toBe('rent');
  });

  it('folds the merged mining bids of the wallet and names its deposits', () => {
    const bid = (time: number) => tx({ category: 'sendOther', amount: '-0.00100000', address: '', time, drivechain: { kind: 'bmm', slot: 1 } });
    const deposit = tx({ category: 'other', amount: '-2.00010000', address: '', time: 50, drivechain: { kind: 'deposit', slot: 3 } });
    expect(isBid(bid(1), HOST)).toBe(true);
    // The destination output (no value) of a first deposit: its other row says it.
    expect(isEmptyRow(tx({ category: 'sendOther', amount: '0.00000000', drivechain: { kind: 'deposit', slot: 3 } }), HOST)).toBe(true);
    expect(isEmptyRow(deposit, HOST)).toBe(false);
    expect(isBid(tx({ chain: 'layers', drivechain: { kind: 'bmm', slot: 1 } }), HOST)).toBe(false);
    expect(whatHappened(deposit, HOST)).toBe('Deposit into %1');
    expect(whatWords('Deposit into %1', 'Chains', 'Layers')).toBe('Deposit into Layers');
    expect(whatWords('Deposit into %1', 'Chains', '')).toBe('Deposit into a sidechain');
    const items = foldActivity([bid(40), bid(41), bid(42), deposit], 10, HOST);
    expect(items).toHaveLength(2);
    const bids = items.find((i) => i.kind === 'rewards');
    expect(bids?.kind === 'rewards' && bids.what).toBe('bids');
    expect(bids?.kind === 'rewards' && bids.count).toBe(3);
    expect(bids?.kind === 'rewards' && bids.total).toBe('-0.00300000');
    const dep = items.find((i) => i.kind === 'tx');
    expect(dep?.kind === 'tx' && dep.side).toBe(3);
  });

  it('reads what a record does on the drivechain as the host says it', () => {
    const base = ['a'.repeat(64), 0, 'sendOther', '', '', 100, 0, -100000, 'confirmed', 1, 0, true, '0000', 10, 'c', false, false, true];
    expect(parseRecords([[...base, 'bmm:7']])[0].drivechain).toEqual({ kind: 'bmm', slot: 7 });
    expect(parseRecords([[...base, 'deposit:12']])[0].drivechain).toEqual({ kind: 'deposit', slot: 12 });
    expect(parseRecords([[...base, '']])[0].drivechain).toBeNull();
    expect(parseRecords([[...base, 'bmm:x']])[0].drivechain).toBeNull();
    expect(parseRecords([base])[0].drivechain).toBeNull();
  });

  it('keeps to the count asked, the rewards row counting once', () => {
    const rows = Array.from({ length: 8 }, (_, i) => tx({ category: i % 2 ? 'mined' : 'receive', amount: '1.00000000', time: 100 - i }));
    const items = foldActivity(rows, 3, HOST);
    expect(items).toHaveLength(3);
    expect(items.filter((i) => i.kind === 'rewards')).toHaveLength(1);
  });

  it('says the age of a block, the genesis as such', () => {
    expect(ageText(1, 500_000_000, 0)).toBe('genesis');
    expect(ageText(1000, 1005)).toBe('just now');
    expect(ageText(1000, 1000 + 3600)).toMatch(/ago$/);
    expect(ageText(1000, 1000 + 5 * 86400)).not.toMatch(/ago$/);
  });
});
