// The sidechains as modules of the app (doc/unified-app.md §14): one window, one sidebar, one header, one status bar,
// one bell, one palette. A module is a sidechain the app runs: switching to it (the module switcher of the sidebar)
// puts its pages in the sidebar and shows them in the main area, in the app's look. Its pages still run apart, in a
// frame of their own origin (lib/packs.svelte.ts, §13.5 #10) that brings no chrome: they tell the app what they have
// (their manifest), where they are (route), what to say (toast, notice), what the wallet holds (summary), and the app
// tells them its look and the page to show. Everything a frame says is checked here; nothing of it is trusted.
import { app, prefs, toast, type Toast } from './state.svelte';
import { TOKENS } from './customthemes';
import { hub } from './sidechains.svelte';
import { go, nav } from './nav.svelte';
import { t } from './i18n.svelte';
import { ICONS, type IconName } from '../components/Icon.svelte';
import { UNITS } from './units';
import type { PackInfo } from './packs.svelte';
import type { SidechainEntry, SidechainOverview } from './unified';

/** The name of this app and of its coin, as the build names them (the product is renamed in one place). */
export function productName(): string {
  return app.info?.name || 'Chains';
}
export function productUnit(): string {
  return UNITS[0]?.short ?? '';
}

export interface ModulePage {
  id: string;
  title: string;
  icon: string;
  iconPath: string;
  section: string;
  summary: string;
  wallet: boolean;
  shortcut?: string;
}

export interface ModuleManifest {
  name: string;
  unit: string;
  home: string;
  sections: { title: string; pages: string[] }[];
  pages: ModulePage[];
  commands: { id: string; label: string; page: string; tab?: string }[];
}

export interface ModuleAsset {
  name: string;
  amount: string;
}

export interface ModuleSummary {
  wallet: string;
  trusted: string | null;
  pending: string | null;
  assets: ModuleAsset[];
  /** When it came (ms). */
  at: number;
}

export interface ModuleNotice {
  id: number;
  stem: string;
  kind: 'tx' | 'system';
  title: string;
  text: string;
  /** Unix seconds. */
  time: number;
  unread: boolean;
  wallet?: string;
  txid?: string;
  /** A notice of the app itself about the module (an update): opens Settings → Sidechains. */
  app?: boolean;
}

/** The pages the app adds to every module, before the frame's: how its node is doing (the overview of phase 1). */
export const NODE_PAGE = '@node';

export const modules = $state({
  manifests: {} as Record<string, ModuleManifest>,
  /** The page each module shows (kept when switching away and back). */
  routes: {} as Record<string, { page: string; tab: string }>,
  summaries: {} as Record<string, ModuleSummary>,
  notices: [] as ModuleNotice[],
  /** A dialog of the module's pages is open: the app dims around the page. */
  modal: {} as Record<string, boolean>,
  /** Whether each module's pages can be shown (pack.info): built in, and its node of this app's version. */
  info: {} as Record<string, PackInfo>,
  /** How each module shown lately is doing (the status bar, the portfolio). */
  overview: {} as Record<string, SidechainOverview>,
});

/** The modules whose frames are kept: turned on, pages built in and of this app's version. */
export function liveModules(): SidechainEntry[] {
  return (hub.list?.chains ?? []).filter((c) => (c.enabled || c.state === 'stopping') && !!modules.info[c.stem]?.pack && modules.info[c.stem]?.compatible);
}

/** Whether the module shown is in its frame now (else the app's page: how its node is doing, or not ready yet). */
export function frameShown(): boolean {
  const stem = activeModule();
  if (!stem) return false;
  const c = hub.list?.chains.find((x) => x.stem === stem);
  if (!c || c.state !== 'running' || !c.ready) return false;
  if (!modules.info[stem]?.pack || !modules.info[stem]?.compatible) return false;
  return moduleRoute(stem).page !== NODE_PAGE;
}

// ---- What a frame may say: checked, bounded, plain text ----

const STEM = /^[a-z0-9]{1,32}$/;
const PAGE_ID = /^[a-z0-9][a-z0-9-]{0,39}$/;
const TAB = /^[a-z0-9/_-]{0,60}$/;
/** An SVG path: commands and numbers only (it is put in a d attribute, nothing else). */
const PATH = /^[MmLlHhVvCcSsQqTtAaZz0-9 .,eE+-]{0,2000}$/;
/** Text of a frame: a bounded line, no control or format characters (bidi marks that turn text around). */
function line(v: unknown, max: number): string | null {
  if (typeof v !== 'string') return null;
  // eslint-disable-next-line no-control-regex
  const clean = v.replace(/[\u0000-\u001f\u007f-\u009f​-‏‪-‮⁦-⁩﻿]/g, ' ').trim();
  return clean.length <= max ? clean : `${clean.slice(0, max - 1)}…`;
}

export function parseManifest(data: unknown): ModuleManifest | null {
  if (!data || typeof data !== 'object') return null;
  const d = data as Record<string, unknown>;
  if (!Array.isArray(d.pages) || !Array.isArray(d.sections)) return null;
  const pages: ModulePage[] = [];
  for (const raw of d.pages.slice(0, 40)) {
    if (!raw || typeof raw !== 'object') continue;
    const p = raw as Record<string, unknown>;
    if (typeof p.id !== 'string' || !PAGE_ID.test(p.id) || pages.some((x) => x.id === p.id)) continue;
    const title = line(p.title, 48);
    if (!title) continue;
    const icon = typeof p.icon === 'string' && p.icon in ICONS ? p.icon : 'dot';
    const iconPath = typeof p.iconPath === 'string' && PATH.test(p.iconPath) ? p.iconPath : '';
    const shortcut = typeof p.shortcut === 'string' && /^Alt\+[0-9]$/.test(p.shortcut) ? p.shortcut : undefined;
    pages.push({ id: p.id, title, icon, iconPath, section: line(p.section, 32) ?? '', summary: line(p.summary, 200) ?? '', wallet: p.wallet === true, ...(shortcut ? { shortcut } : {}) });
  }
  if (pages.length === 0) return null;
  const known = new Set(pages.map((p) => p.id));
  const sections: ModuleManifest['sections'] = [];
  for (const raw of d.sections.slice(0, 12)) {
    if (!raw || typeof raw !== 'object') continue;
    const s = raw as Record<string, unknown>;
    if (!Array.isArray(s.pages)) continue;
    const ids = s.pages.filter((id): id is string => typeof id === 'string' && known.has(id)).slice(0, 40);
    if (ids.length) sections.push({ title: line(s.title, 32) ?? '', pages: ids });
  }
  const commands: ModuleManifest['commands'] = [];
  if (Array.isArray(d.commands)) {
    for (const raw of d.commands.slice(0, 60)) {
      if (!raw || typeof raw !== 'object') continue;
      const c = raw as Record<string, unknown>;
      const label = line(c.label, 64);
      if (typeof c.id !== 'string' || !/^[a-z0-9._-]{1,40}$/.test(c.id) || !label || typeof c.page !== 'string' || !PAGE_ID.test(c.page)) continue;
      const tab = typeof c.tab === 'string' && TAB.test(c.tab) ? c.tab : undefined;
      commands.push({ id: c.id, label, page: c.page, ...(tab ? { tab } : {}) });
    }
  }
  const home = typeof d.home === 'string' && known.has(d.home) ? d.home : pages[0].id;
  return { name: line(d.name, 32) ?? '', unit: line(d.unit, 12) ?? '', home, sections, pages, commands };
}

const AMOUNT = /^-?[0-9]{1,20}(\.[0-9]{1,18})?$/;

export function parseSummary(data: unknown): ModuleSummary | null {
  if (!data || typeof data !== 'object') return null;
  const d = data as Record<string, unknown>;
  const amount = (v: unknown) => (typeof v === 'string' && AMOUNT.test(v) ? v : null);
  const assets: ModuleAsset[] = [];
  if (Array.isArray(d.assets)) {
    for (const raw of d.assets.slice(0, 20)) {
      if (!raw || typeof raw !== 'object') continue;
      const a = raw as Record<string, unknown>;
      const name = line(a.name, 40);
      const value = line(a.amount, 40);
      if (name && value) assets.push({ name, amount: value });
    }
  }
  return { wallet: line(d.wallet, 64) ?? '', trusted: amount(d.trusted), pending: amount(d.pending), assets, at: Date.now() };
}

export function parseRoute(data: unknown): { page: string; tab: string } | null {
  if (!data || typeof data !== 'object') return null;
  const d = data as Record<string, unknown>;
  if (typeof d.page !== 'string' || !PAGE_ID.test(d.page)) return null;
  const tab = typeof d.tab === 'string' && TAB.test(d.tab) ? d.tab : '';
  return { page: d.page, tab };
}

const TOAST_KINDS = new Set(['info', 'success', 'warning', 'error']);

export function parseToast(data: unknown): { kind: Toast['kind']; title: string; message: string } | null {
  if (!data || typeof data !== 'object') return null;
  const d = data as Record<string, unknown>;
  const title = line(d.title, 120);
  if (!title) return null;
  const kind = typeof d.kind === 'string' && TOAST_KINDS.has(d.kind) ? (d.kind as Toast['kind']) : 'info';
  return { kind, title, message: line(d.message, 300) ?? '' };
}

/** A few notices a frame may raise in a while: more are dropped (a page cannot flood the app). */
const RATE = { windowMs: 10_000, max: 6 };
const raised = new Map<string, number[]>();
export function allowRaise(stem: string, now = Date.now()): boolean {
  const list = (raised.get(stem) ?? []).filter((at) => now - at < RATE.windowMs);
  if (list.length >= RATE.max) {
    raised.set(stem, list);
    return false;
  }
  list.push(now);
  raised.set(stem, list);
  return true;
}

let nextNotice = 1;
const NOTICES_MAX = 100;

export function addModuleNotice(stem: string, data: unknown, now = Math.floor(Date.now() / 1000)): ModuleNotice | null {
  if (!data || typeof data !== 'object') return null;
  const d = data as Record<string, unknown>;
  let notice: ModuleNotice;
  if (d.kind === 'tx') {
    if (typeof d.txid !== 'string' || !/^[0-9a-f]{64}$/.test(d.txid)) return null;
    if (modules.notices.some((n) => n.stem === stem && n.txid === d.txid)) return null;
    notice = { id: nextNotice++, stem, kind: 'tx', title: 'New transaction', text: '', time: now, unread: true, txid: d.txid, wallet: line(d.wallet, 64) ?? '' };
  } else if (d.kind === 'system') {
    const title = line(d.title, 120);
    if (!title) return null;
    const time = typeof d.time === 'number' && Number.isFinite(d.time) ? Math.floor(d.time) : now;
    notice = { id: nextNotice++, stem, kind: 'system', title, text: line(d.text, 300) ?? '', time, unread: true };
  } else {
    return null;
  }
  modules.notices = [...modules.notices, notice].slice(-NOTICES_MAX);
  return notice;
}

/** A notice of the app about a module (never of its pages): an update of its program. */
export function addAppNotice(stem: string, title: string, text: string, now = Math.floor(Date.now() / 1000)): void {
  modules.notices = [...modules.notices, { id: nextNotice++, stem, kind: 'system' as const, title, text, time: now, unread: true, app: true }].slice(-NOTICES_MAX);
}

export function moduleUnread(): number {
  return modules.notices.filter((n) => n.unread).length;
}

export function markModuleNoticesRead(): void {
  if (modules.notices.some((n) => n.unread)) modules.notices = modules.notices.map((n) => ({ ...n, unread: false }));
}

/** The name of a module, as the app knows it (its catalog), never as its pages say. */
export function moduleName(stem: string): string {
  return hub.list?.chains.find((c) => c.stem === stem)?.name ?? stem;
}

// ---- Where the user is ----

/** The page a module shows now (its home until it says). */
export function moduleRoute(stem: string): { page: string; tab: string } {
  return modules.routes[stem] ?? { page: modules.manifests[stem]?.home ?? 'home', tab: '' };
}

/** The module shown: a sidechain's stem, or '' when the app's own pages are. */
export function activeModule(): string {
  return hub.selected !== 'chains' && nav.page === 'sidechain' ? hub.selected : '';
}

type Teller = (stem: string, type: string, data: unknown) => boolean;
let teller: Teller = () => false;
/** How the app speaks to a frame (lib/packs.svelte.ts sets it). */
export function setTeller(fn: Teller): void {
  teller = fn;
}
export function tellModule(stem: string, type: string, data: unknown): boolean {
  return teller(stem, type, data);
}

/** Show a module, at a page of it (else where it was). */
export function openModule(stem: string, page?: string, tab = ''): void {
  if (!STEM.test(stem)) return;
  hub.selected = stem;
  if (page !== undefined && PAGE_ID.test(page.replace(/^@/, ''))) {
    const before = modules.routes[stem];
    modules.routes[stem] = { page, tab };
    if (page !== NODE_PAGE && (before?.page !== page || before?.tab !== tab)) tellModule(stem, 'navigate', { page, tab });
  }
  go('sidechain');
  if (page !== NODE_PAGE) tellModule(stem, 'focus', {});
}

/** Back to the app's own pages. */
export function openChains(page: Parameters<typeof go>[0] = 'home', tab = ''): void {
  hub.selected = 'chains';
  go(page, tab);
}

/** What a frame says, once its frame is known to be that module's (lib/packs.svelte.ts). */
export function onModuleMessage(stem: string, type: unknown, data: unknown): boolean {
  switch (type) {
    case 'manifest': {
      const m = parseManifest(data);
      if (!m) return false;
      modules.manifests[stem] = m;
      return true;
    }
    case 'route': {
      const r = parseRoute(data);
      if (!r) return false;
      const now = modules.routes[stem];
      if (now?.page === r.page && now?.tab === r.tab) return true;
      modules.routes[stem] = r;
      return true;
    }
    case 'summary': {
      const s = parseSummary(data);
      if (!s) return false;
      modules.summaries[stem] = s;
      return true;
    }
    case 'toast': {
      const x = parseToast(data);
      if (!x || !allowRaise(stem)) return false;
      // Named after the module, by the app: a page cannot make its notice look like one of the app's own.
      toast(x.kind, `${moduleName(stem)} · ${x.title}`, x.message);
      return true;
    }
    case 'notice':
      return addModuleNotice(stem, data) !== null;
    case 'modal': {
      const open = !!(data && typeof data === 'object' && (data as { open?: unknown }).open === true);
      modules.modal[stem] = open;
      return true;
    }
    case 'key':
      return keyHandler(stem, data);
    default:
      return false;
  }
}

type KeyFn = (stem: string, key: { key: string; ctrl: boolean; alt: boolean; shift: boolean }) => boolean;
let keyFn: KeyFn = () => false;
/** The app's shortcuts, for keys pressed in a frame (lib/keyboard.ts sets it). */
export function setModuleKeys(fn: KeyFn): void {
  keyFn = fn;
}
function keyHandler(stem: string, data: unknown): boolean {
  if (!data || typeof data !== 'object') return false;
  const d = data as Record<string, unknown>;
  if (typeof d.key !== 'string' || d.key.length > 20) return false;
  return keyFn(stem, { key: d.key, ctrl: d.ctrl === true, alt: d.alt === true, shift: d.shift === true });
}

/** The look of the app, for a frame: its built-in theme and each token as the page shows it now. */
export function currentLook(doc: Document = document): Record<string, unknown> {
  const root = doc.documentElement;
  const style = getComputedStyle(root);
  const tokens: Record<string, string> = {};
  for (const name of TOKENS) {
    const v = style.getPropertyValue(`--${name}`).trim();
    if (v) tokens[name] = v;
  }
  const scheme = (root.style.colorScheme || style.colorScheme || 'dark').includes('light') ? 'light' : 'dark';
  return {
    theme: root.dataset.theme ?? '',
    tokens,
    fontAdjust: prefs.fontAdjust,
    font: prefs.fontFamily ?? '',
    moneyFont: prefs.moneyFont,
    mode: prefs.uiMode,
    colorScheme: scheme,
  };
}

/** The icon of a module's page: one of the app's, else the path the module gave (an SVG path only). */
export function pageIconOf(p: ModulePage): { name?: IconName; path?: string } {
  if (p.iconPath) return { path: p.iconPath };
  return { name: (p.icon in ICONS ? p.icon : 'dot') as IconName };
}

/** The page a module shows, its title for the header (the app's words for its node page). */
export function moduleTitle(stem: string): string {
  const r = moduleRoute(stem);
  if (r.page === NODE_PAGE) return t('Its node');
  return modules.manifests[stem]?.pages.find((p) => p.id === r.page)?.title ?? t('Home');
}

export function moduleSectionOf(stem: string, page: string): string {
  return modules.manifests[stem]?.sections.find((s) => s.pages.includes(page))?.title ?? '';
}

export interface PortfolioRow {
  stem: string;
  name: string;
  entry: SidechainEntry;
  wallet: string;
  trusted: string | null;
  pending: string | null;
  unit: string;
  assets: ModuleAsset[];
}

/** The rows of the combined portfolio: each module turned on, from what its pages said, else from its node. */
export function portfolioRows(on: SidechainEntry[]): PortfolioRow[] {
  return on.map((c) => {
    const s = modules.summaries[c.stem];
    const o = modules.overview[c.stem];
    const fixed = (n: number | undefined) => (typeof n === 'number' && Number.isFinite(n) ? n.toFixed(8) : null);
    return {
      stem: c.stem,
      name: c.name,
      entry: c,
      wallet: s?.wallet || o?.wallet?.name || '',
      trusted: s?.trusted ?? fixed(o?.wallet?.trusted),
      pending: s?.pending ?? fixed(o?.wallet ? o.wallet.pending + o.wallet.immature : undefined),
      unit: modules.manifests[c.stem]?.unit ?? '',
      assets: s?.assets ?? [],
    };
  });
}

/** A transaction of any chain this app runs, for the list of every chain (Transactions, the filter of chains). */
export interface ChainTx {
  chain: string;
  name: string;
  txid: string;
  time: number;
  category: string;
  /** Coins of that chain, as its node gives them (never added to another chain's). */
  amount: number;
  address: string;
  label: string;
  confirmations: number;
}

interface ListedTx {
  txid?: unknown;
  time?: unknown;
  category?: unknown;
  amount?: unknown;
  address?: unknown;
  label?: unknown;
  confirmations?: unknown;
}

/** The transactions a node listed (listtransactions), as rows of the list of every chain: bounded, checked. */
export function chainTxs(stem: string, name: string, listed: unknown): ChainTx[] {
  if (!Array.isArray(listed)) return [];
  const out: ChainTx[] = [];
  for (const raw of listed.slice(-500) as ListedTx[]) {
    if (!raw || typeof raw !== 'object' || typeof raw.txid !== 'string' || !/^[0-9a-f]{64}$/.test(raw.txid)) continue;
    if (typeof raw.amount !== 'number' || !Number.isFinite(raw.amount)) continue;
    out.push({
      chain: stem,
      name,
      txid: raw.txid,
      time: typeof raw.time === 'number' ? raw.time : 0,
      category: typeof raw.category === 'string' ? raw.category.slice(0, 16) : '',
      amount: raw.amount,
      address: typeof raw.address === 'string' ? raw.address.slice(0, 120) : '',
      label: typeof raw.label === 'string' ? raw.label.slice(0, 120) : '',
      confirmations: typeof raw.confirmations === 'number' ? raw.confirmations : 0,
    });
  }
  return out;
}

/** Newest first, every chain together. */
export function mergeChainTxs(lists: ChainTx[][]): ChainTx[] {
  return lists.flat().sort((a, b) => b.time - a.time || a.chain.localeCompare(b.chain) || a.txid.localeCompare(b.txid));
}
