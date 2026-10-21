// The transactions of the sidechains as rows of the app's own history (pages/Transactions.svelte, doc/unified-app.md
// §21.4): the page is the app's standard Transactions page (its table, search, filters, sorting, export, details) over
// every chain the top bar's selector shows, with a Chain column. This app's own rows are its wallet's records
// (lib/walletdata.svelte.ts, kept up to date in parts); a sidechain's are read from its node through the host
// (chain.transactions: its wallet's history, amounts as its node wrote them), made into records of the same shape once,
// then kept up to date by its latest rows every REFRESH_MS while the page shows it (never at a block of Chains), merged
// by transaction; read whole again every FULL_MS (the confirmations of its old rows) or when the latest no longer meet
// what is held (a burst of more than LATEST).
import { chainHost, chainWallet, parseUnits, type ChainTxRow } from './chains.svelte';
import { every } from './poll';
import type { RecordType, TxRecord, TxStatus } from './wallet';

/** A record of a sidechain's wallet: a record of the app's history, and the chain it is of. */
export interface ChainRecord extends TxRecord {
  chain: string;
}

/** The chain of a row of the page: a sidechain's record says it; the app's own records are of the app's chain. */
export function chainOf(r: TxRecord, host: string): string {
  return (r as Partial<ChainRecord>).chain ?? host;
}

/** How many rows of a sidechain's history are read the first time (the host's most: webui/chainadapter.cpp). */
export const FULL = 5000;
/** How many of its latest rows are read again while shown. */
export const LATEST = 100;
export const REFRESH_MS = 10_000;
/** Read whole again this often while shown: the confirmations of its older rows. */
export const FULL_MS = 5 * 60_000;
/** The blocks a coinbase of a sidechain waits before it can be spent (the template's COINBASE_MATURITY). */
const MATURITY = 100;

const CATEGORY: Record<string, RecordType> = { receive: 'recv', send: 'send', generate: 'mined', immature: 'mined', orphan: 'mined' };

function signed(text: string | undefined): bigint {
  if (!text) return 0n;
  const neg = text.startsWith('-');
  const u = parseUnits(neg ? text.slice(1) : text, 8);
  return u === null ? 0n : neg ? -u : u;
}

/**
 * The rows of a sidechain's history as records of the app's (newest first as the node gives them). A send's fee is
 * counted once per transaction, in its first row, as the app's own records count it; a row that is no transaction of
 * the wallet (a txid that is not one) is left out.
 */
export function chainRecords(stem: string, rows: readonly ChainTxRow[]): ChainRecord[] {
  const out: ChainRecord[] = [];
  const feeTaken = new Set<string>();
  const keys = new Map<string, number>();
  for (const r of rows) {
    if (!/^[0-9a-f]{64}$/.test(r.txid) || !/^-?\d+(\.\d+)?$/.test(r.amount)) continue;
    const category = String(r.category ?? '');
    const type: RecordType = CATEGORY[category] ?? 'other';
    let amount = signed(r.amount);
    if (category === 'send' && r.fee && !feeTaken.has(r.txid)) {
      feeTaken.add(r.txid);
      amount += signed(r.fee);
    }
    const conf = Number.isFinite(r.confirmations) ? Math.trunc(r.confirmations) : 0;
    let status: TxStatus;
    if (category === 'orphan') status = 'notaccepted';
    else if (conf < 0) status = 'conflicted';
    else if (category === 'immature') status = 'immature';
    else if (conf === 0) status = r.abandoned ? 'abandoned' : 'unconfirmed';
    else if (conf < 6) status = 'confirming';
    else status = 'confirmed';
    const base = `${stem}:${r.txid}:${r.vout}:${category}`;
    const n = keys.get(base) ?? 0;
    keys.set(base, n + 1);
    const time = Number.isFinite(r.time) ? r.time : 0;
    out.push({
      chain: stem,
      key: n ? `${base}#${n}` : base,
      txid: r.txid,
      idx: r.vout,
      type,
      address: String(r.address ?? '').slice(0, 120),
      label: String(r.label ?? '').slice(0, 120),
      time,
      credit: amount > 0n ? amount : 0n,
      debit: amount < 0n ? amount : 0n,
      amount,
      status,
      depth: conf,
      maturesIn: status === 'immature' ? Math.max(1, MATURITY - conf) : 0,
      countsForBalance: conf >= 1 && status !== 'immature' && status !== 'notaccepted',
      sortKey: `${String(Math.max(0, time)).padStart(10, '0')}-${stem}-${r.txid}-${String(r.vout).padStart(4, '0')}-${category}`,
      blockHeight: -1,
      state: conf > 0 ? 'c' : conf < 0 ? 'x' : 'u',
      coinbase: type === 'mined',
      abandoned: !!r.abandoned,
      trusted: conf >= 1,
      drivechain: null,
    });
  }
  return out;
}

/** What shows of a record (a row of the table changes only when one of these does: kept otherwise, the same object). */
function same(a: ChainRecord, b: ChainRecord): boolean {
  return a.status === b.status && a.depth === b.depth && a.label === b.label && a.amount === b.amount && a.time === b.time && a.abandoned === b.abandoned && a.address === b.address;
}

/**
 * The latest rows of a chain put in place of those held: a row known and unchanged stays the same object (its row of
 * the table is not drawn again), a changed one is replaced, a new one added; the others stay. null when the latest do
 * not meet what is held (more came than were read): the history is then read whole.
 */
export function mergeLatest(held: readonly ChainRecord[], latest: readonly ChainRecord[]): ChainRecord[] | null {
  if (!latest.length) return held as ChainRecord[];
  const byKey = new Map(held.map((r) => [r.key, r]));
  const fresh = new Map(latest.map((r) => [r.key, r]));
  // The oldest of the latest must be known already, unless the whole history is shorter than what was read.
  if (held.length && latest.length >= LATEST && !byKey.has(latest[latest.length - 1].key)) return null;
  let changed = false;
  const out: ChainRecord[] = [];
  for (const r of latest) {
    const before = byKey.get(r.key);
    if (before && same(before, r)) out.push(before);
    else {
      out.push(r);
      changed = true;
    }
  }
  for (const r of held) if (!fresh.has(r.key)) out.push(r);
  return changed || out.length !== held.length ? out : (held as ChainRecord[]);
}

class SideTxs {
  /** The records of each sidechain shown: raw state, an array replaced as a whole when it changes. */
  lists = $state.raw<Record<string, ChainRecord[]>>({});
  loaded = $state.raw<Record<string, boolean>>({});
  errors = $state.raw<Record<string, string>>({});
}
export const sideTxs = new SideTxs();

/** The wallet each list was read of, and when it was read whole. */
const readOf: Record<string, { wallet: string; fullAt: number }> = {};
const busy = new Set<string>();

function put(stem: string, list: ChainRecord[]): void {
  if (sideTxs.lists[stem] !== list) sideTxs.lists = { ...sideTxs.lists, [stem]: list };
  if (!sideTxs.loaded[stem]) sideTxs.loaded = { ...sideTxs.loaded, [stem]: true };
  if (sideTxs.errors[stem]) {
    const { [stem]: _, ...rest } = sideTxs.errors;
    sideTxs.errors = rest;
  }
}

/** Read a sidechain's history: whole the first time (or when asked, or a while after), else its latest rows. */
export async function refreshSide(stem: string, now = Date.now()): Promise<void> {
  if (busy.has(stem)) return;
  busy.add(stem);
  try {
    const wallet = chainWallet(stem) ?? '';
    const known = readOf[stem];
    const whole = !known || known.wallet !== wallet || !sideTxs.loaded[stem] || now - known.fullAt >= FULL_MS;
    if (!whole) {
      const got = await chainHost.transactions(stem, chainWallet(stem), LATEST);
      const merged = mergeLatest(sideTxs.lists[stem] ?? [], chainRecords(stem, got.rows ?? []));
      if (merged) {
        put(stem, merged);
        return;
      }
    }
    const got = await chainHost.transactions(stem, chainWallet(stem), FULL);
    readOf[stem] = { wallet, fullAt: now };
    put(stem, mergeLatest([], chainRecords(stem, got.rows ?? [])) ?? []);
  } catch (e) {
    sideTxs.errors = { ...sideTxs.errors, [stem]: e instanceof Error ? e.message : String(e) };
    if (!sideTxs.loaded[stem]) sideTxs.loaded = { ...sideTxs.loaded, [stem]: true };
  } finally {
    busy.delete(stem);
  }
}

/** Follow these sidechains while the page shows them: now, then every REFRESH_MS while the window is seen. Returns the stop. */
export function followSides(stems: readonly string[]): () => void {
  if (!stems.length) return () => undefined;
  return every(REFRESH_MS, () => stems.forEach((s) => void refreshSide(s)), { now: true });
}

/** For the tests: forget every list. */
export function resetSideTxs(): void {
  sideTxs.lists = {};
  sideTxs.loaded = {};
  sideTxs.errors = {};
  for (const k of Object.keys(readOf)) delete readOf[k];
  busy.clear();
}
