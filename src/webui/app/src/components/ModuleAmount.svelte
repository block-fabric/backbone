<script lang="ts">
  // An amount of a module (a decimal its pages or its node gave, in its own coin), hidden as <Amount> hides the app's
  // own in privacy mode (prefs.mask): the same placeholder, the same words.
  import { prefs } from '../lib/state.svelte';
  import SafeText from './SafeText.svelte';

  // coins: the module's own coin, with its 8 decimals as the app writes its own (an asset keeps its own decimals).
  let { text: given, unit = '', plus = false, coins = false }: { text: string | number | null | undefined; unit?: string; plus?: boolean; coins?: boolean } = $props();
  // A number given (a node's answer not made text on the way): its text, as written for coins.
  const text = $derived(typeof given === 'number' ? (Number.isFinite(given) ? (coins ? given.toFixed(8) : String(given)) : null) : given);
  const padded = $derived(text && coins && /^-?[0-9]+(\.[0-9]{0,8})?$/.test(text) ? (text.includes('.') ? text.padEnd(text.indexOf('.') + 9, '0') : `${text}.00000000`) : text);
  const shown = $derived(padded === null || padded === undefined ? '–' : plus && !padded.startsWith('-') && Number(padded) > 0 ? `+${padded}` : padded);
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
