<script lang="ts">
  // An asset of a chain's DEX by its mark: the chain's coin by the chain's own mark, an asset by its logo (or its letter).
  import ChainBadge from '../ChainBadge.svelte';
  import AssetLogo from '../AssetLogo.svelte';
  import { COIN_ID } from '../../lib/dex.svelte';

  let { stem, chainName, id, label, size = 1.5 }: { stem: string; chainName: string; id: string; label: string; size?: number } = $props();
</script>

{#if id === COIN_ID}
  <span class="coin" style:--size="{size}rem"><ChainBadge id={stem} name={chainName} small markOnly /></span>
{:else}
  <AssetLogo {stem} {id} {label} {size} />
{/if}

<style>
  .coin {
    display: inline-grid;
    place-items: center;
    width: var(--size);
    height: var(--size);
    flex: none;
  }
</style>
