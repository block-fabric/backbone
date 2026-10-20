<script lang="ts">
  // A chain named, with its mark (the first letter, in the colour of its pack if it has one): where a row, a card or a
  // form says which chain it is about. The name is the app's (its catalog), never what a chain's pages say.
  import SafeText from './SafeText.svelte';
  import { packs } from '../lib/packs.svelte';
  import { HOST } from '../lib/chains.svelte';

  let { id, name, small = false }: { id: string; name: string; small?: boolean } = $props();
  const color = $derived(id === HOST ? '' : (packs.list.find((p) => p.stem === id)?.brand?.color ?? ''));
  const safeColor = $derived(/^#[0-9a-fA-F]{3,8}$/.test(color) ? color : '');
</script>

<span class="chain-badge" class:small data-chain-badge={id}>
  <span class="mark" class:host={id === HOST} style:background={safeColor || undefined} aria-hidden="true">{name.slice(0, 1)}</span>
  <SafeText text={name} />
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
    color: var(--text);
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
