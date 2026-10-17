// What the block explorer of Chains shows of a block's coinbase and of what the block did to the sidechains:
// the summaries of the list (host explorer.blocks with details) and the detail of one block (host explorer.block),
// both decoded by the node (src/webui/explorer.cpp). Apart from the page so that it can be tested on its own.
import { t, tn } from './i18n.svelte';

/** Amounts are in sats. */
export interface BlockSummary {
  size: number;
  weight: number;
  subsidy: number;
  /** What the coinbase pays: subsidy + fees claimed. */
  reward: number;
  /** reward - subsidy. */
  fees: number;
  coinbase?: string;
  /** The scriptSig of the coinbase, hex (the miner's tag). */
  script?: string;
  /** The address the coinbase pays most ('' for a script without one). */
  payout: string;
  payouts: number;
  /** Slots of the BMM accepts (M7). */
  bmm: number[];
  /** Sidechains the vote message (M4, one or two byte form) upvotes or downvotes. */
  votes: number;
  voteForm: VoteForm;
  acks: number;
  proposals: number;
  bundles: number;
  /** Transactions that change an escrow (deposits, withdrawals). */
  escrow: number;
}
export type VoteForm = '' | 'one' | 'two' | 'repeat' | 'leading';
export type IndexRow = [number, string, number, number, number];
export type DetailedRow = [number, string, number, number, number, BlockSummary | null];

interface SlotRef {
  slot: number;
  title: string;
}
export type OutputKind = 'payout' | 'witness' | 'proposal' | 'ack' | 'bundle' | 'vote' | 'bmm' | 'escrow' | 'data';
export interface CoinbaseOutput {
  n: number;
  value: number;
  /** The script type (pubkeyhash, witness_v0_keyhash, nulldata…). */
  type: string;
  kind: OutputKind;
  address?: string;
  slot?: number;
  title?: string;
  hash?: string;
  form?: VoteForm;
  count?: number;
  hex?: string;
  text?: string;
}
export interface VoteEntry extends Partial<SlotRef> {
  /** For an entry whose sidechain cannot be told: its place in the message. */
  position?: number;
  vote: 'upvote' | 'downvote' | 'abstain';
  index?: number;
  bundle?: string;
}
export interface EscrowChange extends SlotRef {
  kind: 'deposit' | 'withdrawal';
  txid: string;
  amount: number;
  total: number;
  destination?: string;
  bundle?: string;
  paid?: number;
  payouts?: number;
  fee?: number;
}
export interface BlockDetail {
  hash: string;
  height: number;
  active: boolean;
  subsidy: number;
  reward: number;
  /** From the coins the transactions spent; null when they are not known (pruned). */
  fees: number | null;
  unclaimed?: number;
  coinbase: string;
  script: string;
  minScore: number;
  outputs: CoinbaseOutput[];
  outputCount: number;
  bmm: (SlotRef & { hash: string; request?: string; bid?: number })[];
  proposals: (SlotRef & { description: string; hash: string; replaces: string })[];
  acks: (SlotRef & { hash: string })[];
  activations: SlotRef[];
  votes: { form: VoteForm; known: boolean; entries: VoteEntry[] };
  events: boolean;
  proposed: (SlotRef & { hash: string })[];
  closed: (SlotRef & { hash: string; paid: boolean })[];
  pending: (SlotRef & { count: number; bundles: { hash: string; score: number }[] })[];
  escrow: EscrowChange[];
  escrowCount: number;
}

/** The most rows one call of explorer.blocks gives with their summaries (explorer::MAX_DETAILED_ROWS). */
export const MAX_DETAILED_ROWS = 100;

/** The heights as runs of consecutive heights, newest first, each of MAX_DETAILED_ROWS at most: one call each. */
export function heightRuns(heights: readonly number[], max = MAX_DETAILED_ROWS): { from: number; count: number }[] {
  const sorted = [...new Set(heights)].sort((a, b) => b - a);
  const runs: { from: number; count: number }[] = [];
  for (const h of sorted) {
    const last = runs[runs.length - 1];
    if (last && last.from - last.count === h && last.count < max) last.count++;
    else runs.push({ from: h, count: 1 });
  }
  return runs;
}

/** What the vote message of a block did, in a few words; '' without one. */
export function voteText(form: VoteForm, votes: number): string {
  switch (form) {
    case 'one':
    case 'two':
      return votes ? tn('%n vote(s)', votes) : t('abstained');
    case 'repeat':
      return t('votes repeated');
    case 'leading':
      return t('upvotes leaders');
    default:
      return '';
  }
}

/** The drivechain activity of a block's coinbase in a few words ("2 BMM · 1 vote · 1 ack"); '' when it has none. */
export function summaryText(s: BlockSummary): string {
  const parts: string[] = [];
  if (s.bmm.length) parts.push(t('%1 BMM', s.bmm.length));
  const votes = voteText(s.voteForm, s.votes);
  if (votes) parts.push(votes);
  if (s.proposals) parts.push(tn('%n proposal(s)', s.proposals));
  if (s.acks) parts.push(tn('%n ack(s)', s.acks));
  if (s.bundles) parts.push(tn('%n bundle(s)', s.bundles));
  if (s.escrow) parts.push(tn('%n escrow change(s)', s.escrow));
  return parts.join(' · ');
}

/** An address cut in its middle, for a narrow cell. */
export function shortAddress(address: string, keep = 8): string {
  return address.length <= keep * 2 + 1 ? address : `${address.slice(0, keep)}…${address.slice(-6)}`;
}

/** What an output of a coinbase is, in words of the app. */
export function outputKindText(o: Pick<CoinbaseOutput, 'kind' | 'form'>): string {
  switch (o.kind) {
    case 'payout':
      return t('Miner payout');
    case 'witness':
      return t('Witness commitment');
    case 'proposal':
      return t('M1 sidechain proposal');
    case 'ack':
      return t('M2 proposal ack');
    case 'bundle':
      return t('M3 bundle proposal');
    case 'vote':
      return t('M4 bundle votes');
    case 'bmm':
      return t('M7 BMM accept');
    case 'escrow':
      return t('Escrow');
    default:
      return t('Data');
  }
}

/** A vote on the bundles of a sidechain, in words. */
export function voteEntryText(v: Pick<VoteEntry, 'vote' | 'index'>): string {
  if (v.vote === 'upvote') return v.index === undefined ? t('Upvote') : t('Upvote bundle #%1', v.index);
  return v.vote === 'downvote' ? t('Downvote all') : t('Abstain');
}

/** The form of a vote message, in words. */
export function voteFormText(form: VoteForm): string {
  switch (form) {
    case 'one':
      return t('one byte per sidechain');
    case 'two':
      return t('two bytes per sidechain');
    case 'repeat':
      return t('the votes of the block before, repeated');
    case 'leading':
      return t('upvote each bundle leading by 50');
    default:
      return t('no vote message (abstains on every sidechain)');
  }
}
