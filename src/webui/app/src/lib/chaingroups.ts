// What is a sidechain's own, as one group per chain (doc/unified-app.md §20.1): in the sidebar's Chain section (one
// folding group per sidechain turned on, with its mark, its name and how its node is doing) and in the palette
// ("Layers: Trade"). A group holds only that chain's own pages: those of its own app's Chain section that are its own
// (not the template's Drivechains, Merged mining and Explorer, which the app has once for every chain), without Staking
// (the app's Staking entry has it). They open in the app's main area, in the chain's frame (§14), with the titles and
// icons its own app gives them (its manifest), in its own Simple mode only those it shows there. Moving coins in and out
// is the Sidechains page's, for every chain at once. Settings → Interface can add "All its pages" (debugging).
import type { IconName } from '../components/Icon.svelte';
import { selectedChain, setSelectedChain, type ChainEntry } from './chains.svelte';
import { activeModule, modules, moduleRoute, openChains, openModule, pageIconOf } from './modules.svelte';
import { nav, type PageId } from './nav.svelte';

export interface ChainGroupItem {
  /** Stable (tests, the palette's ids): the chain's own page id, or "own" for all its pages. */
  id: string;
  /** Its title: the chain's own words when its pages said them (in the language shown), else these. */
  label: string;
  icon: IconName;
  /** An SVG path of the chain's own icon (its manifest), drawn in place of icon. */
  iconPath?: string;
  /** An app's page shown for that chain, or the chain's own page. */
  page?: PageId;
  tab?: string;
  module?: string;
  /** What it is for: its tooltip, the palette's keywords. */
  summary: string;
  /** A tab of another of the chain's pages (its id), listed under it in the group (Hivemind: Create under Markets). */
  under?: string;
}

interface OwnPage {
  id: string;
  label: string;
  /** Its title in the chain's Simple mode, when Simple mode shows it. */
  simple?: string;
  icon: IconName;
  summary: string;
  /**
   * A tab of another page of the chain (its id), listed nested under it in the group (user, 2026-10-21: Hivemind's
   * Create "nested under Markets"): `tab` is the tab of that page it is (the chain's own pages lead its id there).
   */
  under?: string;
  tab?: string;
}

/** The chains' own icons of their pages (each pack's chain/icons.ts at the pinned commits): the same before and after a
 *  chain's pages are loaded (its frame is made at its first visit, doc/unified-app.md §20.6). */
/** Which of PATHS each page of a chain has. */
const OWN_ICON: Record<string, string> = {"assets": "gem", "my-assets": "holdings", "trade": "swap", "auctions": "gavel", "register": "tagplus", "names": "tag", "directory": "atlas", "paymail": "mail", "dns": "globe", "markets": "market", "portfolio": "portfolio", "voting": "ballot", "decisions": "decision", "create": "create", "votecoin": "votecoin", "payouts": "payout", "private": "private", "memos": "memo", "zhistory": "zhistory", "meltcast": "meltcast", "pool": "pool", "batchpay": "batch", "payments": "ledger", "throughput": "gauge"};

const PATHS: Record<string, string> = {
  "gem": "M7 4h10l4 5.5L12 21 3 9.5zM3 9.5h18M9.5 4L8 9.5l4 11.5 4-11.5L14.5 4",
  "holdings": "M4 16.5c0 1.4 2.7 2.5 6 2.5s6-1.1 6-2.5M4 12.5c0 1.4 2.7 2.5 6 2.5s6-1.1 6-2.5M4 8.5C4 7.1 6.7 6 10 6s6 1.1 6 2.5S13.3 11 10 11 4 9.9 4 8.5zM4 8.5v8M16 8.5v8M18 7.2c1.2.4 2 1 2 1.8v6.5c0 .8-.8 1.4-2 1.8",
  "swap": "M4 8h14M14 4l4 4-4 4M20 16H6M10 12l-4 4 4 4",
  "gavel": "M13.5 4.5l6 6M11 7l6 6M9.8 8.2l6 6M12.2 5.8l-5 5 2 2 5-5M8.5 11.5L3.5 16.5M4 20.5h10",
  "batch": "M3 12h6M9 12l4-6h7M9 12h11M9 12l4 6h7M17 3l3 3-3 3M17 9l3 3-3 3M17 15l3 3-3 3",
  "ledger": "M5 4h14v16H5zM8 8h8M8 12h8M8 16h5M16 15.5l1.5 1.5L20 14",
  "gauge": "M4.5 17a8 8 0 1 1 15 0M12 13l4-4.5M12 13h.01M7 17h10",
  "tagplus": "M3.5 6.5a2 2 0 0 1 2-2h8.2a2 2 0 0 1 1.4.6l5.4 5.5a2 2 0 0 1 0 2.8l-5.4 5.5a2 2 0 0 1-1.4.6H5.5a2 2 0 0 1-2-2zM7 12h6M10 9v6",
  "tag": "M3.5 6.5a2 2 0 0 1 2-2h8.2a2 2 0 0 1 1.4.6l5.4 5.5a2 2 0 0 1 0 2.8l-5.4 5.5a2 2 0 0 1-1.4.6H5.5a2 2 0 0 1-2-2zM8 12a1.4 1.4 0 1 0 2.8 0A1.4 1.4 0 1 0 8 12",
  "atlas": "M4 5.5C6.5 4.5 9.5 4.5 12 6c2.5-1.5 5.5-1.5 8-.5v13c-2.5-1-5.5-1-8 .5-2.5-1.5-5.5-1.5-8-.5zM12 6v13.5",
  "mail": "M3.5 6.5h17v11h-17zM3.5 7l8.5 6.5L20.5 7",
  "globe": "M12 21a9 9 0 1 0 0-18 9 9 0 0 0 0 18zM3 12h18M12 3c2.6 2.6 3.8 5.6 3.8 9s-1.2 6.4-3.8 9c-2.6-2.6-3.8-5.6-3.8-9S9.4 5.6 12 3",
  "market": "M4 20h16M4 4v16M7 15l4-5 3 3 6-7M16 6h4v4",
  "portfolio": "M4 8h16v11H4zM9 8V5h6v3M4 13h16M11 13v2h2v-2",
  "ballot": "M4 13h16v7H4zM8 13V4h8v9M10.5 8.5l1.2 1.2 2.3-2.4M8 16.5h8",
  "decision": "M12 21a9 9 0 1 0 0-18 9 9 0 0 0 0 18zM9.6 9.4a2.5 2.5 0 1 1 3.4 2.3c-.6.3-1 .8-1 1.5v.6M12 17h.01",
  "create": "M12 3v5M12 16v5M3 12h5M16 12h5M6.3 6.3l2.8 2.8M14.9 14.9l2.8 2.8M6.3 17.7l2.8-2.8M14.9 9.1l2.8-2.8",
  "votecoin": "M12 21a9 9 0 1 0 0-18 9 9 0 0 0 0 18zM8.5 12.2l2.4 2.4 4.6-5",
  "payout": "M12 3v8M8.5 7.5L12 11l3.5-3.5M4 15h16M4 19h16",
  "private": "M12 3l8 3v6c0 4.5-3.4 8-8 9-4.6-1-8-4.5-8-9V6zM12 10.5a1.6 1.6 0 1 0 0 .01M12 12v3.5",
  "memo": "M4 6h16v12H4zM4 7l8 6 8-6M9 13.5L4 18M15 13.5l5 4.5",
  "zhistory": "M12 3l8 3v6c0 4.5-3.4 8-8 9-4.6-1-8-4.5-8-9V6zM12 8.5V12l2.5 1.5",
  "meltcast": "M7 4v7M4.5 8.5L7 11l2.5-2.5M17 11V4M14.5 6.5L17 4l2.5 2.5M3 15c3 2 6 2 9 0s6-2 9 0M3 19c3 2 6 2 9 0s6-2 9 0",
  "pool": "M12 3c2.5 3 4 5.2 4 7a4 4 0 0 1-8 0c0-1.8 1.5-4 4-7zM3 17c3 2 6 2 9 0s6-2 9 0M3 21c3 2 6 2 9 0s6-2 9 0"
};

/**
 * The pages of each chain's own app's Chain section that are its own (its src/webui/app/src/chain/local.ts, section
 * "chain"), Staking left out. Read 2026-10-20 from the pinned packs (sidechains.json). A page the chain's manifest does
 * not list (another version, or not in its Simple mode) is not shown once the manifest is known.
 */
export const OWN_PAGES: Record<string, OwnPage[]> = {
  layers: [
    { id: 'assets', label: 'Browse assets', icon: 'coins', summary: 'Every asset of the chain: its supply, markets, auctions and history' },
    { id: 'my-assets', label: 'My assets', simple: 'My assets', icon: 'wallet', summary: 'What this wallet holds and controls' },
    { id: 'trade', label: 'Trade', simple: 'Swap', icon: 'arrow', summary: 'Swap assets in their pools, and provide liquidity' },
    { id: 'auctions', label: 'Auctions', icon: 'clock', summary: 'Buy at a falling price, or sell by Dutch auction' },
  ],
  bitassets: [
    { id: 'assets', label: 'Browse assets', icon: 'coins', summary: 'Every asset of the chain: its supply, markets, auctions and history' },
    { id: 'my-assets', label: 'My assets', simple: 'My assets', icon: 'wallet', summary: 'What this wallet holds and controls' },
    { id: 'trade', label: 'Trade', simple: 'Swap', icon: 'arrow', summary: 'Swap assets in their pools, and provide liquidity' },
    { id: 'auctions', label: 'Auctions', icon: 'clock', summary: 'Buy at a falling price, or sell by Dutch auction' },
  ],
  bitnames: [
    { id: 'register', label: 'Get a name', simple: 'Get a name', icon: 'plus', summary: 'Find a name that is free and take it' },
    { id: 'names', label: 'My names', simple: 'My names', icon: 'book', summary: 'The names of the wallet: records, subnames, giving away' },
    { id: 'directory', label: 'Name directory', icon: 'search', summary: 'Every name of the registry' },
    { id: 'paymail', label: 'Paymail', icon: 'text', summary: 'Messages with a payment that only the holder of a name reads' },
    { id: 'dns', label: 'DNS', icon: 'explorer', summary: 'The DNS server of .x names on its node' },
  ],
  // Votecoin is a tab of the Voting center, Create one of Markets, listed under it (user, 2026-10-21).
  hivemind: [
    { id: 'markets', label: 'Markets', simple: 'Questions', icon: 'dashboard', summary: 'The prediction markets, to trade in' },
    { id: 'create', label: 'Create', icon: 'plus', summary: 'List a decision and open a market on it', under: 'markets', tab: 'create' },
    { id: 'portfolio', label: 'Portfolio', simple: 'My bets', icon: 'coins', summary: 'Your shares and what they are worth' },
    { id: 'voting', label: 'Voting center', icon: 'check', summary: 'Your votes on the decisions and the votecoin that casts them' },
    { id: 'decisions', label: 'Decisions', icon: 'book', summary: 'The questions the markets are about, and their calendar' },
    { id: 'payouts', label: 'Payouts', icon: 'download', summary: 'What the markets paid' },
  ],
  zside: [
    { id: 'private', label: 'Private', simple: 'Private payments', icon: 'eyeoff', summary: 'The shielded balance: pay privately, shield and unshield' },
    { id: 'memos', label: 'Memos', icon: 'text', summary: 'The memos the shielded account received' },
    { id: 'zhistory', label: 'Private activity', icon: 'history', summary: 'The transactions and notes of the shielded account' },
    { id: 'meltcast', label: 'Melt & cast', icon: 'refresh', summary: 'Shield and unshield in steps that leave nothing to link' },
    { id: 'pool', label: 'Shielded pool', icon: 'block', summary: 'What the shielded pool holds' },
  ],
  thunder: [
    { id: 'batchpay', label: 'Batch pay', icon: 'send', summary: 'Pay many recipients at once' },
    { id: 'payments', label: 'Payments', simple: 'Payments', icon: 'history', summary: 'The payments of the wallet: batches as one, totals by day' },
    { id: 'throughput', label: 'Throughput', simple: 'How busy', icon: 'dashboard', summary: 'Transactions per block and per second, fees, the mempool' },
  ],
  // FastBlocks: Staking is its only page of its own (the app's Staking entry).
  fastblocks: [],
};

/**
 * The items of a sidechain's group. `allPages` (Settings → Interface, off by default) adds "All its pages": the chain's
 * whole own navigation, listed under it while shown.
 */
export function chainGroupItems(e: ChainEntry, simple = false, allPages = false): ChainGroupItem[] {
  const manifest = modules.manifests[e.id];
  const listed = manifest ? new Map(manifest.pages.map((p) => [p.id, p])) : null;
  const out: ChainGroupItem[] = [];
  for (const own of OWN_PAGES[e.id] ?? []) {
    const p = listed?.get(own.id);
    if (own.under !== undefined) {
      // A tab of another page, under it: while that page is listed, in Advanced (its own manifest may still list it
      // as a page of its own: a version of its pages before it became a tab).
      if (simple || !out.some((i) => i.id === own.under && !i.under)) continue;
    } else if (listed ? !p : simple && !own.simple) {
      // Known from its manifest: only what it lists (its version, its mode). Not yet: what its mode shows here.
      continue;
    }
    const icon = p ? pageIconOf(p) : PATHS[OWN_ICON[own.id] ?? ''] ? { path: PATHS[OWN_ICON[own.id] ?? ''] } : { name: own.icon };
    out.push({
      id: own.id,
      label: p?.title || (simple && own.simple ? own.simple : own.label),
      icon: icon.name ?? own.icon,
      ...(icon.path ? { iconPath: icon.path } : {}),
      module: own.id,
      summary: p?.summary || own.summary,
      ...(own.under !== undefined ? { under: own.under } : {}),
    });
  }
  if (allPages && !simple) out.push({ id: 'own', label: 'All its pages', icon: 'modules', module: 'home', summary: "The chain's own pages, as in its own app" });
  return out;
}

/** Whether one of the items listed under an item is where the app is now (the item reads as the place it is in). */
export function chainItemWithin(stem: string, item: ChainGroupItem, items: readonly ChainGroupItem[]): boolean {
  return items.some((i) => i.under === item.id && chainItemCurrent(stem, i));
}

/** Open an item: its chain selected for every page, then its page. */
export function openChainItem(stem: string, item: ChainGroupItem): void {
  setSelectedChain(stem);
  if (item.module) openModule(stem, item.module);
  else if (item.page) openChains(item.page, item.tab ?? '');
}

/** Whether the chain's page shown is the tab of another page that an own page is (its route: the page and its tab). */
function inTabOf(own: OwnPage, route: { page: string; tab: string }): boolean {
  return own.under !== undefined && own.tab !== undefined && route.page === own.under && (route.tab === own.tab || route.tab.startsWith(`${own.tab}/`));
}

/** Whether an item is where the app is now (its chain's page shown). */
export function chainItemCurrent(stem: string, item: ChainGroupItem): boolean {
  if (item.module) {
    if (activeModule() !== stem) return false;
    const route = moduleRoute(stem);
    const at = route.page;
    const owns = OWN_PAGES[stem] ?? [];
    if (item.id === 'own') return !owns.some((p) => p.id === at || inTabOf(p, route));
    const self = owns.find((p) => p.id === item.id);
    if (self?.under !== undefined) return at === self.id || inTabOf(self, route);
    // A page with one of its tabs listed under it: that one is current while its tab is shown.
    if (owns.some((p) => p.under === item.module && inTabOf(p, route))) return false;
    return at === item.module;
  }
  if (activeModule() || selectedChain() !== stem || nav.page !== item.page) return false;
  return item.tab === undefined || (nav.tab || '') === item.tab;
}
