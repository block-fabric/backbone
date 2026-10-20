// What each sidechain adds, as one group per chain (doc/unified-app.md §19): in the sidebar's Chain section (one
// folding group per sidechain turned on, with its mark, its name and how its node is doing), in the palette ("Layers:
// Trade"), and as the places a page of the app shows that chain. Opening an item selects that chain for every page
// (the top bar's selector), then opens the app's page for it (Staking, Assets, Markets…) or the chain's own page.
// Its features come from the chain's table (chain.capabilities), never from what its node answers.
import type { IconName } from '../components/Icon.svelte';
import { ALL, chainsWith, selectedChain, setSelectedChain, type ChainEntry } from './chains.svelte';
import { activeModule, moduleRoute, openChains, openModule } from './modules.svelte';
import { nav, type PageId } from './nav.svelte';

export interface ChainGroupItem {
  /** Stable (tests, the palette's ids). */
  id: string;
  /** The app's words (translated where shown). */
  label: string;
  icon: IconName;
  /** An app's page shown for that chain, or the chain's own page. */
  page?: PageId;
  tab?: string;
  module?: string;
  /** What it is for: its tooltip, the palette's keywords. */
  summary: string;
}

/** The items of a sidechain's group, from its table. Simple mode leaves out the chain's own pages (they stay a click away on its cards). */
export function chainGroupItems(e: ChainEntry, simple = false): ChainGroupItem[] {
  const c = e.caps;
  const out: ChainGroupItem[] = [];
  if (c?.assets) out.push({ id: 'assets', label: 'Assets', icon: 'coins', page: 'assets', tab: '', summary: 'Its assets the wallet holds; send them' });
  if (c?.dex) out.push({ id: 'trade', label: 'Trade', icon: 'arrow', page: 'assets', tab: 'trade', summary: 'Its market of assets (its pools and auctions)' });
  if (c?.staking) out.push({ id: 'staking', label: 'Staking', icon: 'shield', page: 'staking', summary: 'Its validators, bonds and rewards' });
  if (c?.names) out.push({ id: 'names', label: 'Names', icon: 'book', page: 'names', summary: 'Your names, and registering new ones' });
  if (c?.markets) {
    out.push({ id: 'markets', label: 'Markets', icon: 'dashboard', page: 'markets', tab: '', summary: 'Its prediction markets and their prices' });
    out.push({ id: 'positions', label: 'Positions', icon: 'coins', page: 'markets', tab: 'positions', summary: 'The shares your wallet holds and what they are worth now' });
    if (e.caps?.embed.includes('decisions')) out.push({ id: 'votes', label: 'Votes', icon: 'check', module: 'decisions', summary: 'Its decisions and the votes on them' });
  }
  if (c?.shielded && c.embed.includes('private')) out.push({ id: 'shielded', label: 'Shielded', icon: 'eyeoff', module: 'private', summary: 'Its shielded account: private payments' });
  out.push({ id: 'move', label: simple ? 'Move coins' : 'Deposit and withdraw', icon: 'sidechains', page: 'sidechains', summary: 'Coins into this chain and back out of it' });
  if (!simple) out.push({ id: 'own', label: 'All its pages', icon: 'modules', module: 'home', summary: 'The chain\'s own pages, as in its own app' });
  return out;
}

/** Open an item: its chain selected for every page, then its page. */
export function openChainItem(stem: string, item: ChainGroupItem): void {
  setSelectedChain(stem);
  if (item.module) openModule(stem, item.module);
  else if (item.page) openChains(item.page, item.tab ?? '');
}

/** Whether an item is where the app is now (its chain selected, its page shown). */
export function chainItemCurrent(stem: string, item: ChainGroupItem): boolean {
  if (item.module) {
    if (activeModule() !== stem) return false;
    const at = moduleRoute(stem).page;
    return item.module === 'home' ? !NAMED_MODULE_PAGES.has(at) : at === item.module;
  }
  if (activeModule() || selectedChain() !== stem || nav.page !== item.page) return false;
  return item.tab === undefined || (nav.tab || '') === item.tab;
}

// "All its pages" is current on a page of the module that no other item names.
const NAMED_MODULE_PAGES = new Set(['decisions', 'private']);

/**
 * The views over many chains kept in the navigation while All is selected (they would repeat a chain's own item
 * otherwise): "All assets" when two chains or more have assets, "All staking" when two or more stake.
 */
export function aggregateItems(entries: ChainEntry[], selection: string = selectedChain(entries)): { page: PageId; label: string; icon: IconName }[] {
  if (selection !== ALL) return [];
  const out: { page: PageId; label: string; icon: IconName }[] = [];
  if (chainsWith('assets', entries).length >= 2) out.push({ page: 'assets', label: 'All assets', icon: 'coins' });
  if (chainsWith('staking', entries).length >= 2) out.push({ page: 'staking', label: 'All staking', icon: 'shield' });
  return out;
}
