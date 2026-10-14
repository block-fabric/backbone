// The interface in the languages of chains-qt (audit M6): the catalogs, the messages with numbers and
// plural forms in them, and the page translated where it shows them, but never the text of the user.
import { execFileSync } from 'node:child_process';
import { existsSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { afterEach, describe, expect, it } from 'vitest';
import { i18n, installTranslator, pluralIndex, refreshPage, setCatalog, t, tn, translateText } from '../lib/i18n.svelte';

const APP = join(__dirname, '../..');
const DE = { code: 'de', name: 'Deutsch', rtl: false, messages: { Send: 'Überweisen', 'Mask values': 'Werte ausblenden', '%1 behind': '%1 im Rückstand', 'Enter address or label to search': 'Zu suchende Adresse oder Bezeichnung eingeben', '%n GB of space available': ['%n GB freier Speicherplatz verfügbar', '%n GB freier Speicherplatz verfügbar'] } };
const RU = { code: 'ru', name: 'русский', rtl: false, messages: { 'Processed %n block(s) of transaction history.': ['Обработан %n блок', 'Обработано %n блока', 'Обработано %n блоков', 'Обработано %n блоков'] } };

afterEach(() => {
  setCatalog(null);
  document.body.innerHTML = '';
});

describe('the catalogs', () => {
  it('are made from the catalogs of chains-qt, and are up to date', () => {
    const index = JSON.parse(readFileSync(join(APP, 'public/locales/index.json'), 'utf8')) as { code: string; name: string; messages: number }[];
    expect(index.length).toBeGreaterThan(90);
    const de = JSON.parse(readFileSync(join(APP, 'public/locales/de.json'), 'utf8'));
    expect(de.name).toBe('Deutsch');
    expect(Object.keys(de.gui).length).toBeGreaterThan(100);
    // Normalized as the interface writes its messages: no mnemonics, an ellipsis, Chains.
    expect(Object.keys(de.gui).some((k) => /&(?=\S)|\.\.\./.test(k))).toBe(false);
    expect(Object.keys(de.gui).some((k) => /Bitcoin/.test(k))).toBe(false);
    expect(Object.keys(de.core).length).toBeGreaterThan(50);
    if (existsSync(join(APP, '../../qt/locale'))) {
      // Made again from the catalogs and the sources, nothing changes.
      execFileSync(process.execPath, [join(APP, 'scripts/i18n.mjs'), '--check'], { stdio: 'pipe' });
    }
  });
});

describe('a language', () => {
  it('translates messages, with what is in them', () => {
    setCatalog(DE);
    expect(translateText('Send')).toBe('Überweisen');
    expect(translateText('3 hours behind')).toBe('3 hours im Rückstand');
    expect(translateText('12 GB of space available')).toBe('12 GB freier Speicherplatz verfügbar');
    expect(translateText('Nothing of the catalog')).toBeNull();
    expect(t('Mask values')).toBe('Werte ausblenden');
    expect(t('%1 behind', '2 days')).toBe('2 days im Rückstand');
    expect(i18n.code).toBe('de');
  });

  it('chooses plural forms by the rules of the language', () => {
    setCatalog(RU);
    expect(pluralIndex('ru', 1, 4)).toBe(0);
    expect(pluralIndex('ru', 3, 4)).toBe(1);
    expect(pluralIndex('ru', 7, 4)).toBe(2);
    expect(tn('Processed %n block(s) of transaction history.', 21)).toBe('Обработан 21 блок');
    expect(translateText('Processed 5 block(s) of transaction history.')).toBe('Обработано 5 блоков');
    expect(pluralIndex('de', 1, 2)).toBe(0);
    expect(pluralIndex('de', 2, 2)).toBe(1);
  });

  it('translates the page as it shows, but not the text of the user', async () => {
    setCatalog(DE);
    document.body.innerHTML = `<main><button title="Mask values">Send</button><input placeholder="Enter address or label to search"><span translate="no">Send</span><p> Send </p></main>`;
    installTranslator(document.body);
    expect(document.querySelector('button')!.textContent).toBe('Überweisen');
    expect(document.querySelector('button')!.title).toBe('Werte ausblenden');
    expect((document.querySelector('input') as HTMLInputElement).placeholder).toBe('Zu suchende Adresse oder Bezeichnung eingeben');
    expect(document.querySelector('span')!.textContent).toBe('Send');
    expect(document.querySelector('p')!.textContent).toBe(' Überweisen ');
    // What the page shows next is translated too (Svelte changes text nodes and adds elements).
    const text = document.querySelector('p')!.firstChild as Text;
    text.nodeValue = '5 hours behind';
    document.querySelector('main')!.insertAdjacentHTML('beforeend', '<b>Mask values</b>');
    await new Promise((resolve) => setTimeout(resolve, 0));
    expect(document.querySelector('p')!.textContent).toBe('5 hours im Rückstand');
    expect(document.querySelector('b')!.textContent).toBe('Werte ausblenden');
    // Back to English: what the page showed comes back.
    setCatalog(null);
    refreshPage();
    expect(document.querySelector('button')!.textContent).toBe('Send');
    expect(document.querySelector('b')!.textContent).toBe('Mask values');
    expect(document.documentElement.lang).toBe('en');
    setCatalog({ code: 'ar', name: 'العربية', rtl: true, messages: {} });
    refreshPage();
    expect(document.documentElement.dir).toBe('rtl');
  });
});
