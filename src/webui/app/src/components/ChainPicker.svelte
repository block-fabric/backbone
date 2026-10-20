<script lang="ts">
  // The chain an action acts on (doc/unified-app.md §18): one chain, chosen in a list, remembered for the page. What
  // happens next (the form, the address, its checks) is that chain's.
  import { t } from '../lib/i18n.svelte';
  import { chainEntries, setActionChain, type ActionView, type ChainEntry } from '../lib/chains.svelte';

  let { page, value = $bindable(), entries = chainEntries(), label = 'On' }: { page: ActionView; value: string; entries?: ChainEntry[]; label?: string } = $props();

  function pick(ev: Event) {
    value = (ev.currentTarget as HTMLSelectElement).value;
    setActionChain(page, value);
  }
</script>

{#if entries.length > 1}
  <label class="chain-picker">
    <span class="muted small">{t(label)}</span>
    <select aria-label={t('Chain')} {value} onchange={pick} data-chain-picker={page}>
      {#each entries as e (e.id)}
        <option value={e.id} translate="no">{e.name}{e.running ? '' : ` (${t('not running')})`}</option>
      {/each}
    </select>
  </label>
{/if}

<style>
  .chain-picker {
    display: inline-flex;
    align-items: center;
    gap: 0.45rem;
  }
  .small {
    font-size: 0.85rem;
  }
  select {
    min-width: 10rem;
    font-weight: 650;
  }
</style>
