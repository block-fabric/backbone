<script lang="ts">
  // The sync overlay (ModalOverlay of chains-qt): while the node catches up, the blocks left, the
  // time of the last block, the progress, its speed per hour and the time left; Hide (or Escape).
  import Dialog from './Dialog.svelte';
  import { formatTime } from '../lib/format';
  import { nav } from '../lib/nav.svelte';
  import { app, node } from '../lib/state.svelte';
  import { blocksLeft, estimate } from '../lib/syncoverlay';

  let now = $state(Date.now());
  $effect(() => {
    const timer = setInterval(() => (now = Date.now()), 1000);
    return () => clearInterval(timer);
  });

  const tip = $derived(node.tip);
  const est = $derived(estimate(node.progressSamples, now));
  const left = $derived(
    tip ? blocksLeft(tip.height, node.header?.height ?? tip.height, node.header?.time ?? 0, app.info?.blockSpacing ?? 600, Math.floor(now / 1000), node.presync ?? undefined) : 'Unknown…',
  );
  const pct = $derived(tip ? (tip.progress * 100).toFixed(2) : '0.00');

  function close() {
    nav.dialog = null;
  }
</script>

{#if nav.dialog === 'sync'}
  <Dialog open={true} title="Synchronizing with the network" onclose={close}>
    <p>Recent transactions may not yet be visible, and therefore your wallet's balance might be incorrect. This information will be correct once your wallet has finished synchronizing with the network, as detailed below.</p>
    <p class="warn">Attempting to spend coins that are affected by not-yet-displayed transactions will not be accepted by the network.</p>
    <progress max="100" value={Number(pct)} aria-label="Progress"></progress>
    <dl class="facts">
      <dt>Number of blocks left</dt><dd id="sync-left">{left}</dd>
      <dt>Last block time</dt><dd>{tip ? formatTime(tip.time) : 'Unknown…'}</dd>
      <dt>Progress</dt><dd class="num">{pct}%</dd>
      <dt>Progress increase per hour</dt><dd class="num">{est.perHour}</dd>
      <dt>Estimated time left until synced</dt><dd>{est.timeLeft}</dd>
    </dl>
    {#snippet actions()}<button type="button" class="btn primary" onclick={close}>Hide</button>{/snippet}
  </Dialog>
{/if}

<style>
  .warn {
    color: var(--warning);
  }
  progress {
    width: 100%;
    margin: 0.4rem 0 1rem;
  }
</style>
