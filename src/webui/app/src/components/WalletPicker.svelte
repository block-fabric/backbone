<script lang="ts">
  // The wallet shown, when the node has wallets loaded. As a pill in the top bars (`pill`).
  import Icon from './Icon.svelte';
  import { selectWallet, wallets } from '../lib/state.svelte';

  let { pill = false }: { pill?: boolean } = $props();
</script>

{#if wallets.list.length > 0}
  <label class="wallet" class:pill-wrap={pill}>
    <span class="sr-only">Wallet</span>
    {#if pill}<span class="wicon" aria-hidden="true"><Icon name="wallet" size={13} /></span>{/if}
    <select class:pill title="The wallet shown" value={wallets.current ?? ''} onchange={(e) => selectWallet((e.currentTarget as HTMLSelectElement).value)}>
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
  .pill-wrap {
    position: relative;
    align-items: center;
  }
  .wicon {
    position: absolute;
    left: 0.35rem;
    display: inline-grid;
    place-items: center;
    width: 1.45rem;
    height: 1.45rem;
    border-radius: 50%;
    background: var(--accent);
    color: var(--on-accent);
    pointer-events: none;
  }
  select.pill {
    min-height: 2.35rem;
    max-width: 14rem;
    padding-left: 2.2rem;
    font-weight: 600;
    text-overflow: ellipsis;
  }
</style>
