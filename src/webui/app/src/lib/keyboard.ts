// The keyboard shortcuts of the main window (as chains-qt).
import { go, nav, type PageId } from './nav.svelte';
import { host } from './rpc';
import { prefs, savePrefs, wallets } from './state.svelte';
import { clampFontAdjust } from './themes';

const PAGE_KEYS: Record<string, PageId> = { '1': 'overview', '2': 'send', '3': 'receive', '4': 'transactions', '5': 'sidechains' };
const WALLET_PAGES = new Set<PageId>(['send', 'receive', 'transactions', 'sidechains']);

/** Handle a key press; true if it was a shortcut. */
export function handleShortcut(e: KeyboardEvent): boolean {
  const ctrl = e.ctrlKey || e.metaKey;
  if (e.altKey && !ctrl && PAGE_KEYS[e.key]) {
    const page = PAGE_KEYS[e.key];
    if (WALLET_PAGES.has(page) && wallets.list.length === 0) return true;
    go(page);
    return true;
  }
  if (!ctrl) return false;
  const key = e.key.toLowerCase();
  if (e.shiftKey && key === 'm') {
    savePrefs({ mask: !prefs.mask });
    return true;
  }
  if (e.shiftKey && key === 'c') {
    go('node', 'console');
    return true;
  }
  if (e.shiftKey && key === 'd') {
    go('node', nav.page === 'node' ? nav.tab : 'info');
    return true;
  }
  if (e.shiftKey) return false;
  if (key === 'q') {
    host.quit();
    return true;
  }
  if (key === 'm') {
    host.minimize();
    return true;
  }
  if (key === '+' || key === '=') {
    savePrefs({ fontAdjust: clampFontAdjust(prefs.fontAdjust + 1) });
    return true;
  }
  if (key === '-' || key === '_') {
    savePrefs({ fontAdjust: clampFontAdjust(prefs.fontAdjust - 1) });
    return true;
  }
  return false;
}
