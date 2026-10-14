// What the wallet pages show of the current wallet, loaded once and shared: its info, its history,
// its address book. Reloaded when the wallet changes (events), not more often than needed.
import { node, wallets } from './state.svelte';
import { walletApi, type AddressEntry, type TxRecord, type WalletInfo } from './wallet';

export const current = $state({
  info: null as WalletInfo | null,
  records: [] as TxRecord[],
  recordsLoaded: false,
  recordsError: '',
  addresses: [] as AddressEntry[],
  addressesLoaded: false,
});

interface Loaded {
  wallet: string;
  version: string;
}

let infoFor: Loaded | null = null;
let recordsFor: Loaded | null = null;
let addressesFor: Loaded | null = null;
let recordsInFlight: Promise<void> | null = null;
let recordsAt = 0;
let recordsTimer: ReturnType<typeof setTimeout> | undefined;
/** A new block alone reloads the history at most this often. */
const TIP_RELOAD_MS = 2500;

/** The client of the current wallet; null without one. */
export function api() {
  return wallets.current === null ? null : walletApi(wallets.current);
}

/** The info of the current wallet (locked, keys, address types...). */
export async function refreshInfo(force = false): Promise<WalletInfo | null> {
  const name = wallets.current;
  if (name === null) {
    current.info = null;
    return null;
  }
  const w = wallets.list.find((x) => x.name === name);
  const version = `${w?.locked}:${w?.encrypted}:${wallets.txVersion}`;
  if (!force && infoFor && infoFor.wallet === name && infoFor.version === version && current.info) return current.info;
  infoFor = { wallet: name, version };
  try {
    const info = await walletApi(name).info();
    if (wallets.current === name) current.info = info;
    return info;
  } catch {
    return null;
  }
}

/**
 * The history of the current wallet. The status of each record changes with each block, so it is
 * loaded again after a new block too, at most every few seconds while the chain moves fast.
 */
export function refreshRecords(force = false): Promise<void> {
  const name = wallets.current;
  if (name === null) {
    current.records = [];
    current.recordsLoaded = false;
    return Promise.resolve();
  }
  const version = `${wallets.txVersion}:${node.tip?.hash ?? ''}`;
  if (!force && recordsFor && recordsFor.wallet === name && recordsFor.version === version) return recordsInFlight ?? Promise.resolve();
  const sameTxs = recordsFor?.wallet === name && recordsFor.version.split(':')[0] === String(wallets.txVersion);
  if (!force && sameTxs && Date.now() - recordsAt < TIP_RELOAD_MS) {
    // Only the tip moved, and the history was loaded a moment ago: later.
    clearTimeout(recordsTimer);
    recordsTimer = setTimeout(() => refreshRecords(), TIP_RELOAD_MS);
    return recordsInFlight ?? Promise.resolve();
  }
  if (recordsFor?.wallet !== name) {
    current.records = [];
    current.recordsLoaded = false;
  }
  recordsFor = { wallet: name, version };
  recordsAt = Date.now();
  const run = (async () => {
    try {
      const r = await walletApi(name).transactions();
      if (wallets.current !== name) return;
      current.records = r.records;
      current.recordsLoaded = true;
      current.recordsError = '';
    } catch (e) {
      if (wallets.current === name) current.recordsError = e instanceof Error ? e.message : String(e);
    }
  })();
  recordsInFlight = run.finally(() => {
    if (recordsInFlight === run) recordsInFlight = null;
  });
  return recordsInFlight;
}

/** The address book of the current wallet. */
export async function refreshAddresses(force = false): Promise<void> {
  const name = wallets.current;
  if (name === null) {
    current.addresses = [];
    return;
  }
  const version = `${wallets.addressVersion}:${wallets.txVersion}`;
  if (!force && addressesFor && addressesFor.wallet === name && addressesFor.version === version) return;
  addressesFor = { wallet: name, version };
  try {
    const list = await walletApi(name).addresses();
    if (wallets.current === name) {
      current.addresses = list;
      current.addressesLoaded = true;
    }
  } catch {
    // The wallet went away; the next event says so.
  }
}

/** The label of an address in the book, or ''. */
export function labelOf(address: string): string {
  return current.addresses.find((a) => a.address === address)?.label ?? '';
}

/** Forget what was loaded (tests, a wallet closed). */
export function resetWalletData(): void {
  infoFor = recordsFor = addressesFor = null;
  current.info = null;
  current.records = [];
  current.recordsLoaded = false;
  current.addresses = [];
  current.addressesLoaded = false;
}
