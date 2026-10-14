// The layouts of the main window: the same pages and actions, arranged three ways
// (doc/webui-layouts.md). Only data here; the shells are in views/layouts.
import type { IconName } from '../components/Icon.svelte';
import { PAGES, type PageId } from './nav.svelte';

export type LayoutId = 'workspace' | 'dashboard' | 'sidebar';

export interface LayoutInfo {
  id: LayoutId;
  name: string;
  icon: IconName;
  summary: string;
}

export const LAYOUTS: readonly LayoutInfo[] = [
  { id: 'workspace', name: 'Workspace tabs', icon: 'tabs', summary: 'Four areas across the top, each with its own tabs, and a detail panel on the right' },
  { id: 'dashboard', name: 'Dashboard', icon: 'dashboard', summary: 'A home of live cards with quick actions, and every page in the sidebar' },
  { id: 'sidebar', name: 'Compact sidebar', icon: 'sidebar', summary: 'An icon sidebar that opens on demand, dense tables, the status bar' },
];

/** The layout of a first start: the one closest to the window of chains-qt. */
export const DEFAULT_LAYOUT: LayoutId = 'dashboard';

export function isLayout(value: unknown): value is LayoutId {
  return typeof value === 'string' && LAYOUTS.some((l) => l.id === value);
}

/** The icon of a page in the navigation: one of its own where pages share an icon in PAGES. */
const NAV_ICONS: Partial<Record<PageId, IconName>> = {
  addresses: 'book',
  messages: 'edit',
  multisig: 'key',
  timestamp: 'clock',
  crypto: 'hash',
  kit: 'tool',
};

export function pageIcon(id: PageId): IconName {
  return NAV_ICONS[id] ?? PAGES[id].icon;
}

/** A place in the app: a page, and the tab of it. */
export interface Place {
  page: PageId;
  tab?: string;
  /** The name in a tab strip, when not the title of the page. */
  label?: string;
}

/** The tab a page shows when none is asked for (to tell which sub-tab is current). */
const DEFAULT_TAB: Partial<Record<PageId, string>> = { node: 'info', settings: 'display' };

export function isAt(place: Place, page: PageId, tab: string): boolean {
  if (place.page !== page) return false;
  if (place.tab === undefined) return true;
  return place.tab === (tab || DEFAULT_TAB[page] || '');
}

export interface Area {
  id: 'wallet' | 'chain' | 'sidechains' | 'tools' | 'node';
  label: string;
  icon: IconName;
  tabs: Place[];
}

/**
 * The workspace layout: five areas, each with its sub-tabs. Every page is in one. What concerns the
 * chain (its explorer, mining, the sidechains) comes before the tools.
 */
export const AREAS: readonly Area[] = [
  {
    id: 'wallet',
    label: 'Wallet',
    icon: 'wallet',
    tabs: [
      { page: 'overview' },
      { page: 'send' },
      { page: 'receive' },
      { page: 'transactions', label: 'History' },
      { page: 'addresses' },
      { page: 'wallets' },
    ],
  },
  {
    id: 'chain',
    label: 'Chain',
    icon: 'block',
    tabs: [{ page: 'explorer', label: 'Explorer' }, { page: 'mine' }],
  },
  {
    id: 'sidechains',
    label: 'Sidechains',
    icon: 'sidechains',
    tabs: [{ page: 'sidechains' }, { page: 'sidechain-nodes' }],
  },
  {
    id: 'tools',
    label: 'Tools',
    icon: 'hash',
    tabs: [
      { page: 'messages', label: 'Messages' },
      { page: 'psbt', label: 'PSBT' },
      { page: 'proof' },
      { page: 'multisig', label: 'Multisig' },
      { page: 'timestamp', label: 'Timestamp' },
      { page: 'crypto' },
    ],
  },
  {
    id: 'node',
    label: 'Node',
    icon: 'terminal',
    tabs: [
      { page: 'node', tab: 'console', label: 'Console' },
      { page: 'node', tab: 'peers', label: 'Peers' },
      { page: 'node', tab: 'info', label: 'Info' },
      { page: 'node', tab: 'traffic', label: 'Traffic' },
      { page: 'settings' },
      { page: 'kit' },
    ],
  },
];

/** The area a page is in (the interface kit and home: the node and the wallet). */
export function areaOf(page: PageId): Area {
  if (page === 'home') return AREAS[0];
  return AREAS.find((a) => a.tabs.some((t) => t.page === page)) ?? AREAS[0];
}

export function placeLabel(place: Place): string {
  return place.label ?? PAGES[place.page].title;
}

export interface NavSection {
  title: string;
  pages: PageId[];
}

/** The sidebars (dashboard, compact): every page, in the sections of the areas. */
export const NAV_SECTIONS: readonly NavSection[] = [
  { title: 'Wallet', pages: ['overview', 'send', 'receive', 'transactions', 'addresses', 'wallets'] },
  // What concerns the chain before the tools.
  { title: 'Chain', pages: ['sidechains', 'sidechain-nodes', 'mine', 'explorer'] },
  { title: 'Tools', pages: ['messages', 'psbt', 'proof', 'multisig', 'timestamp', 'crypto'] },
  { title: 'Node', pages: ['node', 'settings', 'kit'] },
];
