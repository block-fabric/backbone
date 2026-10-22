// The recent activity of many chains as people read it (doc/unified-app.md §20.2, §20.3): each row named in words (what
// happened, or the label the user gave), the address only as its second line, and the small rewards of mining folded
// into one row per chain so that they do not bury the payments. Pure: the components draw it.
import { formatUnits, parseUnits, type ManyChainTx } from './chains.svelte';
import { t } from './i18n.svelte';

/** The label a chain that stakes gives the address its validator's share of a block's fees goes to (FastBlocks, Layers:
 *  -validatorpayout, else a new address of its wallet so labelled), as the app's own validator key has it too. */
export const VALIDATOR_PAYOUT = 'validator payout';

/** The labels a sidechain's wallet gives the addresses the peg pays (getdepositaddress, createwithdrawal's refund). */
const PEG_LABELS = new Set(['deposit', 'withdrawal refund']);

/** A sidechain's coinbase output paid to the peg's addresses (a deposit credited, a withdrawal refunded): its label says it
 *  (a row read without its label: by its place, after the block's maker). */
function pegPaid(r: { vout: number; label?: string }): boolean {
  return r.vout > 0 && (r.label === undefined || PEG_LABELS.has(r.label));
}

/** A sidechain's coinbase pays the block's maker first, then the peg (deposits, refunds), and on a chain that stakes the
 *  validator's and the merged mining bidder's shares of the fees: whatever is not the peg's is a reward. */
export function isReward(r: Pick<ManyChainTx, 'category' | 'vout' | 'chain'> & { label?: string }, host: string): boolean {
  if (r.chain === host) return r.category === 'mined';
  return (r.category === 'generate' || r.category === 'immature' || r.category === 'orphan') && !pegPaid(r);
}

/** A merged mining bid this app's wallet paid (M8, for a sidechain's block): many, small, folded like the rewards. */
export function isBid(r: Pick<ManyChainTx, 'chain' | 'drivechain'>, host: string): boolean {
  return r.chain === host && r.drivechain?.kind === 'bmm';
}

/** A row that says nothing to people, left out of the lists: a reward that pays nothing (a sidechain merged mined
 *  without a subsidy pays its maker only the fees, often none), or an output of no value of a drivechain transaction
 *  the wallet paid (the destination of its first deposit into a sidechain: the deposit is its other row). */
export function isEmptyRow(r: Pick<ManyChainTx, 'category' | 'vout' | 'chain' | 'amount' | 'drivechain'> & { label?: string }, host: string): boolean {
  const zero = (parseUnits(r.amount.replace(/^-/, ''), 8) ?? 0n) === 0n;
  return zero && (isReward(r, host) || (r.chain === host && !!r.drivechain));
}

/** Coins of the peg a sidechain's coinbase paid this wallet (a deposit credited, or a withdrawal refunded). */
export function isPegIn(r: Pick<ManyChainTx, 'category' | 'vout' | 'chain'> & { label?: string }, host: string): boolean {
  return r.chain !== host && (r.category === 'generate' || r.category === 'immature') && pegPaid(r);
}

export interface ActivityTx {
  kind: 'tx';
  key: string;
  tx: ManyChainTx;
  /** What happened: a message of the app (translated where drawn, %1 the app's own chain). */
  what: string;
  /** The label the user gave the address ('' for none): shown first when there is one. */
  label: string;
  /** The address or id for the second line ('' for none). */
  ref: string;
  incoming: boolean;
  /** The slot of the sidechain a deposit went into (null for another row). */
  side: number | null;
}

export interface ActivityRewards {
  kind: 'rewards';
  /** Mining rewards received, or merged mining bids paid. */
  what: 'rewards' | 'bids';
  key: string;
  chain: string;
  name: string;
  count: number;
  /** Their sum, exactly, in coins (8 decimals): what the bids cost with a minus. */
  total: string;
  /** The newest of them. */
  time: number;
  pending: number;
}

export type ActivityItem = ActivityTx | ActivityRewards;

/** What a row is, in words: an English message of the app (%1 the app's own chain, given when it is drawn). */
export function whatHappened(r: ManyChainTx, host: string): string {
  if (isReward(r, host)) return 'Mining reward';
  if (isBid(r, host)) return 'Merged mining bid';
  if (r.chain === host && r.drivechain?.kind === 'deposit') return 'Deposit into %1';
  if (isPegIn(r, host)) return 'Coins in from %1';
  const out = r.amount.startsWith('-');
  if (r.chain === host) {
    if (r.category === 'recv' || r.category === 'recvOther') return 'Payment received';
    if (r.category === 'send' || r.category === 'sendOther') return 'Payment sent';
    return out ? 'Payment sent' : 'Payment received';
  }
  if (r.category === 'send') return 'Payment sent';
  if (r.category === 'receive') return 'Payment received';
  return out ? 'Payment sent' : 'Payment received';
}

/** What a row is, in the language shown (`product`: the app's own chain, the side the coins come from; `side`: the
 *  sidechain a deposit went into, '' when the app does not know it). */
export function whatWords(what: string, product: string, side = ''): string {
  switch (what) {
    case 'Mining reward':
      return t('Mining reward');
    case 'Merged mining bid':
      return t('Merged mining bid');
    case 'Deposit into %1':
      return side ? t('Deposit into %1', side) : t('Deposit into a sidechain');
    case 'Coins in from %1':
      return t('Coins in from %1', product);
    case 'Payment sent':
      return t('Payment sent');
    default:
      return t('Payment received');
  }
}

/**
 * The newest `count` items of the rows (newest first): every payment as its own row; each chain's mining rewards
 * folded into one row where its newest is, with how many and their sum. Rows of one transaction to several of its
 * outputs stay apart only when they differ in direction.
 */
export function foldActivity(rows: readonly ManyChainTx[], count: number, host: string): ActivityItem[] {
  const sorted = [...rows].sort((a, b) => b.time - a.time || a.chain.localeCompare(b.chain) || a.txid.localeCompare(b.txid) || a.vout - b.vout);
  const out: ActivityItem[] = [];
  const groups = new Map<string, { item: ActivityRewards; units: bigint }>();
  for (const r of sorted) {
    if (isEmptyRow(r, host)) continue;
    const bid = isBid(r, host);
    if (bid || isReward(r, host)) {
      const what = bid ? 'bids' : 'rewards';
      const g = groups.get(`${what}:${r.chain}`);
      const units = parseUnits(r.amount.replace(/^-/, ''), 8) ?? 0n;
      if (g) {
        g.item.count++;
        g.units += units;
        g.item.total = (bid ? '-' : '') + formatUnits(g.units, 8);
        if (r.confirmations === 0 || r.category === 'immature') g.item.pending++;
        continue;
      }
      if (out.length >= count) continue;
      const item: ActivityRewards = { kind: 'rewards', what, key: `${what}:${r.chain}`, chain: r.chain, name: r.name, count: 1, total: (bid ? '-' : '') + formatUnits(units, 8), time: r.time, pending: r.confirmations === 0 || r.category === 'immature' ? 1 : 0 };
      groups.set(`${what}:${r.chain}`, { item, units });
      out.push(item);
      continue;
    }
    if (out.length >= count) continue;
    const what = whatHappened(r, host);
    const label = r.label.trim();
    out.push({
      kind: 'tx',
      key: `${r.chain}:${r.txid}:${r.vout}:${r.category}`,
      tx: r,
      what,
      label,
      ref: label ? r.address : r.address || r.txid,
      incoming: !r.amount.startsWith('-'),
      side: r.drivechain?.kind === 'deposit' ? r.drivechain.slot : null,
    });
  }
  return out;
}
