// The block explorer's strips (doc/unified-app.md §21.4): each chain's newest blocks as boxes, newest first, and what
// waits to be mined on it. A strip keeps what it read: at a new block only the new ones are read and put at its head,
// the oldest fall off its end; a strip is never read nor made again whole (a box keeps its place, keyed by its hash).
// This app's own chain from its node (explorer.blocks with their summaries: the sidechains each block merged mined),
// a sidechain from its node (chain.activity).
import { request } from './bridge';
import { coinbaseTag } from './blocks';
import { HOST } from './chains.svelte';
import { host } from './rpc';

export interface StripBlock {
  height: number;
  hash: string;
  time: number;
  txs: number;
  /** The sidechains (slots) this block of Chains merged mined; [] for a sidechain's block. */
  bmm: number[];
  /** What its coinbase paid (sats), null when not known (a sidechain's block). */
  reward: number | null;
  /** The miner's tag of its coinbase; null when not known. */
  tag: string | null;
}

/** How many boxes a strip keeps. */
export const STRIP_LEN = 24;
/** Read at a refresh of a sidechain's strip: its newest few (and more when it went further, a catch-up). */
export const STRIP_STEP = 3;

class Strips {
  /** Each chain's boxes, newest first: plain arrays, put in place whole when one changed (never proxied deeply). */
  blocks = $state.raw<Record<string, StripBlock[]>>({});
  /** How many transactions wait to be mined on a sidechain (this app's own: node.mempool). */
  waiting = $state<Record<string, number>>({});
  errors = $state<Record<string, string>>({});
}
export const strips = new Strips();

function keep(stem: string, merged: StripBlock[]): void {
  if (strips.blocks[stem] !== merged) strips.blocks = { ...strips.blocks, [stem]: merged };
}

/**
 * Fresh blocks (any order) put in a strip: a height read again replaces the box of that height (a reorganisation),
 * newer ones go at the head, the strip is cut to `len`. With `tip` (the fresh ones end at the chain's tip), a box above
 * the tip goes (a shorter chain won). The same array back when nothing changed: nothing shown changes.
 */
export function mergeStrip(old: readonly StripBlock[], fresh: readonly StripBlock[], tip = false, len = STRIP_LEN): StripBlock[] {
  if (!fresh.length) return old as StripBlock[];
  const top = Math.max(...fresh.map((b) => b.height));
  const byHeight = new Map<number, StripBlock>();
  for (const b of old) if (!tip || b.height <= top) byHeight.set(b.height, b);
  for (const b of fresh) {
    const was = byHeight.get(b.height);
    // The same block, as it was: kept (the same object, its box untouched).
    if (!was || was.hash !== b.hash || was.txs !== b.txs || was.bmm.join() !== b.bmm.join() || was.tag !== b.tag || was.reward !== b.reward) byHeight.set(b.height, b);
  }
  const out = [...byHeight.values()].sort((a, b) => b.height - a.height).slice(0, len);
  if (out.length === old.length && out.every((b, i) => b === old[i])) return old as StripBlock[];
  return out;
}

type DetailedRow = [number, string, number, number, number, { reward?: number; bmm?: number[]; script?: string } | null | undefined];

/** This app's own chain, at its tip: the blocks above the strip's head read (and the head again: a reorganisation). */
export async function refreshHostStrip(tipHeight: number): Promise<void> {
  const known = strips.blocks[HOST] ?? [];
  const top = known[0]?.height ?? -1;
  const count = top < 0 || tipHeight - top >= STRIP_LEN ? STRIP_LEN : Math.max(1, tipHeight - top) + 1;
  try {
    const rows = (await host.explorerSummaries(Math.min(STRIP_LEN, count), tipHeight)) as unknown as DetailedRow[];
    const fresh = rows.map(([height, hash, time, nTx, , summary]) => ({
      height,
      hash,
      time,
      txs: nTx,
      bmm: summary?.bmm ?? [],
      reward: typeof summary?.reward === 'number' ? summary.reward : null,
      tag: typeof summary?.script === 'string' ? coinbaseTag(summary.script) : null,
    }));
    keep(HOST, mergeStrip(strips.blocks[HOST] ?? [], fresh, true));
    if (strips.errors[HOST]) delete strips.errors[HOST];
  } catch (e) {
    strips.errors[HOST] = e instanceof Error ? e.message : String(e);
  }
}

interface Activity {
  height: number;
  blocks: { height: number; hash: string; time: number; txs: number }[];
  mempool?: { size?: number };
}

/** A sidechain: its newest few, and as many more as it went further since (up to a strip). */
export async function refreshSideStrip(stem: string): Promise<void> {
  const known = strips.blocks[stem] ?? [];
  const ask = (count: number) => request<Activity>('host', 'chain.activity', { stem, count });
  try {
    let a = await ask(known.length ? STRIP_STEP : STRIP_LEN);
    const top = known[0]?.height ?? -1;
    if (known.length && a.height - top >= STRIP_STEP) a = await ask(Math.min(STRIP_LEN, a.height - top + 1));
    const fresh = a.blocks.map((b) => ({ height: b.height, hash: b.hash, time: b.time, txs: b.txs, bmm: [], reward: null, tag: null }));
    keep(stem, mergeStrip(strips.blocks[stem] ?? [], fresh, true));
    const size = a.mempool?.size ?? 0;
    if (strips.waiting[stem] !== size) strips.waiting[stem] = size;
    if (strips.errors[stem]) delete strips.errors[stem];
  } catch (e) {
    strips.errors[stem] = e instanceof Error ? e.message : String(e);
  }
}

/** For the tests: forget every strip. */
export function resetStrips(): void {
  strips.blocks = {};
  strips.waiting = {};
  strips.errors = {};
}
