// The fixes of the audit of chains-app (doc/webui-audit.md) in the interface.
import { fireEvent, render, screen, waitFor } from '@testing-library/svelte';
import { afterEach, beforeAll, beforeEach, describe, expect, it } from 'vitest';
import Shell from '../views/Shell.svelte';
import { setTransport, type BridgeRequest } from '../lib/bridge';
import { dialogs } from '../lib/dialogs.svelte';
import { go, nav } from '../lib/nav.svelte';
import { app, node, prefs, wallets } from '../lib/state.svelte';
import { resetWalletData } from '../lib/walletdata.svelte';

beforeAll(() => {
  const proto = HTMLDialogElement.prototype as unknown as Record<string, unknown>;
  proto.showModal ??= function (this: HTMLDialogElement) {
    this.setAttribute('open', '');
  };
  proto.close ??= function (this: HTMLDialogElement) {
    this.removeAttribute('open');
  };
});

function info(walletEnabled: boolean) {
  return { name: 'Chains', version: 'test', bugreport: '', network: 'regtest', networkLabel: 'regtest', datadir: '/tmp/x', walletEnabled, devtools: false, test: true, platform: 'linux', blockSpacing: 600 };
}

beforeEach(() => {
  app.info = info(true);
  prefs.unit = 'CHN';
  prefs.mask = false;
  nav.page = 'overview';
  nav.tab = '';
  node.tip = { height: 130, time: 1_700_000_000, hash: '00'.repeat(32), progress: 1, state: 'synced', ibd: false, loadingBlocks: false };
});

afterEach(() => {
  setTransport(null);
  dialogs.queue.splice(0);
  wallets.list = [];
  wallets.current = null;
  resetWalletData();
});

/** The host: what the handlers answer, an error for the rest. */
function host(handlers: Record<string, (r: BridgeRequest) => unknown> = {}): BridgeRequest[] {
  const calls: BridgeRequest[] = [];
  setTransport(async (r) => {
    calls.push(r);
    const h = handlers[r.method ?? ''];
    if (!h) throw { code: -32601, message: `No ${r.method}` };
    return h(r);
  });
  return calls;
}

function menuLabels(): string[] {
  return [...document.querySelectorAll('.menubar > li > button, [role="menubar"] > * > button')].map((b) => b.textContent?.trim() ?? '');
}

describe('a node without wallets (-disablewallet, C1)', () => {
  it('has no wallet menus nor wallet pages, and never calls the wallet functions', async () => {
    app.info = info(false);
    const calls = host();
    render(Shell);
    await waitFor(() => expect(document.querySelector('.content')).toBeTruthy());
    const menus = menuLabels();
    expect(menus.length).toBeGreaterThan(0);
    expect(menus).not.toContain('Your wallet');
    expect(menus).not.toContain('Banking');
    expect(document.body.textContent).toContain('This node runs without wallets');
    expect(document.body.textContent).not.toContain('Create or open a wallet');
    // The sidebar shows no wallet pages.
    expect(screen.queryByRole('button', { name: /^Send/ })).toBeNull();
    // The Wallets page, if reached, says why instead of calling the wallet functions.
    go('wallets', 'create');
    await waitFor(() => screen.getByRole('heading', { name: 'Wallets' }));
    expect(document.body.textContent).toContain('-disablewallet');
    expect(calls.some((c) => c.method?.startsWith('wallets.'))).toBe(false);
  });

  it('shows the wallet pages without a wallet as a way to make one, not a placeholder (L1)', async () => {
    host();
    render(Shell);
    go('send');
    await waitFor(() => screen.getByRole('heading', { name: 'Send' }));
    expect(document.body.textContent).toContain('This page needs a wallet');
    expect(document.body.textContent).not.toMatch(/being rebuilt|Parity items/);
    await fireEvent.click(screen.getByRole('button', { name: 'Open wallet…' }));
    expect(nav.page).toBe('wallets');
  });
});
