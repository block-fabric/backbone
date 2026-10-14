<script lang="ts">
  // The wallet shown, when the node has wallets loaded.
  import { selectWallet, wallets } from '../lib/state.svelte';
</script>

{#if wallets.list.length > 0}
  <label class="wallet">
    <span class="sr-only">Wallet</span>
    <select title="The wallet shown" value={wallets.current ?? ''} onchange={(e) => selectWallet((e.currentTarget as HTMLSelectElement).value)}>
      {#each wallets.list as w (w.name)}
        <option value={w.name}>{w.name === '' ? 'default wallet' : w.name}</option>
      {/each}
    </select>
  </label>
{/if}

<style>
  .wallet {
    display: flex;
    min-width: 0;
  }
  select {
    min-height: 2rem;
    padding-block: 0.2rem;
  }
</style>
