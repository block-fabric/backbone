// The block explorer's strips (doc/unified-app.md §21.4): a strip of boxes for each chain the selector shows, newest
// first after the block being made; at a new block one box more at the head (the others kept, the strip not read again
// whole); a block clicked opens under the strips (Chains: its facts and transactions; a sidechain: its own explorer at
// it); ← and → go from a block to the next; the table of the newest blocks is gone from the page.
import { fireEvent, render, waitFor } from '@testing-library/svelte';
import { flushSync } from 'svelte';
import { beforeEach, describe, expect, it } from 'vitest';
import { setTransport, type BridgeRequest } from '../lib/bridge';
import { HOST } from '../lib/chains.svelte';
import { hub } from '../lib/sidechains.svelte';
import { app, node, prefs } from '../lib/state.svelte';
import { modules } from '../lib/modules.svelte';
import { mergeStrip, resetStrips, strips, STRIP_LEN, type StripBlock } from '../lib/strips.svelte';
import BlockStrips from '../components/BlockStrips.svelte';
import AllExplorer from '../components/AllExplorer.svelte';
import type { SidechainEntry } from '../lib/unified';

const hashOf = (chain: string, h: number) => `${chain === 'chains' ? 'c' : 'd'}${String(h).padStart(63, '0')}`;
const blk = (height: number, hash = hashOf('chains', height), more: Partial<StripBlock> = {}): StripBlock => ({ height, hash, time: 1000 + height, txs: 1, bmm: [], reward: null, tag: null, ...more });

function entry(stem: string): SidechainEntry {
  return { stem, name: stem === 'layers' ? 'Layers' : stem, description: '', slot: 7, diskGB: 4, ramMB: 400, staking: false, enabled: true, state: 'running', pid: 1, restarts: 0, ready: true, dataDir: '', defaultDataDir: '', customDataDir: false, hasData: true, program: '', rpcPort: 0, port: 0, wallet: 'shared' };
}

let calls: BridgeRequest[];
let tip = 100;
let sideTip = 50;

beforeEach(() => {
  tip = 100;
  sideTip = 50;
  app.phase = 'ready';
  prefs.chain = 'all';
  modules.embed = null;
  hub.list = { network: 'regtest', rpcServer: true, chainsDataDir: '/d', chains: [entry('layers')] };
  node.mempool = { size: 3, usage: 0, max: 0 };
  node.tip = { height: tip, time: 2000, hash: hashOf('chains', tip), progress: 1, state: 'ok', ibd: false, loadingBlocks: false };
  resetStrips();
  calls = [];
  setTransport(async (r) => {
    calls.push(r);
    const p = (r.params ?? {}) as Record<string, unknown>;
    switch (r.method) {
      case 'explorer.blocks': {
        const out = [];
        for (let h = Number(p.from); h > Number(p.from) - Number(p.count) && h >= 0; h--) out.push([h, hashOf('chains', h), 1000 + h, 2, 1, { reward: 5000, bmm: h % 2 ? [7] : [], script: '' }]);
        return out;
      }
      case 'chain.activity': {
        const blocks = [];
        for (let h = sideTip; h > sideTip - Number(p.count) && h >= 0; h--) blocks.push({ height: h, hash: hashOf('layers', h), time: 1000 + h, txs: 1 });
        return { stem: p.stem, height: sideTip, blocks, mempool: { size: 2 } };
      }
      case 'getblock':
        return { hash: (r.params as unknown[])[0], height: tip, time: 2000, mediantime: 1990, confirmations: 1, size: 300, weight: 1200, difficulty: 1, bits: '1d00ffff', merkleroot: 'ab'.repeat(32), version: 1, nonce: 1, tx: ['ee'.repeat(32), 'ff'.repeat(32)] };
      case 'explorer.block':
        return null;
      case 'chain.block':
        return { stem: p.stem, hash: p.hash, height: 50, time: 1050, confirmations: 1, size: 200, weight: 800, nTx: 1, tx: ['aa'.repeat(32)] };
      default:
        return null;
    }
  });
});

describe('a strip of a chain', () => {
  it('new blocks at its head, the same ones kept as they were, cut to its length; a shorter chain that won goes', () => {
    const old = [blk(5), blk(4), blk(3)];
    expect(mergeStrip(old, [blk(5), blk(4)], true)).toBe(old);
    const grown = mergeStrip(old, [blk(6), blk(5)], true);
    expect(grown.map((b) => b.height)).toEqual([6, 5, 4, 3]);
    expect(grown[1]).toBe(old[0]);
    // A reorganisation: block 5 replaced, 6 gone (the tip is 5 now).
    const reorg = mergeStrip(grown, [blk(5, 'x'.repeat(64))], true);
    expect(reorg.map((b) => b.hash.slice(0, 1))).toEqual(['x', 'c', 'c']);
    const long = mergeStrip([], Array.from({ length: STRIP_LEN + 5 }, (_, i) => blk(i)));
    expect(long.length).toBe(STRIP_LEN);
    expect(long[0].height).toBe(STRIP_LEN + 4);
  });
});

describe('the strips of the explorer', () => {
  it('one strip per chain shown, the block being made first; a new block one box more, the others kept', async () => {
    render(BlockStrips, { chains: [HOST, 'layers'] });
    await waitFor(() => expect(document.querySelectorAll('[data-strip="chains"] button[data-strip-block]').length).toBe(STRIP_LEN + 1));
    await waitFor(() => expect(document.querySelectorAll('[data-strip="layers"] button[data-strip-block]').length).toBe(STRIP_LEN + 1));
    const first = document.querySelector('[data-strip="chains"] button[data-strip-block]')!;
    expect(first.getAttribute('data-strip-block')).toBe('next');
    expect(first.textContent).toContain('101');
    expect(first.textContent).toContain('3 waiting');
    // A block of Chains says the sidechains it merged mined (Layers at slot 7) by their mark.
    expect(document.querySelector(`[data-strip-block="${hashOf('chains', 99)}"] [data-chain-badge="layers"]`)).toBeTruthy();
    const kept = document.querySelector(`[data-strip-block="${hashOf('chains', 100)}"]`);
    calls = [];
    tip = 101;
    node.tip = { ...node.tip!, height: 101, hash: hashOf('chains', 101) };
    flushSync();
    await waitFor(() => expect(document.querySelector(`[data-strip-block="${hashOf('chains', 101)}"]`)).toBeTruthy());
    // Only the new block and the head read again, not the strip.
    const read = calls.filter((c) => c.method === 'explorer.blocks');
    expect(read.length).toBe(1);
    expect((read[0].params as { count: number }).count).toBe(2);
    expect(document.querySelector(`[data-strip-block="${hashOf('chains', 100)}"]`)).toBe(kept);
    expect(strips.blocks[HOST].length).toBe(STRIP_LEN);
  });

  it('a block clicked opens under the strips, ← and → go to the next, Escape closes; a sidechain\'s in its own explorer', async () => {
    render(BlockStrips, { chains: [HOST, 'layers'] });
    await waitFor(() => expect(document.querySelector(`[data-strip-block="${hashOf('chains', 100)}"]`)).toBeTruthy());
    await fireEvent.click(document.querySelector(`[data-strip-block="${hashOf('chains', 100)}"]`)!);
    await waitFor(() => expect(document.querySelector('[data-strip-detail]')).toBeTruthy());
    expect(document.querySelector('[data-strip-detail]')!.getAttribute('data-strip-detail')).toBe(`chains:${hashOf('chains', 100)}`);
    await waitFor(() => expect(document.querySelector('[data-chains-block]')).toBeTruthy());
    expect(calls.some((c) => c.method === 'getblock')).toBe(true);
    // → the older one (at its right in the strip), ← back.
    await fireEvent.keyDown(document.querySelector('[data-strip-detail]')!, { key: 'ArrowRight' });
    expect(document.querySelector('[data-strip-detail]')!.getAttribute('data-strip-detail')).toBe(`chains:${hashOf('chains', 99)}`);
    await fireEvent.keyDown(document.querySelector('[data-strip-detail]')!, { key: 'ArrowLeft' });
    expect(document.querySelector('[data-strip-detail]')!.getAttribute('data-strip-detail')).toBe(`chains:${hashOf('chains', 100)}`);
    await fireEvent.keyDown(document.querySelector('[data-strip-detail]')!, { key: 'Escape' });
    expect(document.querySelector('[data-strip-detail]')).toBeNull();
    // A block of Layers: its facts and transactions from its node (chain.block).
    await waitFor(() => expect(document.querySelector(`[data-strip-block="${hashOf('layers', 50)}"]`)).toBeTruthy());
    await fireEvent.click(document.querySelector(`[data-strip-block="${hashOf('layers', 50)}"]`)!);
    await waitFor(() => expect(document.querySelector('[data-strip-detail] [data-side-block="layers:50"]')).toBeTruthy());
    expect(calls.find((c) => c.method === 'chain.block')!.params).toEqual({ stem: 'layers', hash: hashOf('layers', 50) });
  });

  it('the main page of the explorer with All: the strips, no table of the newest blocks', async () => {
    render(AllExplorer);
    await waitFor(() => expect(document.querySelector('[data-strips]')).toBeTruthy());
    expect(document.querySelector('[data-many-blocks]')).toBeNull();
    expect(document.querySelector('[data-all-search]')).toBeTruthy();
  });
});
