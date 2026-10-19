// Many transactions of the wallet at once (user feedback 2026-10-19, item 10: a script made a thousand assets in a
// sidechain's app and the UI skipped frames): the pages read the wallet again once per burst, not once per transaction.
import { waitFor } from '@testing-library/svelte';
import { afterEach, beforeAll, describe, expect, it } from 'vitest';
import { dispatch } from '../lib/bridge';
import { BURST_MS, STREAM_MS, subscribe, wallets } from '../lib/state.svelte';

beforeAll(() => subscribe());
afterEach(() => {
  wallets.list = [];
  wallets.current = null;
});

describe('a burst of transactions of the wallet', () => {
  it('is one change for the pages, made within a moment', async () => {
    wallets.current = 'w';
    const before = wallets.txVersion;
    const t0 = Date.now();
    for (let i = 0; i < 500; i++) dispatch([{ type: 'wallet.tx', data: { name: 'w', txid: i.toString(16).padStart(64, '0'), change: 'new' } }]);
    // The notices still count every one.
    expect(wallets.newTxs.length).toBeGreaterThan(0);
    expect(wallets.txVersion).toBe(before);
    await waitFor(() => expect(wallets.txVersion).toBe(before + 1));
    expect(Date.now() - t0).toBeGreaterThanOrEqual(BURST_MS - 20);
    // Another later is another change; while they keep coming, at most one a second.
    const t1 = Date.now();
    dispatch([{ type: 'wallet.tx', data: { name: 'w', txid: 'ff'.repeat(32), change: 'new' } }]);
    await waitFor(() => expect(wallets.txVersion).toBe(before + 2), { timeout: 3000 });
    expect(Date.now() - t1).toBeGreaterThanOrEqual(STREAM_MS - 300);
    // Another wallet's change no change of the one shown.
    dispatch([{ type: 'wallet.tx', data: { name: 'other', txid: 'ee'.repeat(32), change: 'new' } }]);
    await new Promise((r) => setTimeout(r, BURST_MS + 50));
    expect(wallets.txVersion).toBe(before + 2);
  });

  it('changes the addresses once too', async () => {
    wallets.current = 'w';
    const before = wallets.addressVersion;
    for (let i = 0; i < 100; i++) dispatch([{ type: 'wallet.addressbook', data: { name: 'w' } }]);
    await waitFor(() => expect(wallets.addressVersion).toBe(before + 1));
  });
});
