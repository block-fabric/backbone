// The backup phrase in the pages (doc/unified-app.md §23): the dialog's window shows the words (never selectable, a copy
// refused and said) and takes the fields (a word of the list or not as it is typed, the words counted, a new passphrase's
// length, the box ticked first); the word list is BIP39's, the host's own; the guides (the first start, the move of
// wallets of before, the guided move of coins), Settings → Wallet → Backup phrase and the enable wizard against a pretend
// host. No page ever holds a word: the host asks in its own window.
import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { resolve } from 'node:path';
import { fireEvent, render as mount, screen, waitFor } from '@testing-library/svelte';
import { afterEach, beforeAll, beforeEach, describe, expect, it } from 'vitest';
import { ARM_MS, isListWord, phraseState, readData, render, wire, type Typed } from '../dialog/dialog';
import { BIP39_WORDS } from '../lib/bip39words';
import { allReady, autoGuide, chainWords, progressText, refreshSeed, seed, type SeedStatus } from '../lib/seed.svelte';
import { setTransport, type BridgeRequest } from '../lib/bridge';
import { hub } from '../lib/sidechains.svelte';
import { app, wallets } from '../lib/state.svelte';
import FirstRunWizard from '../components/FirstRunWizard.svelte';
import MigrateWizard from '../components/MigrateWizard.svelte';
import MoveWizard from '../components/MoveWizard.svelte';
import SeedSettings from '../components/SeedSettings.svelte';
import SeedFlows from '../components/SeedFlows.svelte';
import EnableWizard from '../components/EnableWizard.svelte';
import type { SidechainEntry, SidechainList } from '../lib/unified';

describe('the word list', () => {
  it('is BIP39\'s English list, the host\'s own', () => {
    expect(BIP39_WORDS.length).toBe(2048);
    expect([...BIP39_WORDS].sort()).toEqual([...BIP39_WORDS]);
    expect(createHash('sha256').update(BIP39_WORDS.map((w) => w + '\n').join('')).digest('hex')).toBe('2f5eed53a4727b4bf8880d8f3f199efc90e58503646d9ff8eff3a2ed3b24dbda');
    // The host's (src/webui/bip39_english.cpp): the same words, in the same order.
    const cpp = readFileSync(resolve(__dirname, '../../../bip39_english.cpp'), 'utf8');
    expect([...cpp.matchAll(/"([a-z]+)"/g)].map((m) => m[1])).toEqual([...BIP39_WORDS]);
  });

  it('says a word of the list, counts the words, and finds the first that is not one', () => {
    expect(isListWord('abandon')).toBe(true);
    expect(isListWord(' Zoo ')).toBe(true);
    expect(isListWord('bitcoin')).toBe(false);
    expect(phraseState('')).toMatchObject({ count: 0, bad: 0 });
    expect(phraseState('legal winner thank')).toMatchObject({ count: 3, bad: 0, countOk: false });
    expect(phraseState('legal winner thank year wave sausage worth usefull legal winner thank yellow')).toMatchObject({ count: 12, bad: 8, badWord: 'usefull' });
    expect(phraseState(Array(24).fill('zoo').join('\n')).countOk).toBe(true);
  });
});

describe('the dialog\'s window: a phrase shown, fields typed', () => {
  let now = 0;
  const clock = () => now;
  const base = { title: 'Your backup phrase', message: 'Write these 12 words on paper', accept: 'Next', level: 'warning', look: { theme: 'midnight' } };
  const opened: { dispose(): void }[] = [];
  afterEach(() => {
    for (const c of opened) c.dispose();
    opened.length = 0;
    document.body.replaceChildren();
  });
  function open(raw: Record<string, unknown>) {
    const r = render(document, readData({ ...base, ...raw }));
    document.body.replaceChildren(r.root);
    const answers: { accept: boolean; passphrase?: string; typed?: Typed }[] = [];
    const control = wire(document, r, (accept, passphrase, typed) => answers.push({ accept, passphrase, typed }), clock);
    opened.push(control);
    now = 0;
    control.arm(0);
    return { r, answers };
  }

  it('reads what the host sends strictly: words of the list form only, fields of known kinds and ids', () => {
    const d = readData({ ...base, words: ['abandon', 'ABOUT', '<b>', 'zoo', 7], fields: [{ id: 'word-3', kind: 'word', label: 'Word 3' }, { id: 'Bad Id', kind: 'word' }, { id: 'x', kind: 'html' }], checkbox: 'I wrote them down' });
    expect(d.words).toEqual(['abandon', 'zoo']);
    expect(d.fields.map((f) => [f.id, f.kind])).toEqual([['word-3', 'word'], ['x', 'password']]);
    expect(d.checkbox).toBe('I wrote them down');
  });

  it('shows the words numbered, not selectable, and refuses a copy, saying why', () => {
    const words = 'legal winner thank year wave sausage worth useful legal winner thank yellow'.split(' ');
    const { r } = open({ words, checkbox: 'I wrote the 12 words down, in order' });
    const items = [...document.querySelectorAll('#dialog-words li')];
    expect(items.map((li) => li.querySelector('.w')?.textContent)).toEqual(words);
    expect(items[7].querySelector('.n')?.textContent).toBe('8');
    const copy = new Event('copy', { bubbles: true, cancelable: true });
    document.querySelector('#dialog-words')!.dispatchEvent(copy);
    expect(copy.defaultPrevented).toBe(true);
    expect(document.querySelector('#dialog-words-note')?.textContent).toContain('write the words on paper');
    // The box first: the button that accepts waits for it, then for its delay.
    expect(r.accept.disabled).toBe(true);
    r.box!.click();
    expect(r.accept.disabled).toBe(false);
  });

  it('answers the box and the fields; a field needed and empty, or the box not ticked, sends nothing; the fields emptied after', () => {
    const { r, answers } = open({ checkbox: 'Sure', fields: [{ id: 'word-3', kind: 'word', label: 'Word 3' }, { id: 'day', kind: 'date', label: 'When', optional: true }] });
    now = ARM_MS + 1;
    r.box!.click();
    r.accept.click();
    // Word 3 empty: said there, nothing sent.
    expect(answers).toEqual([]);
    expect(r.inputs[0].el.getAttribute('aria-invalid')).toBe('true');
    (r.inputs[0].el as HTMLInputElement).value = 'thank';
    r.accept.click();
    expect(answers).toEqual([{ accept: true, passphrase: undefined, typed: { checked: true, fields: { 'word-3': 'thank' } } }]);
    expect(r.inputs[0].el.value).toBe('');
  });

  it('says as it is typed: a word not of the list, how many words, a new passphrase\'s length', () => {
    const { r } = open({ minNewPassword: 8, fields: [{ id: 'w', kind: 'word', label: 'Word 3' }, { id: 'words', kind: 'words', label: 'Your phrase' }, { id: 'p', kind: 'new-password', label: 'Passphrase' }] });
    const live = (i: number) => r.inputs[i].el.parentElement!.querySelector('.live') as HTMLElement;
    const type = (i: number, v: string) => {
      r.inputs[i].el.value = v;
      r.inputs[i].el.dispatchEvent(new Event('input'));
    };
    type(0, 'thnak');
    expect(live(0).textContent).toContain('Not a word of the list');
    type(0, 'thank');
    expect(live(0).textContent).toBe('');
    type(1, 'legal winner thank year');
    expect(live(1).textContent).toBe('4 words · 12 or 24 are needed');
    type(1, 'legal winner thank year wave sausage worth usefull legal winner thank yellow');
    expect(live(1).textContent).toContain('Word 8, “usefull”, is not a word of the list');
    type(1, 'legal winner thank year wave sausage worth useful legal winner thank yellow');
    expect(live(1).textContent).toBe('12 words');
    type(2, 'abc');
    expect(live(2).textContent).toBe('5 more characters');
    type(2, 'correct horse');
    expect(live(2).textContent).toBe('Long enough');
    // A phrase given back to be corrected, with the error at its field; the focus there.
    document.body.replaceChildren();
    const again = open({ fields: [{ id: 'words', kind: 'words', label: 'Your phrase', value: 'legal winner', error: 'Word 2 is not right' }] });
    expect(again.r.inputs[0].el.value).toBe('legal winner');
    expect(document.querySelector('[role="alert"]')?.textContent).toBe('Word 2 is not right');
    expect(document.activeElement).toBe(again.r.inputs[0].el);
  });
});

describe('the state of the phrase in words', () => {
  const status = (over: Partial<SeedStatus> = {}): SeedStatus => ({ exists: false, unlocked: false, nodeReady: true, firstRun: false, offer: false, minPassphrase: 8, chains: [], ...over });
  it('opens the first start or the offer by itself, once, never over another guide, never before the node is ready', () => {
    const s = { open: null, autoOpen: true, autoOpened: false };
    expect(autoGuide(status({ firstRun: true }), s)).toBe('firstrun');
    expect(autoGuide(status({ offer: true }), s)).toBe('migrate');
    expect(autoGuide(status({ firstRun: true, nodeReady: false }), s)).toBeNull();
    expect(autoGuide(status({ firstRun: true }), { ...s, autoOpened: true })).toBeNull();
    expect(autoGuide(status({ firstRun: true }), { ...s, open: 'move' })).toBeNull();
    expect(autoGuide(status({ firstRun: true }), { ...s, autoOpen: false })).toBeNull();
    expect(autoGuide(status({ exists: true }), s)).toBeNull();
  });
  it('says each chain\'s wallet in plain words, with how far a rescan is', () => {
    expect(chainWords({ id: 'layers', name: 'Layers', mode: 'phrase', state: 'ready' }).text).toBe('From your backup phrase');
    expect(chainWords({ id: 'layers', name: 'Layers', mode: 'phrase', state: 'needs-keys' }).tone).toBe('warn');
    expect(chainWords({ id: 'layers', name: 'Layers', mode: 'separate', state: 'own' }).text).toContain('back it up on its own');
    expect(progressText({ id: 'chains', name: 'Chains', mode: 'phrase', state: 'scanning', progress: 0.4 })).toBe('40 %');
    expect(progressText({ id: 'chains', name: 'Chains', mode: 'phrase', state: 'scanning' })).toBe('');
    expect(allReady(status({ chains: [{ id: 'chains', name: 'Chains', mode: 'phrase', state: 'ready' }, { id: 'thunder', name: 'Thunder', mode: 'separate', state: 'own' }] }))).toBe(true);
    expect(allReady(status({ chains: [{ id: 'layers', name: 'Layers', mode: 'phrase', state: 'importing' }] }))).toBe(false);
  });
});

// ---- The guides against a pretend host ----

beforeAll(() => {
  const proto = HTMLDialogElement.prototype as unknown as Record<string, unknown>;
  proto.showModal ??= function (this: HTMLDialogElement) {
    this.setAttribute('open', '');
  };
  proto.close ??= function (this: HTMLDialogElement) {
    this.removeAttribute('open');
  };
});

function entry(stem: string, over: Partial<SidechainEntry> = {}): SidechainEntry {
  const names: Record<string, string> = { layers: 'Layers', thunder: 'Thunder' };
  return { stem, name: names[stem] ?? stem, description: `${stem} for tests`, slot: 7, diskGB: 4, ramMB: 400, staking: false, enabled: false, state: 'disabled', pid: 0, restarts: 0, ready: false, dataDir: '', defaultDataDir: '', customDataDir: false, hasData: false, freeGB: 200, program: `/app/${stem}d`, rpcPort: 1, port: 2, wallet: 'shared', ...over };
}

let calls: BridgeRequest[];
let st: SeedStatus;
let list: SidechainList;
let answers: Record<string, unknown>;

beforeEach(() => {
  app.phase = 'ready';
  calls = [];
  answers = {};
  seed.status = null;
  seed.open = null;
  seed.autoOpen = true;
  seed.autoOpened = false;
  list = { network: 'regtest', rpcServer: false, chainsDataDir: '/data', chains: [entry('layers'), entry('thunder')] };
  hub.list = structuredClone(list);
  st = { exists: false, unlocked: false, nodeReady: true, firstRun: true, offer: false, minPassphrase: 8, chains: [{ id: 'chains', name: 'Chains', mode: 'own', state: 'own' }] };
  setTransport(async (r) => {
    calls.push(r);
    const p = (r.params ?? {}) as Record<string, unknown>;
    if (r.method && r.method in answers) {
      const a = answers[r.method];
      return typeof a === 'function' ? (a as (p: Record<string, unknown>) => unknown)(p) : a;
    }
    switch (r.method) {
      case 'seed.status':
        return structuredClone(st);
      case 'seed.create':
      case 'seed.restore':
        st = { ...st, exists: true, unlocked: true, firstRun: false, chains: [{ id: 'chains', name: 'Chains', mode: 'phrase', state: 'ready', wallet: 'main' }] };
        return { created: r.method === 'seed.create', restored: r.method === 'seed.restore', fingerprint: '73c5da0a', words: 12 };
      case 'seed.unlock':
        st = { ...st, unlocked: true };
        return { unlocked: true };
      case 'seed.dismiss':
        return true;
      case 'sidechains.list':
        return structuredClone(list);
      case 'sidechains.enable': {
        const e = list.chains.find((c) => c.stem === p.stem)!;
        Object.assign(e, { enabled: true, state: 'running', ready: true, wallet: p.wallet });
        st.chains.push({ id: String(p.stem), name: e.name, mode: 'phrase', state: 'importing' });
        return true;
      }
      default:
        return null;
    }
  });
});

afterEach(() => setTransport(null));

const methods = () => calls.map((c) => c.method);

describe('the first start', () => {
  it('asks the host to make the phrase (never shows a word), goes on once made, and turns the sidechains picked on with the phrase unlocked', async () => {
    await refreshSeed();
    mount(FirstRunWizard, { onclose: () => {} });
    const wizard = () => document.querySelector('[data-wizard="firstrun"]') as HTMLElement;
    const next = () => document.querySelector('[data-action="next"]') as HTMLButtonElement;
    expect(wizard().dataset.step).toBe('welcome');
    await fireEvent.click(next());
    expect(wizard().textContent).toContain('a phrase of its own');
    await fireEvent.click(next());
    expect(wizard().dataset.step).toBe('backup');
    expect(next().disabled).toBe(true);
    // 24 words and an extra word are options of their own.
    await fireEvent.click(screen.getByText('More options'));
    await fireEvent.click(screen.getByText('24 words'));
    await fireEvent.click(document.querySelector('[data-action="make-phrase"]') as HTMLButtonElement);
    await waitFor(() => expect(document.querySelector('[data-made]')).toBeTruthy());
    expect(calls.find((c) => c.method === 'seed.create')?.params).toEqual({ words: 24, extraWord: false });
    // Nothing of the phrase in the page.
    expect(document.body.textContent).not.toMatch(/abandon|zoo/);
    await fireEvent.click(next());
    expect(wizard().dataset.step).toBe('network');
    await fireEvent.click(next());
    await fireEvent.click(document.querySelector('.pick[data-stem="layers"] input') as HTMLInputElement);
    await fireEvent.click(next());
    expect(wizard().dataset.step).toBe('storage');
    await fireEvent.click(next());
    // The phrase locked by now (its time was up): unlocked once for the chains picked, then they are turned on.
    st.unlocked = false;
    await refreshSeed();
    await fireEvent.click(document.querySelector('[data-action="finish"]') as HTMLButtonElement);
    await waitFor(() => expect(document.querySelector('[data-wizard="firstrun"]')?.getAttribute('data-step')).toBe('done'));
    const order = methods().filter((m) => m === 'seed.unlock' || m === 'sidechains.enable');
    expect(order).toEqual(['seed.unlock', 'sidechains.enable']);
    expect(calls.find((c) => c.method === 'seed.unlock')?.params).toEqual({ stems: ['layers'] });
    expect((calls.find((c) => c.method === 'sidechains.enable')?.params as Record<string, unknown>).wallet).toBe('shared');
    expect(document.querySelector('[data-seed-chains] li[data-chain="layers"]')?.getAttribute('data-state')).toBe('importing');
    expect(document.querySelector('[data-action="move"]')).toBeTruthy();
  });

  it('restores instead: the host asks for the words; a cancel makes nothing; skipping is remembered by the host', async () => {
    answers['seed.restore'] = { cancelled: true };
    await refreshSeed();
    mount(FirstRunWizard, { onclose: () => {} });
    await fireEvent.click(document.querySelector('[data-action="next"]') as HTMLButtonElement);
    await fireEvent.click(screen.getByText('Restore from a backup phrase'));
    await fireEvent.click(document.querySelector('[data-action="next"]') as HTMLButtonElement);
    expect(document.querySelector('[data-wizard="firstrun"]')?.getAttribute('data-step')).toBe('restore');
    await fireEvent.click(document.querySelector('[data-action="make-phrase"]') as HTMLButtonElement);
    await waitFor(() => expect(document.querySelector('[role="alert"]')?.textContent).toContain('Nothing was made'));
    expect((document.querySelector('[data-action="next"]') as HTMLButtonElement).disabled).toBe(true);
    await fireEvent.click(screen.getByText('Skip for now'));
    await waitFor(() => expect(calls.some((c) => c.method === 'seed.dismiss' && (c.params as Record<string, unknown>).what === 'firstRun')).toBe(true));
  });

  it('opens by itself at the first start, not in the tests of the app unless they ask; the offer to wallets of before likewise', async () => {
    mount(SeedFlows);
    await waitFor(() => expect(document.querySelector('[data-wizard="firstrun"]')).toBeTruthy());
    expect(seed.open).toBe('firstrun');
  });
});

describe('moving wallets of before to a phrase', () => {
  it('"Not now" is remembered; a phrase made, each wallet of before with coins moved by a payment to yourself, asked each time', async () => {
    st = { ...st, firstRun: false, offer: true, hasWallets: true };
    await refreshSeed();
    const view = mount(MigrateWizard, { onclose: () => {} });
    await fireEvent.click(document.querySelector('[data-action="later"]') as HTMLButtonElement);
    await waitFor(() => expect(calls.some((c) => c.method === 'seed.dismiss' && (c.params as Record<string, unknown>).what === 'offer')).toBe(true));
    view.unmount();

    wallets.list = [{ name: 'main', encrypted: false, locked: false } as never, { name: 'seed', encrypted: true, locked: true } as never];
    wallets.balances = { main: { trusted: 150_000_000n, untrustedPending: 0n, immature: 0n } } as never;
    answers['wallet.newAddress'] = 'bcrt1qnew';
    answers['wallet.send.prepare'] = { token: 't1', fee: 141, vsize: 141, total: 150_000_000, recipients: [], txid: 'ab'.repeat(32) };
    answers['wallet.send.commit'] = { complete: true, txid: 'ab'.repeat(32) };
    answers['seed.create'] = () => {
      st = { ...st, exists: true, offer: false, chains: [{ id: 'chains', name: 'Chains', mode: 'phrase', state: 'ready', wallet: 'seed' }] };
      return { created: true };
    };
    mount(MigrateWizard, { onclose: () => {} });
    await fireEvent.click(document.querySelector('[data-action="next"]') as HTMLButtonElement);
    await fireEvent.click(document.querySelector('[data-action="make-phrase"]') as HTMLButtonElement);
    await waitFor(() => expect((document.querySelector('[data-action="next"]') as HTMLButtonElement).disabled).toBe(false));
    await fireEvent.click(document.querySelector('[data-action="next"]') as HTMLButtonElement);
    await waitFor(() => expect(document.querySelector('[data-old="chains/main"]')).toBeTruthy());
    // The pages use the wallet of the phrase from now on.
    expect(wallets.current).toBe('seed');
    await fireEvent.click(document.querySelector('[data-old="chains/main"] [data-action="move-old"]') as HTMLButtonElement);
    await waitFor(() => expect(document.querySelector('[data-old="chains/main"]')?.getAttribute('data-state')).toBe('moved'));
    // A new address of the new wallet; all of the old one's coins, its fee taken from them; asked (commit).
    expect(calls.find((c) => c.method === 'wallet.newAddress')?.params).toMatchObject({ wallet: 'seed' });
    const prepare = calls.find((c) => c.method === 'wallet.send.prepare')?.params as Record<string, unknown>;
    expect(prepare.wallet).toBe('main');
    expect(prepare.recipients).toEqual([{ address: 'bcrt1qnew', label: 'To your backup phrase wallet', amount: 150_000_000, subtractFee: true, message: '' }]);
    expect(methods()).toContain('wallet.send.commit');
    expect(methods()).not.toContain('seed.show');
  });
});

describe('moving coins, step by step', () => {
  it('a deposit: the direction, the sidechain, the amount, a check, then the app\'s own question; followed until credited', async () => {
    list.chains[0] = entry('layers', { enabled: true, state: 'running', ready: true });
    hub.list = structuredClone(list);
    wallets.current = 'main';
    wallets.balances = { main: { trusted: 500_000_000n, untrustedPending: 0n, immature: 0n } } as never;
    answers['chain.deposit'] = { txid: 'cd'.repeat(32), address: 'rsc1qdeposit' };
    answers['chain.transactions'] = { stem: 'layers', wallet: 'main', rows: [{ address: 'rsc1qdeposit', amount: '1.00000000', category: 'receive' }] };
    answers['chain.summary'] = { stem: 'layers', wallet: 'main', balances: { trusted: '0', pending: '0' } };
    mount(MoveWizard, { onclose: () => {} });
    const step = () => document.querySelector('[data-wizard="move"]')?.getAttribute('data-step');
    const next = () => document.querySelector('[data-action="next"]') as HTMLButtonElement;
    await fireEvent.click(next());
    expect(step()).toBe('chain');
    expect(next().disabled).toBe(true);
    await fireEvent.click(document.querySelector('[data-chain="layers"]') as HTMLButtonElement);
    await fireEvent.click(next());
    await fireEvent.input(document.querySelector('[data-move-amount]') as HTMLInputElement, { target: { value: '9' } });
    // More than held is not refused for a deposit here (the host and the node check); a wrong amount is.
    await fireEvent.input(document.querySelector('[data-move-amount]') as HTMLInputElement, { target: { value: '1.000000001' } });
    expect(next().disabled).toBe(true);
    await fireEvent.input(document.querySelector('[data-move-amount]') as HTMLInputElement, { target: { value: '1' } });
    await fireEvent.click(next());
    expect(step()).toBe('review');
    await fireEvent.click(document.querySelector('[data-action="send"]') as HTMLButtonElement);
    await waitFor(() => expect(step()).toBe('sent'));
    expect(calls.find((c) => c.method === 'chain.deposit')?.params).toMatchObject({ stem: 'layers', amount: '1' });
    await waitFor(() => expect(document.querySelector('[data-credited="true"]')).toBeTruthy());
  });

  it('a withdrawal: back to this app\'s chain, an address of its wallet filled in, the miners\' fee said', async () => {
    list.chains[0] = entry('layers', { enabled: true, state: 'running', ready: true });
    hub.list = structuredClone(list);
    wallets.current = 'main';
    answers['wallet.newAddress'] = 'bcrt1qback';
    answers['chain.summary'] = { stem: 'layers', wallet: 'main', balances: { trusted: '5.00000000', pending: '0' } };
    answers['chain.withdraw'] = { txid: 'ef'.repeat(32) };
    answers['chain.peg'] = { stem: 'layers', wallet: 'main', mainchain: {}, bmm: {}, withdrawals: [{ txid: 'ef'.repeat(32), vout: 0, amount: '1', status: 'waiting for a bundle', mine: true }] };
    mount(MoveWizard, { onclose: () => {} });
    await fireEvent.click(document.querySelector('[data-direction="out"]') as HTMLButtonElement);
    await fireEvent.click(document.querySelector('[data-action="next"]') as HTMLButtonElement);
    await fireEvent.click(document.querySelector('[data-chain="layers"]') as HTMLButtonElement);
    await fireEvent.click(document.querySelector('[data-action="next"]') as HTMLButtonElement);
    await waitFor(() => expect((document.querySelector('[data-move-address]') as HTMLInputElement).value).toBe('bcrt1qback'));
    await fireEvent.input(document.querySelector('[data-move-amount]') as HTMLInputElement, { target: { value: '1' } });
    await fireEvent.click(document.querySelector('[data-action="next"]') as HTMLButtonElement);
    expect(document.body.textContent).toContain('Fee for the miners');
    await fireEvent.click(document.querySelector('[data-action="send"]') as HTMLButtonElement);
    await waitFor(() => expect(document.querySelector('[data-wizard="move"]')?.getAttribute('data-step')).toBe('sent'));
    expect(calls.find((c) => c.method === 'chain.withdraw')?.params).toMatchObject({ stem: 'layers', address: 'bcrt1qback', amount: '1', fee: '0.0001' });
    await waitFor(() => expect(document.body.textContent).toContain('waiting for a bundle'));
  });
});

describe('Settings → Wallet → Backup phrase, and turning a sidechain on', () => {
  it('lists each chain\'s wallet; showing the phrase and changing the passphrase are the host\'s, no word here', async () => {
    st = { ...st, exists: true, firstRun: false, words: 12, created: 1_792_600_000, fingerprint: '73c5da0a', chains: [{ id: 'chains', name: 'Chains', mode: 'phrase', state: 'ready', wallet: 'main' }, { id: 'layers', name: 'Layers', mode: 'phrase', state: 'needs-keys' }, { id: 'thunder', name: 'Thunder', mode: 'separate', state: 'own', wallet: 'main' }] };
    answers['seed.show'] = { shown: true };
    answers['seed.addChain'] = { unlocked: true };
    mount(SeedSettings);
    await waitFor(() => expect(document.querySelector('[data-seed-chains] li[data-chain="layers"]')).toBeTruthy());
    expect(document.body.textContent).toContain('Phrase 73c5da0a');
    await fireEvent.click(document.querySelector('[data-make-keys="layers"]') as HTMLButtonElement);
    await waitFor(() => expect(calls.find((c) => c.method === 'seed.addChain')?.params).toEqual({ stem: 'layers' }));
    await fireEvent.click(document.querySelector('[data-action="seed-show"]') as HTMLButtonElement);
    await waitFor(() => expect(methods()).toContain('seed.show'));
    expect(document.querySelector('[data-seed-chains] li[data-chain="thunder"]')?.textContent).toContain('back it up on its own');
  });

  it('with a phrase, a sidechain\'s keys come from it: the passphrase asked once before it is turned on; declined, not turned on', async () => {
    st = { ...st, exists: true, firstRun: false, unlocked: false };
    await refreshSeed();
    answers['seed.unlock'] = { cancelled: true };
    mount(EnableWizard, { entry: entry('layers'), onclose: () => {} });
    const next = () => document.querySelector('[data-action="next"]') as HTMLButtonElement;
    for (let i = 0; i < 3; i++) await fireEvent.click(next());
    expect(document.querySelector('[data-wizard="enable"]')?.getAttribute('data-step')).toBe('wallet');
    expect(document.body.textContent).toContain('you type your wallet passphrase once');
    await fireEvent.click(next());
    await fireEvent.click(document.querySelector('[data-action="turn-on"]') as HTMLButtonElement);
    await waitFor(() => expect(methods()).toContain('seed.unlock'));
    expect(methods()).not.toContain('sidechains.enable');
    answers['seed.unlock'] = { unlocked: true };
    await fireEvent.click(document.querySelector('[data-action="turn-on"]') as HTMLButtonElement);
    await waitFor(() => expect(methods()).toContain('sidechains.enable'));
    expect(calls.filter((c) => c.method === 'seed.unlock').map((c) => c.params)).toEqual([{ stems: ['layers'] }, { stems: ['layers'] }]);
  });
});
