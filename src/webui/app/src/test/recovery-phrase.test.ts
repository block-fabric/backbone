// The recovery phrases (BIP39) of the Wallets page: a new wallet is made of one by default, shown once and three of its
// words asked back; a wallet is restored from one, with its BIP39 passphrase and its birthday, and what the rescan
// found is shown by address type. The phrase is the host's to check and keep nowhere; the page forgets it.
import { fireEvent, render, screen, waitFor } from '@testing-library/svelte';
import { afterEach, beforeAll, beforeEach, describe, expect, it } from 'vitest';
import DialogHost from '../components/DialogHost.svelte';
import Wallets from '../pages/Wallets.svelte';
import { setTransport, type BridgeRequest } from '../lib/bridge';
import { dialogs } from '../lib/dialogs.svelte';
import { app, prefs, wallets } from '../lib/state.svelte';
import { birthdayTime, isPhraseSize, phraseChecks, phraseWords } from '../lib/wallet';

const PHRASE = 'abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about';
const WORDS = PHRASE.split(' ');

beforeAll(() => {
  // jsdom has no modal dialogs.
  const proto = HTMLDialogElement.prototype as unknown as Record<string, unknown>;
  proto.showModal ??= function (this: HTMLDialogElement) {
    this.setAttribute('open', '');
  };
  proto.close ??= function (this: HTMLDialogElement) {
    this.removeAttribute('open');
  };
});

beforeEach(() => {
  app.info = { name: 'Backbone', version: 'test', bugreport: '', network: 'regtest', networkLabel: 'regtest', datadir: '/tmp', walletEnabled: true, devtools: false, test: true, platform: 'linux', blockSpacing: 600 };
  prefs.unit = 'CHN';
  prefs.mask = false;
});

afterEach(() => {
  setTransport(null);
  dialogs.queue.splice(0);
  wallets.list = [];
  wallets.current = null;
});

let calls: BridgeRequest[] = [];
function host(handlers: Record<string, (p: Record<string, unknown>) => unknown>) {
  calls = [];
  setTransport(async (r) => {
    calls.push(r);
    const h = handlers[r.method ?? ''];
    return h ? h((r.params ?? {}) as Record<string, unknown>) : null;
  });
}

const byId = (id: string) => document.getElementById(id)!;

const BASE = {
  'wallets.list': () => [],
  'wallets.signers': () => ({ signers: [], supported: false }),
};

describe('the words of a phrase', () => {
  it('are read as the host reads them, and three are asked back at random places', () => {
    expect(phraseWords('  Abandon\tABOUT \n zoo ')).toEqual(['abandon', 'about', 'zoo']);
    expect(phraseWords('')).toEqual([]);
    expect([11, 12, 15, 18, 21, 24, 25, 13].map(isPhraseSize)).toEqual([false, true, true, true, true, true, false, false]);
    for (let i = 0; i < 20; i++) {
      const checks = phraseChecks(12);
      expect(checks).toHaveLength(3);
      expect(new Set(checks).size).toBe(3);
      expect([...checks].sort((a, b) => a - b)).toEqual(checks);
      expect(checks.every((p) => p >= 1 && p <= 12)).toBe(true);
    }
    expect(phraseChecks(24, 3, () => 0)).toHaveLength(3);
    expect(birthdayTime('2017-08-01')).toBe(1501545600);
    expect(birthdayTime('')).toBe(0);
    expect(birthdayTime('yesterday')).toBe(0);
  });
});

describe('a new wallet', () => {
  it('is made of a recovery phrase by default: shown once, three words asked back, then forgotten', async () => {
    host({
      ...BASE,
      'wallets.generateMnemonic': () => ({ mnemonic: PHRASE }),
      'wallets.createFromMnemonic': (p) => ({ name: p.name, birthday: 1, rescanned: false, funds: [], warnings: [] }),
    });
    render(Wallets);
    render(DialogHost);
    await fireEvent.click(screen.getByRole('button', { name: /Create wallet/ }));
    const withPhrase = screen.getByRole('checkbox', { name: /With a recovery phrase/ }) as HTMLInputElement;
    expect(withPhrase.checked).toBe(true);
    expect((screen.getByLabelText('Words') as HTMLSelectElement).value).toBe('12');
    await fireEvent.input(byId('wallet-name'), { target: { value: 'savings' } });
    await fireEvent.submit(document.getElementById('create-form')!);

    // The phrase, numbered, as text that is never translated.
    await waitFor(() => screen.getByText(/Write these 12 words down on paper/));
    const list = screen.getByRole('list', { name: 'Recovery phrase' });
    expect(list.querySelectorAll('li')).toHaveLength(12);
    expect(list.querySelectorAll('[translate="no"]')).toHaveLength(12);
    expect(list.textContent).toContain('about');
    expect(calls.find((c) => c.method === 'wallets.generateMnemonic')?.params).toEqual({ words: 12 });

    await fireEvent.click(screen.getByRole('button', { name: 'I wrote them down' }));
    const inputs = [...document.querySelectorAll<HTMLInputElement>('#phrase-check-form input')];
    expect(inputs).toHaveLength(3);
    const places = inputs.map((i) => Number(document.querySelector(`label[for="${i.id}"]`)!.textContent!.replace('Word ', '')));
    // A wrong word: said by its place, and nothing is made.
    for (const [i, input] of inputs.entries()) await fireEvent.input(input, { target: { value: i === 0 ? 'zoo' : WORDS[places[i] - 1] } });
    await fireEvent.submit(document.getElementById('phrase-check-form')!);
    await waitFor(() => screen.getByText(new RegExp(`That is not word ${places[0]} of your recovery phrase`)));
    expect(calls.some((c) => c.method === 'wallets.createFromMnemonic')).toBe(false);
    // The right ones, in any case and spacing.
    await fireEvent.input(inputs[0], { target: { value: `  ${WORDS[places[0] - 1].toUpperCase()} ` } });
    await fireEvent.submit(document.getElementById('phrase-check-form')!);
    await waitFor(() => expect(calls.some((c) => c.method === 'wallets.createFromMnemonic')).toBe(true));
    const made = calls.find((c) => c.method === 'wallets.createFromMnemonic')!;
    // A new phrase: no rescan; no encryption asked for, so no passphrase.
    expect(made.params).toEqual({ name: 'savings', mnemonic: PHRASE, birthday: 'now' });
    // Gone from the page.
    await waitFor(() => expect(document.body.textContent).not.toContain('about'));
    expect(document.querySelectorAll('#phrase-check-form input')).toHaveLength(0);
  });

  it('is made without a phrase for a watch-only or a blank wallet, as before', async () => {
    host({ ...BASE, 'wallets.create': (p) => ({ name: p.name, warnings: [] }) });
    render(Wallets);
    render(DialogHost);
    await fireEvent.click(screen.getByRole('button', { name: /Create wallet/ }));
    await fireEvent.input(byId('wallet-name'), { target: { value: 'watch' } });
    await fireEvent.click(screen.getByRole('checkbox', { name: /Disable Private Keys/ }));
    expect((screen.getByRole('checkbox', { name: /With a recovery phrase/ }) as HTMLInputElement).checked).toBe(false);
    await fireEvent.submit(document.getElementById('create-form')!);
    await waitFor(() => expect(calls.some((c) => c.method === 'wallets.create')).toBe(true));
    expect(calls.some((c) => c.method === 'wallets.generateMnemonic')).toBe(false);
    expect(calls.find((c) => c.method === 'wallets.create')?.params).toMatchObject({ name: 'watch', disablePrivateKeys: true });
  });

  it('forgets the phrase when its dialog is cancelled', async () => {
    host({ ...BASE, 'wallets.generateMnemonic': () => ({ mnemonic: PHRASE }) });
    render(Wallets);
    render(DialogHost);
    await fireEvent.click(screen.getByRole('button', { name: /Create wallet/ }));
    await fireEvent.input(byId('wallet-name'), { target: { value: 'w' } });
    await fireEvent.submit(document.getElementById('create-form')!);
    await waitFor(() => screen.getByRole('list', { name: 'Recovery phrase' }));
    await fireEvent.click(screen.getAllByRole('button', { name: 'Cancel' }).find((b) => b.closest('dialog[open]'))!);
    await waitFor(() => expect(document.body.textContent).not.toContain('about'));
    expect(calls.some((c) => c.method === 'wallets.createFromMnemonic')).toBe(false);
  });
});

describe('restoring from a recovery phrase', () => {
  it('takes the phrase, its BIP39 passphrase and birthday, says when the node is pruned, and shows what the rescan found', async () => {
    host({
      ...BASE,
      getblockchaininfo: () => ({ pruned: true, pruneheight: 900000 }),
      'wallets.createFromMnemonic': (p) => ({
        name: p.name,
        birthday: p.birthday,
        rescanned: true,
        funds: [
          { type: 'bech32', bip: 84, path: 'm/84h/0h/0h', transactions: 2, received: 200_000_000, balance: 150_000_000 },
          { type: 'bech32m', bip: 86, path: 'm/86h/0h/0h', transactions: 0, received: 0, balance: 0 },
          { type: 'p2sh-segwit', bip: 49, path: 'm/49h/0h/0h', transactions: 1, received: 25_000_000, balance: 25_000_000 },
          { type: 'legacy', bip: 44, path: 'm/44h/0h/0h', transactions: 0, received: 0, balance: 0 },
        ],
        warnings: [],
      }),
    });
    render(Wallets);
    render(DialogHost);
    await fireEvent.click(screen.getByRole('button', { name: /Restore from recovery phrase/ }));
    await waitFor(() => screen.getByText(/This node is pruned: it only has the blocks from height 900000 on/));
    await fireEvent.input(byId('restore-name'), { target: { value: 'old coins' } });
    const text = byId('restore-phrase') as HTMLTextAreaElement;
    expect(text.getAttribute('spellcheck')).toBe('false');
    expect(text.getAttribute('autocomplete')).toBe('off');
    const restoreButton = screen.getByRole('button', { name: 'Restore' }) as HTMLButtonElement;
    await fireEvent.input(text, { target: { value: WORDS.slice(0, 11).join(' ') } });
    expect(screen.getByText(/11 words: a phrase has 12, 15, 18, 21 or 24 BIP39 words/)).toBeTruthy();
    expect(restoreButton.disabled).toBe(true);
    await fireEvent.input(text, { target: { value: PHRASE } });
    expect(restoreButton.disabled).toBe(false);
    await fireEvent.input(screen.getByLabelText('Wallet birthday (optional)'), { target: { value: '2017-08-01' } });
    const bip39 = screen.getByLabelText('BIP39 passphrase') as HTMLInputElement;
    expect(bip39.type).toBe('password');
    await fireEvent.input(bip39, { target: { value: 'TREZOR' } });
    await fireEvent.submit(document.getElementById('restore-phrase-form')!);

    await waitFor(() => screen.getByText(/The rescan found coins of these address types/));
    expect(calls.find((c) => c.method === 'wallets.createFromMnemonic')?.params).toEqual({ name: 'old coins', mnemonic: PHRASE, bip39Passphrase: 'TREZOR', birthday: 1501545600 });
    // Only the types that held coins.
    const table = document.querySelector('table.funds')!;
    expect(table.querySelectorAll('tbody tr')).toHaveLength(2);
    expect(table.textContent).toContain('Bech32 (SegWit)');
    expect(table.textContent).toContain('m/84h/0h/0h');
    expect(table.textContent).toContain('1.50000000');
    expect(table.textContent).toContain('Base58 (P2SH-SegWit)');
    expect(table.textContent).not.toContain('Taproot');
    // The phrase is gone from the form.
    expect(document.body.textContent).not.toContain('abandon');
    expect(document.querySelector<HTMLTextAreaElement>('#restore-phrase')?.value ?? '').toBe('');
  });

  it('shows the error of the host, which names no word of the phrase', async () => {
    host({
      ...BASE,
      getblockchaininfo: () => ({ pruned: false }),
      'wallets.createFromMnemonic': () => {
        throw Object.assign(new Error('The checksum of the recovery phrase does not match: a word is mistyped, missing or out of place.'), { code: -1 });
      },
    });
    render(Wallets);
    render(DialogHost);
    await fireEvent.click(screen.getByRole('button', { name: /Restore from recovery phrase/ }));
    expect(screen.queryByText(/This node is pruned/)).toBeNull();
    await fireEvent.input(byId('restore-name'), { target: { value: 'w' } });
    await fireEvent.input(byId('restore-phrase'), { target: { value: Array(12).fill('abandon').join(' ') } });
    await fireEvent.submit(document.getElementById('restore-phrase-form')!);
    await waitFor(() => screen.getByText(/The checksum of the recovery phrase does not match/));
    expect(calls.find((c) => c.method === 'wallets.createFromMnemonic')?.params).toMatchObject({ birthday: 0 });
  });
});
