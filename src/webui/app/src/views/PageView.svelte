<script lang="ts">
  // The page shown, whatever the layout around it.
  import Addresses from '../pages/Addresses.svelte';
  import Kit from '../pages/Kit.svelte';
  import Messages from '../pages/Messages.svelte';
  import NodeWindow from '../pages/NodeWindow.svelte';
  import Overview from '../pages/Overview.svelte';
  import Planned from '../pages/Planned.svelte';
  import Psbt from '../pages/Psbt.svelte';
  import Receive from '../pages/Receive.svelte';
  import Send from '../pages/Send.svelte';
  import Settings from '../pages/Settings.svelte';
  import Transactions from '../pages/Transactions.svelte';
  import Wallets from '../pages/Wallets.svelte';
  import Home from './home/Home.svelte';
  import { nav, PAGES } from '../lib/nav.svelte';
  import { wallets } from '../lib/state.svelte';

  const noWallet = $derived(wallets.list.length === 0);
  const page = $derived(PAGES[nav.page]);
</script>

{#key nav.page}
  {#if nav.page === 'home'}
    <Home />
  {:else if nav.page === 'overview'}
    <Overview />
  {:else if nav.page === 'node'}
    <NodeWindow />
  {:else if nav.page === 'settings'}
    <Settings />
  {:else if nav.page === 'kit'}
    <Kit />
  {:else if page.wallet && noWallet}
    <Planned {page} note="open or create a wallet first" />
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
  {:else}
    <Planned {page} />
  {/if}
{/key}
