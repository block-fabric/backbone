// The names the interface shows on the eCash networks: the product's (the host's, app.info.name: Backbone), the
// chain's (eCash) and its coin's (ECX), never those of Chains (user feedback 2026-10-23: "our local backbone UI
// client shows Chains"). The names come from the host and the network (lib/brand.svelte.ts), never written into
// the interface: what it shows is checked on each eCash network, and its sources are checked for the old names
// in what they show (the Chains networks of -devnetworks keep theirs, from the same code).
import { render } from '@testing-library/svelte';
import { readdirSync, readFileSync } from 'node:fs';
import { join, relative } from 'node:path';
import { afterEach, beforeAll, beforeEach, describe, expect, it } from 'vitest';
import Brand from '../components/Brand.svelte';
import FirstRunTips from '../components/FirstRunTips.svelte';
import NotificationBell from '../components/NotificationBell.svelte';
import Sidebar from '../components/Sidebar.svelte';
import StatusBar from '../components/StatusBar.svelte';
import Multisig from '../pages/Multisig.svelte';
import Messages from '../pages/Messages.svelte';
import Receive from '../pages/Receive.svelte';
import Send from '../pages/Send.svelte';
import Settings from '../pages/Settings.svelte';
import Splash from '../views/Splash.svelte';
import { setTransport } from '../lib/bridge';
import { chainName, product, productInitial } from '../lib/brand.svelte';
import { buildCommands, groupTitle, menuSections } from '../lib/commands.svelte';
import { FIRST_STEPS } from '../lib/firststeps';
import { t } from '../lib/i18n.svelte';
import { nav } from '../lib/nav.svelte';
import { app, prefs, wallets } from '../lib/state.svelte';
import { setCurrency, ticker, UNITS } from '../lib/units';
import { proofVerdict } from '../lib/tools';

/** The old names: of the product (Chains), of its coin (CHN, mCHN). */
const OLD = /\bChains\b|\bm?CHN\b/;

const NETWORKS = [
  { network: 'ecash', networkLabel: '' },
  { network: 'ecashbeta', networkLabel: 'eCash betanet' },
  { network: 'ecashtest', networkLabel: 'eCash testnet' },
];

/** What a page shows: its text, and the words of its attributes (title, aria-label, placeholder, alt). */
function shown(root: HTMLElement): string {
  const words = [root.textContent ?? ''];
  for (const el of root.querySelectorAll('*')) {
    for (const a of ['title', 'aria-label', 'placeholder', 'alt']) {
      const v = el.getAttribute(a);
      if (v) words.push(v);
    }
  }
  return words.join('\n');
}

function oldNames(text: string): string[] {
  return text.split('\n').filter((line) => OLD.test(line));
}

beforeAll(() => {
  const proto = HTMLDialogElement.prototype as unknown as Record<string, unknown>;
  proto.showModal ??= function (this: HTMLDialogElement) {
    this.setAttribute('open', '');
  };
  proto.close ??= function (this: HTMLDialogElement) {
    this.removeAttribute('open');
  };
  window.matchMedia ??= ((query: string) => ({ matches: false, media: query, addEventListener() {}, removeEventListener() {} })) as unknown as typeof window.matchMedia;
  Element.prototype.scrollIntoView ??= function () {};
});

afterEach(() => {
  setTransport(null);
  wallets.list = [];
  wallets.current = null;
  setCurrency('regtest');
});

for (const { network, networkLabel } of NETWORKS) {
  describe(`on ${network}`, () => {
    beforeEach(() => {
      // As the host says it (app.info of src/webui/app.cpp) on this network.
      app.info = { name: 'Backbone', version: 'test', bugreport: '', network, networkLabel, chainName: 'eCash', coin: 'ECX', datadir: '/tmp/x', walletEnabled: true, devtools: false, test: true, platform: 'linux', blockSpacing: 600 };
      setCurrency(network);
      setTransport(async (r) => (r.method === 'prefs.set' ? {} : null));
      prefs.loaded = true;
      prefs.tipsDone = false;
      prefs.unit = 'CHN';
      wallets.list = [{ name: 'w', encrypted: false, locked: false, privateKeysDisabled: false } as (typeof wallets.list)[number]];
      wallets.current = 'w';
    });

    it('names the product, the chain and the coin after the host and the network', () => {
      expect(product()).toBe('Backbone');
      expect(productInitial()).toBe('B');
      expect(chainName()).toBe('eCash');
      expect(ticker()).toBe('ECX');
      expect(UNITS.map((u) => `${u.short} ${u.long}`).filter((u) => OLD.test(u))).toEqual([]);
      expect(t('Quit %1', product())).toBe('Quit Backbone');
      expect(t('Warning: Invalid {chain} address')).toBe('Warning: Invalid eCash address');
      expect(t('Get your first {coin}')).toBe('Get your first ECX');
    });

    it('has no old name in its menus, its commands, its first steps and its messages', () => {
      const commands = buildCommands();
      const words = commands.flatMap((c) => [t(c.label), c.tip ?? '', groupTitle(c.group)]);
      words.push(...menuSections(commands).map((m) => groupTitle(m.title)));
      words.push(...FIRST_STEPS.flatMap((s) => [t(s.title), t(s.text), s.action ? t(s.action.label) : '']));
      words.push(proofVerdict(1, 0, '1.5'), proofVerdict(1, 1, '1.5'));
      expect(words.filter((w) => OLD.test(w))).toEqual([]);
      expect(commands.find((c) => c.id === 'node.about')?.label).toBe('About Backbone');
      expect(groupTitle('Chains')).toBe('eCash');
    });

    it('has no old name in the sidebar, the brand, the splash and the status bar', () => {
      for (const view of [Sidebar, Brand, Splash, StatusBar, NotificationBell, FirstRunTips]) {
        const { container, unmount } = render(view as never);
        expect(oldNames(shown(container)), view.name).toEqual([]);
        unmount();
      }
      const { container, unmount } = render(Splash, { props: { stopping: true } });
      expect(shown(container)).toContain('Backbone is shutting down');
      unmount();
      const sidebar = render(Sidebar);
      expect(shown(sidebar.container)).toContain('Quit Backbone');
      expect(sidebar.container.querySelector('.brand .name')?.textContent).toBe('Backbone');
      sidebar.unmount();
    });

    it('has no old name on the pages of the wallet and the settings', () => {
      for (const [page, tab] of [
        [Send, ''],
        [Receive, ''],
        [Messages, 'sign'],
        [Messages, 'verify'],
        [Multisig, ''],
        [Settings, 'main'],
        [Settings, 'display'],
        [Settings, 'window'],
      ] as const) {
        nav.tab = tab;
        const { container, unmount } = render(page as never);
        expect(oldNames(shown(container)), `${page.name} ${tab}`).toEqual([]);
        unmount();
      }
      nav.tab = '';
    });
  });
}

describe('the sources of the interface', () => {
  // The places that name the Chains networks (CHN) on purpose, for -devnetworks, and the ids that keep their old
  // names (settings, menus): code, not what is shown.
  const ALLOWED: [string, RegExp][] = [
    ['lib/units.ts', /export type Unit = 'CHN' \| 'mCHN'|id: 'm?CHN'|ecash \? '[^']*' : '[^']*'|, 'CHN'[,)]/],
    ['lib/uri.ts', /parseAmount\(value, 'CHN'\)/],
    ['lib/state.svelte.ts', /unit: 'CHN' as Unit/],
    ['lib/brand.svelte.ts', /\? 'eCash' : 'Chains'/],
    ['lib/commands.svelte.ts', /MENU_GROUPS = \[|group: 'Chains'|group === 'Chains'/],
  ];
  const SRC = join(__dirname, '..');

  /** The code of a source without its comments (what it shows is in the code). */
  function code(text: string): string {
    return text
      .replace(/<!--[\s\S]*?-->/g, '')
      .replace(/\/\*[\s\S]*?\*\//g, '')
      .split('\n')
      .map((line) => (/^\s*\/\//.test(line) ? '' : line.replace(/\s\/\/\s.*$/, '')))
      .join('\n');
  }

  function files(dir: string): string[] {
    return readdirSync(dir, { withFileTypes: true }).flatMap((e) => {
      const path = join(dir, e.name);
      if (e.isDirectory()) return e.name === 'test' ? [] : files(path);
      return /\.(svelte|ts)$/.test(e.name) && !/\.test\.ts$/.test(e.name) ? [path] : [];
    });
  }

  it('name neither Chains nor CHN in what they show', () => {
    const found: string[] = [];
    for (const path of files(SRC)) {
      const file = relative(SRC, path);
      code(readFileSync(path, 'utf8'))
        .split('\n')
        .forEach((line, i) => {
          if (!OLD.test(line)) return;
          if (ALLOWED.some(([f, re]) => f === file && re.test(line))) return;
          found.push(`${file}:${i + 1}: ${line.trim()}`);
        });
    }
    expect(found).toEqual([]);
  });

  it('take the name of the product from the build, not from the page', () => {
    for (const page of ['index.html', 'dialog.html']) {
      expect(readFileSync(join(SRC, '..', page), 'utf8')).toMatch(/<title>%CLIENT_NAME%<\/title>/);
    }
  });
});
