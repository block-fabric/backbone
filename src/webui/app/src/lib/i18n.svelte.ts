// The language of the interface. The messages come from the catalogs of chains-qt (made into
// public/locales by scripts/i18n.mjs), as the host gives them (app.locale).
//
// The interface is written in English, and only its own words are translated: translation is opt-in.
// - The text written in the templates of the components (static text, and the title, placeholder,
//   aria-label and alt attributes, with what is put in them) is made into calls of t() when the
//   components are compiled (scripts/i18n-preprocess.mjs). Only the template is looked up in the
//   catalog; the values put in it are spliced in as they are, never translated nor matched.
// - Words of the app held in code (the labels of tabs and menus, the titles of dialogs) are translated
//   where they are shown, with t() or tn().
// - Text from the chain, peers, the wallet or the user is never translated: it is shown with SafeText
//   (components/SafeText.svelte, translate="no"), and a test (i18n-lint.test.ts) finds the places that
//   show such fields without it.
// Nothing looks at the page (no MutationObserver): a label that reads like a message of the app is shown
// as it is, in every language.
//
// Messages with placeholders ("%1 behind", "%n GB of space available") are matched by a token matcher,
// not a regular expression: the literal parts are found with indexOf from left to right, without
// backtracking, in text of at most MAX_MATCHED characters.
import { on, request } from './bridge';

export type Message = string | string[];

export interface Locale {
  code: string;
  name: string;
  rtl: boolean;
  messages: Record<string, Message>;
}

/** The language shown ('' for English), for the parts of the interface that follow it. */
export const i18n = $state({ code: '', name: 'English', rtl: false, version: 0 });

/** The longest text tried against the messages with placeholders. */
export const MAX_MATCHED = 300;

interface Pattern {
  source: string;
  /** The literal parts of the message, around its placeholders: holders.length + 1 of them. */
  parts: string[];
  /** The placeholders of the message, in the order they come in it ("%1", "%n"...). */
  holders: string[];
  value: Message;
}

let exact = new Map<string, Message>();
/** The same, by the message in lower case: the interface writes "Pay to" where chains-qt wrote "Pay To". */
let folded = new Map<string, Message>();
let patterns: Pattern[] = [];
let bySource = new Map<string, Pattern>();

/** What a template of the app becomes: the message found and what its placeholders hold (in the template). */
interface Resolved {
  value: Message;
  values: Record<string, string>;
}
const templateCache = new Map<string, Resolved | null>();
const textCache = new Map<string, string | null>();

const HOLDER = /%[0-9n]/g;
/** A number as the interface writes it, with its separators (for %n). Linear: no nested repetition. */
const NUMBER = /^-?\d[\d   ,.]*$/;

/** Use a catalog (null: English). */
export function setCatalog(locale: Locale | null): void {
  exact = new Map();
  folded = new Map();
  patterns = [];
  bySource = new Map();
  templateCache.clear();
  textCache.clear();
  i18n.code = locale?.code ?? '';
  i18n.name = locale?.name ?? 'English';
  i18n.rtl = !!locale?.rtl;
  for (const [source, value] of Object.entries(locale?.messages ?? {})) {
    const holders = source.match(HOLDER) ?? [];
    if (holders.length === 0) {
      if (typeof value === 'string') {
        exact.set(source, value);
        const key = source.toLowerCase();
        if (!folded.has(key)) folded.set(key, value);
      }
      continue;
    }
    const parts = source.split(HOLDER);
    // Two placeholders side by side cannot be told apart in a text: such a message is only used by its source.
    const pattern = { source, parts, holders, value };
    bySource.set(source, pattern);
    if (parts.slice(1, -1).every((p) => p !== '')) patterns.push(pattern);
  }
  // The messages with the most literal text first: "%1 behind" must not take what a longer one would.
  const literal = (p: Pattern) => p.parts.reduce((n, s) => n + s.length, 0);
  patterns.sort((a, b) => literal(b) - literal(a));
  i18n.version++;
}

const CANONICAL = ['zero', 'one', 'two', 'few', 'many', 'other'];

/** Which of the plural forms of a message goes with the number, in the order of the CLDR categories of the language. */
export function pluralIndex(code: string, n: number, count: number): number {
  if (count <= 1) return 0;
  let rules: Intl.PluralRules;
  try {
    rules = new Intl.PluralRules(code.replace('_', '-').replace(/@.*$/, ''));
  } catch {
    return n === 1 ? 0 : Math.min(1, count - 1);
  }
  const categories = CANONICAL.filter((c) => rules.resolvedOptions().pluralCategories.includes(c as Intl.LDMLPluralRule));
  const index = categories.indexOf(rules.select(n));
  return Math.max(0, Math.min(index, count - 1));
}

/** Put the values in place of the placeholders, in one pass: what the values hold is not looked at again. */
function fill(text: string, values: Record<string, string>): string {
  return text.replace(HOLDER, (h) => values[h] ?? h);
}

function numberOf(text: string): number {
  return Number(text.replace(/[^\d-]/g, ''));
}

/**
 * The values of the placeholders of a message in a text, or null when the text is not that message.
 * The literal parts are found from left to right (the first place each fits), with no going back: the
 * time is linear in the length of the text.
 */
export function matchMessage(text: string, parts: string[]): string[] | null {
  const first = parts[0];
  const last = parts[parts.length - 1];
  const holders = parts.length - 1;
  if (text.length < first.length + last.length + holders) return null;
  if (!text.startsWith(first) || !text.endsWith(last)) return null;
  const values: string[] = [];
  let pos = first.length;
  const end = text.length - last.length;
  for (let i = 1; i < parts.length; i++) {
    let next: number;
    if (i === parts.length - 1) {
      next = end;
    } else {
      next = text.indexOf(parts[i], pos + 1);
      if (next < 0 || next + parts[i].length > end) return null;
    }
    // Every placeholder holds something.
    if (next <= pos) return null;
    values.push(text.slice(pos, next));
    pos = next + parts[i].length;
  }
  return values;
}

function choose(value: Message, n: () => number): string {
  return Array.isArray(value) ? value[pluralIndex(i18n.code, n(), value.length)] : value;
}

/** A message of the catalog that a text of the app is, with what its placeholders hold. */
function resolve(text: string, numeric: (value: string) => boolean): Resolved | null {
  const direct = exact.get(text) ?? (/\s/.test(text) ? folded.get(text.toLowerCase()) : undefined);
  if (typeof direct === 'string') return { value: direct, values: {} };
  const own = bySource.get(text);
  if (own) return { value: own.value, values: Object.fromEntries(own.holders.map((h) => [h, h])) };
  if (text.length > MAX_MATCHED) return null;
  for (const p of patterns) {
    const found = matchMessage(text, p.parts);
    if (!found) continue;
    const values: Record<string, string> = {};
    p.holders.forEach((h, i) => (values[h] = found[i]));
    if (values['%n'] !== undefined && !numeric(values['%n'])) continue;
    return { value: p.value, values };
  }
  return null;
}

/**
 * The translation of a text that the app made itself (the message of a dialog, a notice), or null when it
 * is no message of the catalog. Never for text from the chain, peers or the user. Text longer than
 * MAX_MATCHED is only looked up as a whole.
 */
export function translateText(text: string): string | null {
  if (exact.size === 0 && patterns.length === 0) return null;
  const cached = textCache.get(text);
  if (cached !== undefined) return cached;
  const found = resolve(text, (v) => NUMBER.test(v.trim()));
  const out = found ? fill(choose(found.value, () => numberOf(found.values['%n'] ?? '0')), found.values) : null;
  if (textCache.size > 5000) textCache.clear();
  textCache.set(text, out);
  return out;
}

/** translateText, or the text itself. */
export function tx(text: string): string {
  void i18n.version;
  return translateText(text) ?? text;
}

function template(source: string): Resolved | null {
  const cached = templateCache.get(source);
  if (cached !== undefined) return cached;
  // A template of the app: its placeholders (%1, %2...) stand for values; the number of %n is in them.
  const found = exact.size === 0 && patterns.length === 0 ? null : resolve(source, () => true);
  // Templates of the app are few; words of the app put together in code (t(label)) can be more.
  if (templateCache.size > 5000) templateCache.clear();
  templateCache.set(source, found);
  return found;
}

/**
 * A message of the app in the language shown, with %1, %2... filled in. The source is looked up in the
 * catalog (also as a part of a longer message of the catalog: "%1 hours behind" is "%1 behind"); the
 * values are put in as they are.
 */
export function t(source: string, ...args: unknown[]): string {
  void i18n.version;
  const values: Record<string, string> = Object.fromEntries(args.map((a, i) => [`%${i + 1}`, a === null || a === undefined ? '' : String(a)]));
  const found = template(source);
  if (!found) return fill(source, values);
  const form = choose(found.value, () => numberOf(fill(found.values['%n'] ?? '0', values)));
  return fill(fill(form, found.values), values);
}

/** A message with plural forms (%n), in the language shown. */
export function tn(source: string, n: number, ...args: unknown[]): string {
  void i18n.version;
  const values: Record<string, string> = { '%n': String(n), ...Object.fromEntries(args.map((a, i) => [`%${i + 1}`, a === null || a === undefined ? '' : String(a)])) };
  const found = template(source);
  if (!found) return fill(source, values);
  const form = choose(found.value, () => n);
  return fill(fill(form, found.values), values);
}

/** Show the page in the language of the catalog now (after setCatalog): its language and direction. */
export function refreshPage(): void {
  if (typeof document === 'undefined') return;
  document.documentElement.lang = i18n.code ? i18n.code.replace('_', '-') : 'en';
  document.documentElement.dir = i18n.rtl ? 'rtl' : 'ltr';
}

/** The language of the host, now; and again whenever it changes (the language option, applied). */
export async function loadLocale(): Promise<void> {
  try {
    const locale = await request<Locale>('host', 'app.locale', {});
    setCatalog(locale.code ? locale : null);
  } catch {
    setCatalog(null);
  }
  refreshPage();
}

let following = false;
export function followLocale(): void {
  if (following) return;
  following = true;
  on('locale', () => {
    loadLocale();
  });
}
