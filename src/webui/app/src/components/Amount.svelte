<script lang="ts">
  // An amount in the unit chosen in the status bar; hidden behind dots in privacy mode.
  import { formatAmount, unitInfo, type Unit } from '../lib/units';
  import { prefs } from '../lib/state.svelte';

  let {
    sats,
    unit = undefined,
    plus = false,
    showUnit = true,
    colored = false,
    masked = undefined,
  }: { sats: bigint | number | null | undefined; unit?: Unit; plus?: boolean; showUnit?: boolean; colored?: boolean; masked?: boolean } = $props();

  const u = $derived(unit ?? prefs.unit);
  const hidden = $derived(masked ?? prefs.mask);
  const value = $derived(sats === null || sats === undefined ? null : typeof sats === 'bigint' ? sats : BigInt(Math.round(sats)));
  const text = $derived(value === null ? '–' : formatAmount(value, u, { plus }));
  const sign = $derived(value === null ? 0 : value > 0n ? 1 : value < 0n ? -1 : 0);
</script>

<span class="amount num" class:pos={colored && sign > 0} class:neg={colored && sign < 0} title={hidden ? 'Hidden: privacy mode' : undefined}>
  {#if hidden}<span aria-label="hidden amount">••••••</span>{:else}{text}{/if}{#if showUnit}<span class="unit">&nbsp;{unitInfo(u).short}</span>{/if}
</span>

<style>
  .amount {
    white-space: nowrap;
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
