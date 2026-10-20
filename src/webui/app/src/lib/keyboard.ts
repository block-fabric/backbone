// The keyboard shortcuts of the main window (as chains-qt).
import { openPalette } from './commands.svelte';
import { go, nav, type PageId } from './nav.svelte';
import { host } from './rpc';
import { prefs, savePrefs, wallets } from './state.svelte';
import { clampFontAdjust } from './themes';
import { goBack, goForward } from './history.svelte';
import { cycleChain } from './chains.svelte';
import { activeModule, frameShown, modules, openModule, setModuleKeys } from './modules.svelte';

/** Ctrl+letter: the tabs of the node window (the Window menu of chains-qt). */
const NODE_TABS: Record<string, string> = { i: 'info', t: 'console', n: 'traffic', p: 'peers' };
const PAGE_KEYS: Record<string, PageId> = { '1': 'overview', '2': 'send', '3': 'receive', '4': 'transactions', '5': 'sidechains' };
const WALLET_PAGES = new Set<PageId>(['send', 'receive', 'transactions']);

/** Close what is on top: the newest open dialog (as Escape does), else the node window. */
export function closeTop(): boolean {
  const open = [...document.querySelectorAll<HTMLDialogElement>('dialog[open]')];
  const top = open[open.length - 1];
  if (top) {
    top.dispatchEvent(new Event('cancel', { cancelable: true }));
    return true;
  }
  if (nav.page === 'node') {
    go('overview');
    return true;
  }
  return false;
}

/** Handle a key press; true if it was a shortcut. */
export function handleShortcut(e: KeyboardEvent): boolean {
  const ctrl = e.ctrlKey || e.metaKey;
  // Back and forward (the app's history: its pages and the modules' alike).
  if (e.altKey && !ctrl && (e.key === 'ArrowLeft' || e.key === 'BrowserBack')) return goBack() || true;
  if (e.altKey && !ctrl && (e.key === 'ArrowRight' || e.key === 'BrowserForward')) return goForward() || true;
  if (e.key === 'BrowserBack') return goBack() || true;
  if (e.key === 'BrowserForward') return goForward() || true;
  // A module shown: Alt+digit opens its page of that shortcut, as in its own app.
  const stem = activeModule();
  if (stem && e.altKey && !ctrl && /^[0-9]$/.test(e.key)) {
    const page = modules.manifests[stem]?.pages.find((p) => p.shortcut === `Alt+${e.key}`);
    if (page) {
      openModule(stem, page.id);
      return true;
    }
  }
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
  if (key === 'k') {
    openPalette();
    return true;
  }
  // Ctrl+B folds the navigation into icons, or unfolds it (the sidebar's button, the palette).
  if (key === 'b') {
    savePrefs({ sidebarCollapsed: !prefs.sidebarCollapsed });
    return true;
  }
  // Ctrl+] and Ctrl+[: the next chain every page shows, and the one before (the top bar's selector).
  if (key === ']' || key === '[') {
    cycleChain(key === ']' ? 1 : -1);
    return true;
  }
  // Ctrl+W closes the dialog on top (GUIUtil::handleCloseWindowShortcut), or the node window.
  if (key === 'w') return closeTop();
  if (NODE_TABS[key]) {
    go('node', NODE_TABS[key]);
    return true;
  }
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

// Keys pressed in a module's frame that are the app's: an allowlist of navigation only (§13.5 #19), never handed to
// handleShortcut (that would let a frame quit, minimize, toggle the mask or the sidebar, close a dialog). Only from the
// frame of the module shown, while the window has the focus, a few a second at most.
const KEY_RATE = { windowMs: 1000, max: 8 };
let keyTimes: number[] = [];
export function moduleKey(stem: string, k: { key: string; ctrl: boolean; alt: boolean; shift: boolean }, now = Date.now()): boolean {
  if (stem !== activeModule() || !frameShown()) return false;
  if (typeof document !== 'undefined' && typeof document.hasFocus === 'function' && !document.hasFocus()) return false;
  keyTimes = keyTimes.filter((at) => now - at < KEY_RATE.windowMs);
  if (keyTimes.length >= KEY_RATE.max) return false;
  keyTimes.push(now);
  const plainAlt = k.alt && !k.ctrl && !k.shift;
  if ((plainAlt && k.key === 'ArrowLeft') || k.key === 'BrowserBack') return goBack() || true;
  if ((plainAlt && k.key === 'ArrowRight') || k.key === 'BrowserForward') return goForward() || true;
  if (plainAlt && /^[0-9]$/.test(k.key)) {
    const page = modules.manifests[stem]?.pages.find((p) => p.shortcut === `Alt+${k.key}`);
    if (!page) return false;
    openModule(stem, page.id);
    return true;
  }
  // The palette of the app: it only opens (what runs from it is the user's choice there).
  if (k.ctrl && !k.alt && !k.shift && k.key.toLowerCase() === 'k') {
    openPalette();
    return true;
  }
  return false;
}
setModuleKeys(moduleKey);
