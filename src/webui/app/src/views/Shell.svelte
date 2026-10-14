<script lang="ts">
  // The main window once the node runs: menus, navigation, the page, the status bar.
  import AppMenu, { type Menu } from '../components/AppMenu.svelte';
  import Sidebar from '../components/Sidebar.svelte';
  import StatusBar from '../components/StatusBar.svelte';
  import Kit from '../pages/Kit.svelte';
  import NodeWindow from '../pages/NodeWindow.svelte';
  import Overview from '../pages/Overview.svelte';
  import NoWallet from '../components/NoWallet.svelte';
  import WindowButtons from '../components/WindowButtons.svelte';
  import { barMouseDown, frame } from '../lib/frame.svelte';
  import Settings from '../pages/Settings.svelte';
  import Addresses from '../pages/Addresses.svelte';
  import Messages from '../pages/Messages.svelte';
  import Psbt from '../pages/Psbt.svelte';
  import Receive from '../pages/Receive.svelte';
  import Send from '../pages/Send.svelte';
  import Transactions from '../pages/Transactions.svelte';
  import Wallets from '../pages/Wallets.svelte';
  import TxNotifier from '../components/TxNotifier.svelte';
  import SyncOverlay from '../components/SyncOverlay.svelte';
  import Crypto from '../pages/Crypto.svelte';
  import Explorer from '../pages/Explorer.svelte';
  import Mine from '../pages/Mine.svelte';
  import Multisig from '../pages/Multisig.svelte';
  import Proof from '../pages/Proof.svelte';
  import SidechainNodes from '../pages/SidechainNodes.svelte';
  import Sidechains from '../pages/Sidechains.svelte';
  import Timestamp from '../pages/Timestamp.svelte';
  import { go, nav, PAGES, type PageId } from '../lib/nav.svelte';
  import { host } from '../lib/rpc';
  import { app, prefs, savePrefs, wallets, walletsEnabled } from '../lib/state.svelte';
  import { clampFontAdjust, THEMES } from '../lib/themes';

  const noWallet = $derived(wallets.list.length === 0);
  // With -disablewallet there are no wallet menus nor wallet pages (chains-qt builds no wallet UI then).
  const walletsOn = $derived(walletsEnabled());
  const currentWallet = $derived(wallets.list.find((w) => w.name === wallets.current) ?? null);
  const currentEncrypted = $derived(!!currentWallet?.encrypted);
  const currentWatchOnly = $derived(!!currentWallet?.privateKeysDisabled);
  const page = $derived(PAGES[nav.page]);

  function to(id: PageId, tab = '') {
    return () => go(id, tab);
  }

  const menus: Menu[] = $derived([
    ...(walletsOn ? walletMenus() : []),
    ...otherMenus(),
  ]);

  function walletMenus(): Menu[] {
    return [
    {
      label: 'Your wallet',
      items: [
        { label: 'Create wallet…', action: to('wallets', 'create'), tip: 'Create a new wallet' },
        { label: 'Open wallet…', action: to('wallets', 'open'), tip: 'Open a wallet' },
        { label: 'Close wallet…', action: to('wallets', 'close'), disabled: noWallet, tip: 'Close wallet' },
        { label: 'Close all wallets…', action: to('wallets', 'close-all'), disabled: noWallet, tip: 'Close all wallets' },
        { label: 'Migrate wallet…', action: to('wallets', 'migrate'), tip: 'Migrate a wallet' },
        { label: 'Restore and migrate wallet file…', action: to('wallets', 'restore-migrate') },
        'separator',
        { label: 'Sending addresses', action: to('addresses', 'send'), disabled: noWallet, tip: 'Show the list of used sending addresses and labels' },
        { label: 'Receiving addresses', action: to('addresses', 'receive'), disabled: noWallet, tip: 'Show the list of used receiving addresses and labels' },
        'separator',
        { label: 'Backup wallet…', action: to('wallets', 'backup'), disabled: noWallet, tip: 'Backup wallet to another location' },
        { label: 'Restore wallet…', action: to('wallets', 'restore'), tip: 'Restore a wallet from a backup file' },
        { label: 'Export watch-only wallet…', action: to('wallets', 'export'), disabled: noWallet || currentWatchOnly, tip: 'Export a watch-only version of the current wallet that can be restored onto another node.' },
        'separator',
        { label: 'Encrypt wallet…', action: to('wallets', 'encrypt'), disabled: noWallet || currentEncrypted || currentWatchOnly, tip: 'Encrypt the private keys that belong to your wallet' },
        { label: 'Change passphrase…', action: to('wallets', 'passphrase'), disabled: noWallet || !currentEncrypted, tip: 'Change the passphrase used for wallet encryption' },
        { label: 'Unlock wallet for a while…', action: to('wallets', 'unlock'), disabled: noWallet || !currentEncrypted },
        { label: 'Mask values', action: () => savePrefs({ mask: !prefs.mask }), checked: prefs.mask, shortcut: 'Ctrl+Shift+M', tip: 'Mask the values in the Overview tab' },
      ],
    },
    {
      label: 'Banking',
      items: [
        { label: 'Send', action: to('send'), shortcut: 'Alt+2', disabled: noWallet, tip: 'Send coins to a Chains address' },
        { label: 'Receive', action: to('receive'), shortcut: 'Alt+3', disabled: noWallet, tip: 'Request payments (generates QR codes and chains: URIs)' },
        { label: 'Transactions', action: to('transactions'), shortcut: 'Alt+4', disabled: noWallet || prefs.mask, tip: 'Browse transaction history' },
        { label: 'Open URI…', action: to('send', 'uri'), disabled: noWallet, tip: 'Open a chains: URI' },
        'separator',
        { label: 'Proof of funds', action: to('proof'), disabled: noWallet },
        { label: 'Multisig lounge', action: to('multisig'), disabled: noWallet },
        'separator',
        { label: 'Load PSBT from file…', action: to('psbt', 'file'), disabled: noWallet, tip: 'Load Partially Signed Chains Transaction' },
        { label: 'Load PSBT from clipboard…', action: to('psbt', 'clipboard'), disabled: noWallet, tip: 'Load Partially Signed Chains Transaction from clipboard' },
      ],
    },
    ];
  }

  function otherMenus(): Menu[] {
    return [
    {
      label: 'Use Chains',
      items: [
        { label: 'Timestamp file', action: to('timestamp') },
        'separator',
        ...(walletsOn
          ? ([
              { label: 'Sign message…', action: to('messages', 'sign'), disabled: noWallet, tip: 'Sign messages with your Chains addresses to prove you own them' },
              { label: 'Verify message…', action: to('messages', 'verify'), disabled: noWallet, tip: 'Verify messages to ensure they were signed with specified Chains addresses' },
              'separator',
            ] as Menu['items'])
          : []),
        { label: 'Sidechains', action: to('sidechains'), shortcut: 'Alt+5', tip: 'Deposit to sidechains, and manage sidechain proposals and withdrawal votes' },
      ],
    },
    {
      label: 'Chains',
      items: [
        { label: 'Sidechain nodes', action: to('sidechain-nodes'), tip: 'Install, start and stop the nodes of sidechains, and watch them' },
        { label: 'Solo mine', action: to('mine'), tip: 'Mine blocks with the processor of this computer' },
        { label: 'Sidechain proposals', action: to('sidechains', 'proposals'), tip: 'Propose sidechains, acknowledge proposals and vote on withdrawals in the blocks this node mines' },
        { label: 'Withdrawal votes', action: to('sidechains', 'withdrawals') },
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
        { label: 'Node window', action: to('node'), shortcut: 'Ctrl+Shift+D', tip: 'Open node debugging and diagnostic console' },
        { label: 'Options…', action: to('settings', 'main'), tip: 'Modify configuration options for Chains' },
        { label: 'Sync status…', action: () => (nav.dialog = 'sync') },
        {
          label: 'Theme',
          items: [
            ...THEMES.map((t) => ({ label: t.name, checked: prefs.theme === t.id, radio: true, action: () => savePrefs({ theme: t.id }) })),
            { label: 'Custom…', action: to('settings', 'display') },
          ],
        },
        { label: 'Increase font size', action: () => savePrefs({ fontAdjust: clampFontAdjust(prefs.fontAdjust + 1) }), shortcut: 'Ctrl++' },
        { label: 'Decrease font size', action: () => savePrefs({ fontAdjust: clampFontAdjust(prefs.fontAdjust - 1) }), shortcut: 'Ctrl+−' },
        { label: 'Command-line options', action: () => (nav.dialog = 'help'), tip: 'Show the Chains help message to get a list with possible Chains command-line options' },
        'separator',
        { label: `About ${app.info?.name ?? 'Chains'}`, action: () => (nav.dialog = 'about'), tip: `Show information about ${app.info?.name ?? 'Chains'}` },
        ...(app.info?.test || app.info?.devtools ? [{ label: 'Interface kit', action: to('kit') }] : []),
        'separator',
        { label: 'Exit', action: () => host.quit(), shortcut: 'Ctrl+Q', tip: 'Quit application' },
      ],
    },
    ];
  }
</script>

<div class="shell">
  <Sidebar />
  <div class="main">
    <!-- svelte-ignore a11y_no_static_element_interactions -->
    <header class="appbar" class:framed={frame.framed} onmousedown={barMouseDown}>
      <AppMenu {menus} />
      {#if frame.framed}<span class="title" translate="no">{frame.title}</span>{/if}
      <WindowButtons />
    </header>
    <div class="stage">
    <!-- Before the pages: the dialogs a page opens come after it, above it. -->
    <SyncOverlay />
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
        {:else if nav.page === 'sidechains'}
          <Sidechains />
        {:else if nav.page === 'sidechain-nodes'}
          <SidechainNodes />
        {:else if nav.page === 'mine'}
          <Mine />
        {:else if nav.page === 'explorer'}
          <Explorer />
        {:else if nav.page === 'timestamp'}
          <Timestamp />
        {:else if nav.page === 'crypto'}
          <Crypto />
        {:else if (page.wallet && noWallet) || (!walletsOn && nav.page === 'wallets')}
          <NoWallet {page} />
        {:else if nav.page === 'send'}
          <Send />
        {:else if nav.page === 'receive'}
          <Receive />
        {:else if nav.page === 'transactions'}
          <Transactions />
        {:else if nav.page === 'addresses'}
          <Addresses />
        {:else if nav.page === 'messages'}
          <Messages />
        {:else if nav.page === 'psbt'}
          <Psbt />
        {:else if nav.page === 'wallets'}
          <Wallets />
        {:else if nav.page === 'proof'}
          <Proof />
        {:else if nav.page === 'multisig'}
          <Multisig />
        {/if}
      {/key}
    </main>
    </div>
    <StatusBar />
  </div>
</div>
<TxNotifier />

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
  /* The title bar of the window (ThemedFrame): the menus, the title, the buttons of the window. */
  .appbar.framed {
    padding-block: 0;
    padding-inline-end: 0;
    user-select: none;
  }
  .appbar.framed :global(.menubar) {
    flex: 0 1 auto;
  }
  .title {
    flex: 1 1 0;
    min-width: 0;
    overflow: hidden;
    white-space: nowrap;
    text-overflow: ellipsis;
    text-align: center;
    color: var(--text-muted);
    font-size: 0.9rem;
  }
  .stage {
    position: relative;
    flex: 1;
    min-height: 0;
    display: flex;
    flex-direction: column;
  }
  .content {
    flex: 1;
    min-height: 0;
    overflow: auto;
    outline: none;
  }
</style>
