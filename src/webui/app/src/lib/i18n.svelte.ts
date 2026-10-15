// The language of the interface. The messages come from the catalogs of chains-qt (made into
// public/locales by scripts/i18n.mjs), as the host gives them (app.locale).
//
// The interface is written in English. What it shows is translated where it is shown: a translator
// follows the page (a MutationObserver) and puts in place of each text, tooltip, placeholder and label
// that is a message of the catalog its translation, also when the message has numbers or names in it
// ("%1 behind", "%n GB of space available"). Text from the chain, the wallet or the user is never
// translated: it is shown inside elements marked translate="no". Code that builds text itself can use
// t() and tn().
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

interface Pattern {
  source: string;
  re: RegExp;
  /** The placeholders of the message, in the order they come in it ("%1", "%n"...). */
  holders: string[];
  value: Message;
}

let exact = new Map<string, Message>();
/** The same, by the message in lower case: the interface writes "Pay to" where chains-qt wrote "Pay To". */
let folded = new Map<string, Message>();
let patterns: Pattern[] = [];
const cache = new Map<string, string | null>();

function escapeRe(s: string): string {
  return s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

/** Use a catalog (null: English). */
export function setCatalog(locale: Locale | null): void {
  exact = new Map();
  folded = new Map();
  patterns = [];
  cache.clear();
  i18n.code = locale?.code ?? '';
  i18n.name = locale?.name ?? 'English';
  i18n.rtl = !!locale?.rtl;
  for (const [source, value] of Object.entries(locale?.messages ?? {})) {
    if (!/%[0-9n]/.test(source)) {
      if (typeof value === 'string') {
        exact.set(source, value);
        const key = source.toLowerCase();
        if (!folded.has(key)) folded.set(key, value);
      }
      continue;
    }
    const holders: string[] = [];
    const re = source
      .split(/(%[0-9n])/)
      .map((part) => {
        if (/^%[0-9n]$/.test(part)) {
          holders.push(part);
          return part === '%n' ? '(-?\\d[\\d\\u202f\\u00a0 ,.]*)' : '(.+?)';
        }
        return escapeRe(part);
      })
      .join('');
    patterns.push({ source, re: new RegExp(`^${re}$`, 's'), holders, value });
  }
  // The longer messages first: "%1 behind" must not take what a longer one would.
  patterns.sort((a, b) => b.re.source.length - a.re.source.length);
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

function fill(text: string, values: Record<string, string>): string {
  return text.replace(/%[0-9n]/g, (h) => values[h] ?? h);
}

function numberOf(text: string): number {
  return Number(text.replace(/[^\d-]/g, ''));
}

/** The translation of a text the page shows, or null when it is no message of the catalog. */
export function translateText(text: string): string | null {
  if (exact.size === 0 && patterns.length === 0) return null;
  const cached = cache.get(text);
  if (cached !== undefined) return cached;
  let out: string | null = null;
  const direct = exact.get(text) ?? (/\s/.test(text) ? folded.get(text.toLowerCase()) : undefined);
  if (typeof direct === 'string') {
    out = direct;
  } else {
    for (const p of patterns) {
      const m = text.match(p.re);
      if (!m) continue;
      const values: Record<string, string> = {};
      p.holders.forEach((h, i) => (values[h] = m[i + 1]));
      const form = Array.isArray(p.value) ? p.value[pluralIndex(i18n.code, numberOf(values['%n'] ?? '0'), p.value.length)] : p.value;
      out = fill(form, values);
      break;
    }
  }
  if (cache.size > 20000) cache.clear();
  cache.set(text, out);
  return out;
}

/** A message in the language shown, with %1, %2... filled in. */
export function t(source: string, ...args: (string | number)[]): string {
  void i18n.version;
  const value = exact.get(source) ?? patterns.find((p) => p.source === source)?.value;
  const text = typeof value === 'string' ? value : source;
  return fill(text, Object.fromEntries(args.map((a, i) => [`%${i + 1}`, String(a)])));
}

/** A message with plural forms (%n), in the language shown. */
export function tn(source: string, n: number, ...args: (string | number)[]): string {
  void i18n.version;
  const values = { '%n': String(n), ...Object.fromEntries(args.map((a, i) => [`%${i + 1}`, String(a)])) };
  const pattern = patterns.find((p) => p.source === source);
  if (pattern && Array.isArray(pattern.value)) return fill(pattern.value[pluralIndex(i18n.code, n, pattern.value.length)], values);
  return translateText(fill(source, values)) ?? fill(source, values);
}

// --- The translator of the page ---

const ATTRIBUTES = ['title', 'placeholder', 'aria-label', 'alt'];
/** Elements whose text is never translated (code, the text typed in a field); their attributes still are. */
const SKIP = new Set(['SCRIPT', 'STYLE', 'TEXTAREA', 'CODE', 'PRE']);
/** The English a text node or an attribute showed, before it was translated. */
const textOriginal = new WeakMap<Node, string>();
const textWritten = new WeakMap<Node, string>();
const attrOriginal = new WeakMap<Element, Map<string, string>>();
const attrWritten = new WeakMap<Element, Map<string, string>>();
let observer: MutationObserver | null = null;

/** Whether the text of an element is not to be translated. */
function excluded(el: Element | null): boolean {
  for (let e = el; e; e = e.parentElement) {
    if (SKIP.has(e.tagName) || e.getAttribute('translate') === 'no' || (e as HTMLElement).isContentEditable) return true;
  }
  return false;
}

/** Whether the attributes of an element are not to be translated (only translate="no" says so). */
function excludedAttributes(el: Element): boolean {
  for (let e: Element | null = el; e; e = e.parentElement) if (e.getAttribute('translate') === 'no' || e.tagName === 'SCRIPT' || e.tagName === 'STYLE') return true;
  return false;
}

function translateTextNode(node: Text): void {
  const current = node.nodeValue ?? '';
  if (textWritten.get(node) === current) return;
  // New text from the page: English, as it is written.
  textOriginal.set(node, current);
  apply(node, current);
}

function apply(node: Text, original: string): void {
  const core = original.trim();
  let next = original;
  if (core && /\p{L}/u.test(core)) {
    const tr = translateText(core);
    if (tr !== null) next = original.replace(core, tr);
  }
  if (node.nodeValue !== next) {
    textWritten.set(node, next);
    node.nodeValue = next;
  } else {
    textWritten.set(node, next);
  }
}

function translateAttribute(el: Element, name: string): void {
  const current = el.getAttribute(name);
  if (current === null) return;
  if (attrWritten.get(el)?.get(name) === current) return;
  let originals = attrOriginal.get(el);
  if (!originals) attrOriginal.set(el, (originals = new Map()));
  originals.set(name, current);
  applyAttribute(el, name, current);
}

function applyAttribute(el: Element, name: string, original: string): void {
  const tr = original.trim() ? translateText(original.trim()) : null;
  const next = tr ?? original;
  let written = attrWritten.get(el);
  if (!written) attrWritten.set(el, (written = new Map()));
  written.set(name, next);
  if (el.getAttribute(name) !== next) el.setAttribute(name, next);
}

function walk(root: Node): void {
  if (root.nodeType === Node.TEXT_NODE) {
    if (!excluded(root.parentElement)) translateTextNode(root as Text);
    return;
  }
  if (root.nodeType !== Node.ELEMENT_NODE) return;
  const el = root as Element;
  if (excludedAttributes(el)) return;
  for (const a of ATTRIBUTES) if (el.hasAttribute(a)) translateAttribute(el, a);
  for (const child of el.childNodes) walk(child);
}

/** Translate everything again from the English it showed (the language changed). */
function retranslate(root: Node): void {
  if (root.nodeType === Node.TEXT_NODE) {
    const original = textOriginal.get(root);
    if (original !== undefined && !excluded(root.parentElement)) apply(root as Text, original);
    else if (!excluded(root.parentElement)) translateTextNode(root as Text);
    return;
  }
  if (root.nodeType !== Node.ELEMENT_NODE) return;
  const el = root as Element;
  if (excludedAttributes(el)) return;
  const originals = attrOriginal.get(el);
  for (const a of ATTRIBUTES) {
    const original = originals?.get(a);
    if (original !== undefined && el.getAttribute(a) === attrWritten.get(el)?.get(a)) applyAttribute(el, a, original);
    else if (el.hasAttribute(a)) translateAttribute(el, a);
  }
  for (const child of el.childNodes) retranslate(child);
}

/** Follow the page and translate it (from now on, and what it shows already). */
export function installTranslator(root: Element = document.body): void {
  if (typeof MutationObserver === 'undefined') return;
  observer?.disconnect();
  observer = new MutationObserver((records) => {
    if (exact.size === 0 && patterns.length === 0) return;
    for (const r of records) {
      if (r.type === 'characterData') {
        if (!excluded(r.target.parentElement)) translateTextNode(r.target as Text);
      } else if (r.type === 'attributes') {
        if (r.attributeName && !excludedAttributes(r.target as Element)) translateAttribute(r.target as Element, r.attributeName);
      } else {
        for (const n of r.addedNodes) walk(n);
      }
    }
  });
  observer.observe(root, { subtree: true, childList: true, characterData: true, attributes: true, attributeFilter: ATTRIBUTES });
  retranslate(root);
}

/** Show the page in the language of the catalog now (after setCatalog). */
export function refreshPage(root: Element = document.body): void {
  if (typeof document === 'undefined') return;
  document.documentElement.lang = i18n.code ? i18n.code.replace('_', '-') : 'en';
  document.documentElement.dir = i18n.rtl ? 'rtl' : 'ltr';
  retranslate(root);
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
