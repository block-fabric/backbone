<script lang="ts">
  // An asset's logo (lib/assetlogos.svelte.ts), or its first letter in a round placeholder until it has one (or when it
  // has none). An <img> of a checked data: URL only: never markup of a chain.
  import { assetLogo } from '../lib/assetlogos.svelte';

  let { stem, id, label, size = 1.6 }: { stem: string; id: string; label: string; size?: number } = $props();
  const url = $derived(assetLogo(stem, id));
  const letter = $derived((label.replace(/^0x/, '').match(/[\p{L}\p{N}]/u)?.[0] ?? '?').toUpperCase());
</script>

<span class="asset-logo" style:--size="{size}rem" data-asset-logo={url ? 'image' : 'letter'} aria-hidden="true">
  {#if url}<img src={url} alt="" width="64" height="64" draggable="false" />{:else}{letter}{/if}
</span>

<style>
  .asset-logo {
    display: inline-grid;
    place-items: center;
    flex: none;
    width: var(--size);
    height: var(--size);
    border-radius: 50%;
    overflow: hidden;
    background: var(--accent-soft);
    color: var(--accent-strong);
    font-size: calc(var(--size) * 0.45);
    font-weight: 800;
    vertical-align: middle;
  }
  img {
    width: 100%;
    height: 100%;
    object-fit: cover;
  }
</style>
