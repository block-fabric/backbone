<script lang="ts">
  // An amount of a module (a decimal its pages or its node gave, in its own coin), hidden as <Amount> hides the app's
  // own in privacy mode (prefs.mask): the same placeholder, the same words. Drawn as <Amount> draws the app's own
  // (doc/unified-app.md §20.3): groups of three digits, the decimals dimmed, the unit after the figure; the digits are
  // the node's own text, never rounded.
  import { prefs } from '../lib/state.svelte';
  import SafeText from './SafeText.svelte';

  // coins: the module's own coin, with its 8 decimals as the app writes its own (an asset keeps its own decimals).
  let {
    text: given,
    unit = '',
    plus = false,
    coins = false,
    colored = false,
  }: { text: string | number | null | undefined; unit?: string; plus?: boolean; coins?: boolean; colored?: boolean } = $props();
  // A number given (a node's answer not made text on the way): its text, as written for coins.
  const text = $derived(typeof given === 'number' ? (Number.isFinite(given) ? (coins ? given.toFixed(8) : String(given)) : null) : given);
  const padded = $derived(text && coins && /^-?[0-9]+(\.[0-9]{0,8})?$/.test(text) ? (text.includes('.') ? text.padEnd(text.indexOf('.') + 9, '0') : `${text}.00000000`) : text);
  const sign = $derived(padded && /^-?[0-9]/.test(padded) ? (padded.startsWith('-') ? -1 : /[1-9]/.test(padded) ? 1 : 0) : 0);
  // A plain decimal: its whole part in groups of three (a thin space, as the app's own), its decimals apart.
  const parts = $derived.by(() => {
    if (padded === null || padded === undefined) return null;
    const m = /^(-?)([0-9]+)(\.[0-9]+)?$/.exec(padded);
    if (!m) return null;
    const whole = m[2].replace(/\B(?=(\d{3})+(?!\d))/g, '\u2009');
    const lead = m[1] ? '-' : plus && sign > 0 ? '+' : '';
    return { whole: `${lead}${whole}`, dec: m[3] ?? '' };
  });
</script>

<span class="amount num" class:pos={colored && sign > 0} class:neg={colored && sign < 0} title={prefs.mask ? 'Hidden: privacy mode' : undefined} data-module-amount
  >{#if prefs.mask}<span aria-label="hidden amount">••••••</span>{:else if parts}{parts.whole}{#if parts.dec}<span class="dec">{parts.dec}</span>{/if}{:else}<SafeText text={padded ?? '–'} />{/if}{#if unit}<span class="unit">&nbsp;<SafeText text={unit} /></span>{/if}</span
>

<style>
  .amount {
    white-space: nowrap;
  }
  .dec {
    color: var(--text-muted);
    font-weight: 450;
  }
  .pos .dec,
  .neg .dec {
    color: inherit;
  }
  .unit {
    color: var(--text-muted);
    font-size: 0.85em;
    font-weight: 500;
  }
  .pos {
    color: var(--positive);
  }
  .neg {
    color: var(--negative);
  }
</style>
