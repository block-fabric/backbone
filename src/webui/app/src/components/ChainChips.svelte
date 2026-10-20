<script lang="ts">
  // The filter of chains of a page that shows many (doc/unified-app.md §18): All, then each chain this app runs. What
  // the user picks is remembered for that page (prefs.chainViews). With only the app's own chain there is nothing to
  // filter, and nothing is shown.
  import { t } from '../lib/i18n.svelte';
  import SafeText from './SafeText.svelte';
  import { ALL, chainEntries, setViewFilter, viewFilter, type ChainEntry, type ReadView } from '../lib/chains.svelte';

  let { page, entries = chainEntries(), label = 'Chains' }: { page: ReadView; entries?: ChainEntry[]; label?: string } = $props();
  const value = $derived(viewFilter(page, entries));
</script>

{#if entries.length > 1}
  <div class="chain-chips" role="radiogroup" aria-label={t(label)} data-chain-chips={page}>
    <button type="button" role="radio" aria-checked={value === ALL} class="chip" class:on={value === ALL} data-chain-chip="all" onclick={() => setViewFilter(page, ALL)}>{t('All')}</button>
    {#each entries as e (e.id)}
      <button type="button" role="radio" aria-checked={value === e.id} class="chip" class:on={value === e.id} class:off={!e.running} data-chain-chip={e.id} onclick={() => setViewFilter(page, e.id)}><SafeText text={e.name} /></button>
    {/each}
  </div>
{/if}

<style>
  .chain-chips {
    display: inline-flex;
    flex-wrap: wrap;
    gap: 0.3rem;
    padding: 0.2rem;
    border: 1px solid var(--line);
    border-radius: 999px;
    background: var(--surface-2);
  }
  .chip {
    padding: 0.2rem 0.75rem;
    border: 0;
    border-radius: 999px;
    background: none;
    color: var(--text-muted);
    font: inherit;
    font-size: 0.88rem;
    font-weight: 600;
    cursor: pointer;
  }
  .chip:hover {
    background: var(--raised);
    color: var(--text);
  }
  .chip.on {
    background: var(--accent);
    color: var(--on-accent);
  }
  .chip.off:not(.on) {
    opacity: 0.6;
  }
</style>
