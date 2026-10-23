<script lang="ts">
  // The mark of the product, its name and the network (betanet, testnet...). The product has no logo: its mark is
  // its initial on a tile (a text mark), its name the host's (lib/brand.svelte.ts).
  import { product, productInitial } from '../lib/brand.svelte';
  import { t } from '../lib/i18n.svelte';
  import { app } from '../lib/state.svelte';

  let { name = true, onclick, label }: { name?: boolean; onclick?: () => void; label?: string } = $props();
  const shownLabel = $derived(label ?? t('%1: home', product()));
</script>

{#snippet content()}
  <svg class="mark" viewBox="0 0 32 32" aria-hidden="true" data-testid="brand-mark">
    <rect x="3.2" y="3.2" width="25.6" height="25.6" rx="7" />
    <text x="16" y="16.6" text-anchor="middle" dominant-baseline="middle">{productInitial()}</text>
  </svg>
  {#if name}
    <span class="name" translate="no">{product()}</span>
    {#if app.info?.networkLabel}<span class="badge warning net">{app.info.networkLabel}</span>{/if}
  {/if}
{/snippet}

{#if onclick}
  <button type="button" class="brand" {onclick} aria-label={shownLabel} title={shownLabel}>{@render content()}</button>
{:else}
  <div class="brand">{@render content()}</div>
{/if}

<style>
  .brand {
    display: flex;
    align-items: center;
    gap: 0.55rem;
    padding: 0.2rem 0.4rem;
    border: 0;
    border-radius: var(--radius-s);
    background: none;
    color: var(--text);
    font: inherit;
  }
  button.brand {
    cursor: pointer;
  }
  button.brand:hover {
    background: var(--raised);
  }
  .mark {
    width: 1.9rem;
    height: 1.9rem;
    flex: none;
  }
  .mark rect {
    fill: none;
    stroke: var(--accent);
    stroke-width: 2.4;
  }
  .mark text {
    fill: var(--accent);
    font-size: 17px;
    font-weight: 800;
    font-family: inherit;
  }
  .name {
    font-size: 1.15rem;
    font-weight: 700;
    letter-spacing: -0.02em;
  }
</style>
