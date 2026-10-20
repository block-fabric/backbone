<script lang="ts">
  // An amount of a module (a decimal its pages or its node gave, in its own coin), hidden as <Amount> hides the app's
  // own in privacy mode (prefs.mask): the same placeholder, the same words.
  import { prefs } from '../lib/state.svelte';
  import SafeText from './SafeText.svelte';

  let { text, unit = '', plus = false }: { text: string | null | undefined; unit?: string; plus?: boolean } = $props();
  const shown = $derived(text === null || text === undefined ? '–' : plus && !text.startsWith('-') && Number(text) > 0 ? `+${text}` : text);
</script>

<span class="amount num" title={prefs.mask ? 'Hidden: privacy mode' : undefined} data-module-amount>
  {#if prefs.mask}<span aria-label="hidden amount">••••••</span>{:else}<SafeText text={shown} />{/if}{#if unit}<span class="unit">&nbsp;<SafeText text={unit} /></span>{/if}
</span>

<style>
  .amount {
    white-space: nowrap;
  }
  .unit {
    color: var(--text-muted);
  }
</style>
