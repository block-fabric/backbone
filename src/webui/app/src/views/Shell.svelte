<script lang="ts">
  // The main window once the node runs: menus, navigation, the page, the status bar.
  import AppMenu, { type Menu } from '../components/AppMenu.svelte';
  import Sidebar from '../components/Sidebar.svelte';
  import StatusBar from '../components/StatusBar.svelte';
  import Kit from '../pages/Kit.svelte';
  import NodeWindow from '../pages/NodeWindow.svelte';
  import Overview from '../pages/Overview.svelte';
  import Planned from '../pages/Planned.svelte';
  import Settings from '../pages/Settings.svelte';
  import { go, nav, PAGES, type PageId } from '../lib/nav.svelte';
  import { host } from '../lib/rpc';
  import { app, prefs, savePrefs, wallets } from '../lib/state.svelte';
  import { clampFontAdjust, THEMES } from '../lib/themes';

  const noWallet = $derived(wallets.list.length === 0);
  const page = $derived(PAGES[nav.page]);

  function to(id: PageId, tab = '') {
    return () => go(id, tab);
  }

  const menus: Menu[] = $derived([
    {
      label: 'Your wallet',
      items: [
        { label: 'Create wallet…', action: to('wallets', 'create') },
        { label: 'Open wallet…', action: to('wallets', 'open') },
        { label: 'Close wallet…', action: to('wallets', 'close'), disabled: noWallet },
        { label: 'Close all wallets…', action: to('wallets', 'close-all'), disabled: noWallet },
        { label: 'Migrate wallet…', action: to('wallets', 'migrate') },
        'separator',
        { label: 'Sending addresses', action: to('addresses', 'send'), disabled: noWallet },
        { label: 'Receiving addresses', action: to('addresses', 'receive'), disabled: noWallet },
        'separator',
        { label: 'Backup wallet…', action: to('wallets', 'backup'), disabled: noWallet },
        { label: 'Restore wallet…', action: to('wallets', 'restore') },
        { label: 'Export watch-only wallet…', action: to('wallets', 'export'), disabled: noWallet },
        'separator',
        { label: 'Encrypt wallet…', action: to('wallets', 'encrypt'), disabled: noWallet },
        { label: 'Change passphrase…', action: to('wallets', 'passphrase'), disabled: noWallet },
        { label: 'Mask values', action: () => savePrefs({ mask: !prefs.mask }), checked: prefs.mask, shortcut: 'Ctrl+Shift+M' },
      ],
    },
    {
      label: 'Banking',
      items: [
        { label: 'Send', action: to('send'), shortcut: 'Alt+2', disabled: noWallet },
        { label: 'Receive', action: to('receive'), shortcut: 'Alt+3', disabled: noWallet },
        { label: 'Transactions', action: to('transactions'), shortcut: 'Alt+4', disabled: noWallet },
        { label: 'Open URI…', action: to('send', 'uri'), disabled: noWallet },
        'separator',
        { label: 'Proof of funds', action: to('proof'), disabled: noWallet },
        { label: 'Multisig lounge', action: to('multisig'), disabled: noWallet },
        'separator',
        { label: 'Load PSBT from file…', action: to('psbt', 'file'), disabled: noWallet },
        { label: 'Load PSBT from clipboard…', action: to('psbt', 'clipboard'), disabled: noWallet },
      ],
    },
    {
      label: 'Use Chains',
      items: [
        { label: 'Timestamp file', action: to('timestamp') },
        'separator',
        { label: 'Sign message…', action: to('messages', 'sign'), disabled: noWallet },
        { label: 'Verify message…', action: to('messages', 'verify'), disabled: noWallet },
        'separator',
        { label: 'Sidechains', action: to('sidechains'), shortcut: 'Alt+5', disabled: noWallet },
      ],
    },
    {
      label: 'Chains',
      items: [
        { label: 'Sidechain nodes', action: to('sidechain-nodes') },
        { label: 'Solo mine', action: to('mine') },
        { label: 'Sidechain proposals and votes', action: to('sidechains', 'proposals'), disabled: noWallet },
      ],
    },
    {
      label: 'Crypto tools',
      items: [
        { label: 'Block explorer', action: to('explorer') },
        { label: 'Hash calculator', action: to('crypto', 'hash') },
        { label: 'Merkle tree', action: to('crypto', 'merkle') },
        { label: 'Address decoder', action: to('crypto', 'address') },
      ],
    },
    {
      label: 'Window',
      items: [
        { label: 'Minimize', action: () => host.minimize(), shortcut: 'Ctrl+M' },
        'separator',
        { label: 'Information', action: to('node', 'info'), shortcut: 'Ctrl+I' },
        { label: 'Console', action: to('node', 'console'), shortcut: 'Ctrl+T' },
        { label: 'Network traffic', action: to('node', 'traffic'), shortcut: 'Ctrl+N' },
        { label: 'Peers', action: to('node', 'peers'), shortcut: 'Ctrl+P' },
      ],
    },
    {
      label: 'This node',
      items: [
        { label: 'Node window', action: to('node'), shortcut: 'Ctrl+Shift+D' },
        { label: 'Options…', action: to('settings', 'main') },
        {
          label: 'Theme',
          items: [
            ...THEMES.map((t) => ({ label: t.name, checked: prefs.theme === t.id, action: () => savePrefs({ theme: t.id }) })),
            { label: 'Custom…', action: to('settings', 'display') },
          ],
        },
        { label: 'Increase font size', action: () => savePrefs({ fontAdjust: clampFontAdjust(prefs.fontAdjust + 1) }), shortcut: 'Ctrl++' },
        { label: 'Decrease font size', action: () => savePrefs({ fontAdjust: clampFontAdjust(prefs.fontAdjust - 1) }), shortcut: 'Ctrl+−' },
        { label: 'Command-line options', action: () => (nav.dialog = 'help') },
        'separator',
        { label: `About ${app.info?.name ?? 'Chains'}`, action: () => (nav.dialog = 'about') },
        ...(app.info?.test || app.info?.devtools ? [{ label: 'Interface kit', action: to('kit') }] : []),
        'separator',
        { label: 'Exit', action: () => host.quit(), shortcut: 'Ctrl+Q' },
      ],
    },
  ]);
</script>

<div class="shell">
  <Sidebar />
  <div class="main">
    <header class="appbar">
      <AppMenu {menus} />
    </header>
    <main class="content" id="content" tabindex="-1">
      {#key nav.page}
        {#if nav.page === 'overview'}
          <Overview />
        {:else if nav.page === 'node'}
          <NodeWindow />
        {:else if nav.page === 'settings'}
          <Settings />
        {:else if nav.page === 'kit'}
          <Kit />
        {:else}
          <Planned {page} />
        {/if}
      {/key}
    </main>
    <StatusBar />
  </div>
</div>

<style>
  .shell {
    display: flex;
    height: 100%;
  }
  .main {
    flex: 1;
    min-width: 0;
    display: flex;
    flex-direction: column;
  }
  .appbar {
    display: flex;
    align-items: center;
    gap: 1rem;
    min-height: 2.6rem;
    padding: 0.25rem 0.75rem;
    border-bottom: 1px solid var(--line);
    background: var(--surface);
  }
  .content {
    flex: 1;
    min-height: 0;
    overflow: auto;
    outline: none;
  }
</style>
