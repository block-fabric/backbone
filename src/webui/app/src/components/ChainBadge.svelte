<script lang="ts">
  // A chain named, with its mark (the first letter, in the colour of its pack if it has one): where a row, a card or a
  // form says which chain it is about. The name is the app's (its catalog), never what a chain's pages say.
  import SafeText from './SafeText.svelte';
  import { HOST } from '../lib/chains.svelte';
  import { chainColor } from '../lib/chaincolors';

  // markOnly: the mark alone (the name then in the tooltip and for a screen reader).
  let { id, name, small = false, markOnly = false }: { id: string; name: string; small?: boolean; markOnly?: boolean } = $props();
  const color = $derived(id === HOST ? '' : chainColor(id));
</script>

<span class="chain-badge" class:small data-chain-badge={id} title={markOnly ? name : undefined}>
  <span class="mark" class:host={id === HOST} style:background={color || undefined} aria-hidden="true">{name.slice(0, 1).toUpperCase()}</span>
  {#if markOnly}<span class="sr-only"><SafeText text={name} /></span>{:else}<SafeText text={name} />{/if}
</span>

<style>
  .chain-badge {
    display: inline-flex;
    align-items: center;
    gap: 0.4rem;
    font-weight: 650;
    white-space: nowrap;
  }
  .mark {
    display: inline-grid;
    place-items: center;
    width: 1.45rem;
    height: 1.45rem;
    border-radius: 50%;
    background: var(--raised);
    color: #fff;
    font-size: 0.72rem;
    font-weight: 800;
  }
  .mark.host {
    background: var(--accent);
    color: var(--on-accent);
  }
  .small .mark {
    width: 1.15rem;
    height: 1.15rem;
    font-size: 0.62rem;
  }
  .small {
    font-size: 0.85rem;
  }
</style>
