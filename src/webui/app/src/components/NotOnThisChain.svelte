<script lang="ts">
  // A page of what some sidechains add (Staking, Assets, Names, Markets) while the top bar shows a chain that has none
  // of it (doc/unified-app.md §19): said plainly, with the chains that have it a click away (selecting one, or All).
  import { t } from '../lib/i18n.svelte';
  import SafeText from './SafeText.svelte';
  import ChainBadge from './ChainBadge.svelte';
  import { ALL, chainEntries, selectedChain, setSelectedChain, type ChainEntry } from '../lib/chains.svelte';

  let { what, have, none }: { what: string; have: ChainEntry[]; none: string } = $props();
  const sel = $derived(chainEntries().find((e) => e.id === selectedChain()));
</script>

<section class="panel not-here" data-not-on-chain={sel?.id ?? ALL}>
  {#if have.length === 0}
    <p class="muted">{t(none)}</p>
  {:else}
    <p><strong>{#if sel}{t('%1 has no %2.', sel.name, t(what))}{:else}{t(none)}{/if}</strong></p>
    <p class="muted">{t('Show it on:')}</p>
    <div class="choices">
      {#each have as e (e.id)}
        <button type="button" class="btn" onclick={() => setSelectedChain(e.id)} data-show-chain={e.id}><ChainBadge id={e.id} name={e.name} small /></button>
      {/each}
      {#if have.length > 1}<button type="button" class="btn ghost" onclick={() => setSelectedChain(ALL)}>{t('All chains')}</button>{/if}
    </div>
    {#if sel}<p class="faint small"><SafeText text={t('The chain shown is chosen at the top, next to the page\'s name (Ctrl+] for the next).')} /></p>{/if}
  {/if}
</section>

<style>
  .not-here {
    padding: 1.2rem 1.3rem;
  }
  .not-here p {
    margin: 0 0 0.6rem;
  }
  .choices {
    display: flex;
    flex-wrap: wrap;
    gap: 0.5rem;
    margin-bottom: 0.8rem;
  }
  .small {
    font-size: 0.84rem;
  }
</style>
