// The translator against text from outside the app (security audit 2026-10-14, item 3): a label that
// reads like a message of the app is shown as it is, in French too, and long text from the chain or a
// peer cannot freeze the page (no backtracking matcher, nothing that looks at the page).
import { flushSync } from 'svelte';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { render, waitFor } from '@testing-library/svelte';
import { afterEach, beforeAll, beforeEach, describe, expect, it } from 'vitest';
import Transactions from '../pages/Transactions.svelte';
import { setTransport } from '../lib/bridge';
import { dialogs } from '../lib/dialogs.svelte';
import { MAX_MATCHED, matchMessage, setCatalog, t, translateText, type Locale } from '../lib/i18n.svelte';
import { app, prefs, wallets } from '../lib/state.svelte';
import { resetWalletData } from '../lib/walletdata.svelte';
import { regtestP2tr, regtestP2wpkh } from './bech32';

const FR_FILE = JSON.parse(readFileSync(join(__dirname, '../../public/locales/fr.json'), 'utf8')) as { code: string; name: string; rtl: boolean; gui: Record<string, string | string[]> };
/** French as the host gives it (app.locale: the messages of the interface). */
const FR: Locale = { code: FR_FILE.code, name: FR_FILE.name, rtl: FR_FILE.rtl, messages: FR_FILE.gui };

beforeAll(() => {
  const proto = HTMLDialogElement.prototype as unknown as Record<string, unknown>;
  proto.showModal ??= function (this: HTMLDialogElement) {
    this.setAttribute('open', '');
  };
  proto.close ??= function (this: HTMLDialogElement) {
    this.removeAttribute('open');
  };
});

beforeEach(() => {
  app.info = { name: 'Chains', version: 'test', bugreport: '', network: 'regtest', networkLabel: 'regtest', datadir: '/tmp', walletEnabled: true, devtools: false, test: true, platform: 'linux', blockSpacing: 600 };
  prefs.unit = 'CHN';
  prefs.mask = false;
  wallets.list = [{ name: 'w', encrypted: false, locked: false, privateKeysDisabled: false, hd: true }];
  wallets.current = 'w';
  wallets.balances.w = { trusted: 5_000_000_000n, untrustedPending: 0n, immature: 0n };
});

afterEach(() => {
  setTransport(null);
  setCatalog(null);
  dialogs.queue.splice(0);
  wallets.list = [];
  wallets.current = null;
  resetWalletData();
});

/** The transactions of the wallet, with these labels. */
function history(labels: string[]) {
  setTransport(async (r) => {
    if (r.method === 'wallet.transactions') {
      return {
        height: 10,
        hash: 'h',
        records: labels.map((label, i) => [String(i).padStart(2, '0').repeat(32), 0, 'recv', i % 2 ? regtestP2tr : regtestP2wpkh, label, 1_700_000_000 + i, 250_000_000, 0, 'confirmed', 7, 0, true, `k${i}`]),
      };
    }
    if (r.method === 'wallet.addresses') return [];
    return null;
  });
}

/** The labels shown in the list, as the page shows them. */
const shownLabels = () => [...document.querySelectorAll('.cellwrap [translate="no"]:not(.amt)')].map((e) => e.textContent ?? '');

describe('text from outside the app', () => {
  it('is shown verbatim in French even when it reads like a message of the app', async () => {
    setCatalog(FR);
    // Messages of the catalog, one with a placeholder ("%1 behind"), and the French of one.
    const labels = ['Send', 'Confirmed', 'Label', '3 hours behind', 'Envoyer'];
    expect(translateText('Send')).toBe('Envoyer');
    expect(translateText('3 hours behind')).not.toBeNull();
    history(labels);
    render(Transactions);
    await waitFor(() => expect(shownLabels().length).toBe(labels.length));
    expect(shownLabels().sort()).toEqual([...labels].sort());
    // The words of the app around them are in French.
    expect(document.querySelector('[role="columnheader"]:nth-child(4)')?.textContent?.trim()).toBe('Étiquette');
  });

  it('of 10 KB renders in under 50 ms, in French', async () => {
    setCatalog(FR);
    // A page shown once, so that what is measured is the label.
    history(['warm up']);
    const first = render(Transactions);
    await waitFor(() => expect(shownLabels()).toEqual(['warm up']));
    first.unmount();
    resetWalletData();

    // What made the regular expressions of the old translator backtrack: the words of the messages,
    // over and over, with no end that fits.
    const unit = ' behind of to at %1 and Sent to Received with ';
    const big = unit.repeat(Math.ceil(10_240 / unit.length)).slice(0, 10_240);
    let delivered = 0;
    setTransport(async (r) => {
      if (r.method === 'wallet.transactions') {
        delivered = performance.now();
        return { height: 10, hash: 'h', records: [['cc'.repeat(32), 0, 'recv', regtestP2wpkh, big, 1_700_000_000, 250_000_000, 0, 'confirmed', 7, 0, true, 'k']] };
      }
      if (r.method === 'wallet.addresses') return [];
      return null;
    });
    render(Transactions);
    let shown = 0;
    await waitFor(
      () => {
        flushSync();
        expect(shownLabels()).toEqual([big]);
        shown = performance.now();
      },
      { interval: 1 },
    );
    expect(delivered).toBeGreaterThan(0);
    expect(shown - delivered).toBeLessThan(50);
  });

  it('is never matched at length, and the matcher does not backtrack', () => {
    setCatalog({ code: 'fr', name: 'Français', rtl: false, messages: { '%1 a %2 a %3 a %4 b': '%1 x %2 x %3 x %4 y', '%1 behind': '%1 de retard' } });
    const evil = 'a '.repeat(5_000);
    let start = performance.now();
    expect(translateText(evil)).toBeNull();
    expect(translateText(evil + 'behind')).toBeNull();
    expect(performance.now() - start).toBeLessThan(50);
    // The matcher itself, on 10 KB: linear.
    start = performance.now();
    for (let i = 0; i < 20; i++) expect(matchMessage(evil + 'c', ['', ' a ', ' a ', ' a ', ' b'])).toBeNull();
    expect(performance.now() - start).toBeLessThan(50);
    // Up to MAX_MATCHED characters, messages with placeholders are found.
    expect(translateText('5 hours behind')).toBe('5 hours de retard');
    expect(translateText('x'.repeat(MAX_MATCHED) + ' behind')).toBeNull();
  });

  it('put in a message of the app is spliced in as it is, not translated', () => {
    setCatalog({ code: 'fr', name: 'Français', rtl: false, messages: { 'Sent to %1': 'Envoyé à %1', Send: 'Envoyer', '%1 behind': '%1 de retard' } });
    expect(t('Sent to %1', 'Send')).toBe('Envoyé à Send');
    // A value holding a placeholder is not filled again.
    expect(t('Sent to %1', '%1 behind')).toBe('Envoyé à %1 behind');
    // A template of the app that is a part of a message: "%1 hours behind" is "%1 behind".
    expect(t('%1 hours behind', 3)).toBe('3 hours de retard');
  });
});
