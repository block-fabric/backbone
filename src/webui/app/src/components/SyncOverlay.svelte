<script lang="ts">
  // The sync overlay (ModalOverlay of chains-qt): while the node catches up, the blocks left, the
  // time of the last block, the progress, its speed per hour and the time left; Hide (or Escape).
  import Dialog from './Dialog.svelte';
  import { formatTime } from '../lib/format';
  import { nav } from '../lib/nav.svelte';
  import { app, node, walletsEnabled } from '../lib/state.svelte';
  import { MAX_BLOCK_TIME_GAP } from '../lib/sync';
  import { blocksLeft, estimate, overlay } from '../lib/syncoverlay';

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
    // Closed by the user: it no longer opens by itself (ModalOverlay userClosed).
    if (nav.dialog === 'sync') overlay.userClosed = true;
    nav.dialog = null;
  }

  // As BitcoinGUI::setNumBlocks with a wallet: the overlay shows while the node is behind, unless the user
  // closed it, and goes once it has caught up.
  const behind = $derived(!!tip && Math.floor(now / 1000) - tip.time >= MAX_BLOCK_TIME_GAP);
  $effect(() => {
    if (!tip || !walletsEnabled()) return;
    if (behind) {
      if (nav.dialog === null && !overlay.userClosed && !overlay.shown) {
        overlay.shown = true;
        nav.dialog = 'sync';
      }
    } else {
      overlay.shown = false;
      if (nav.dialog === 'sync' && !overlay.byUser) nav.dialog = null;
    }
  });
  // Opened from a menu, the label of the status bar or the warning of the overview: the user's own.
  $effect(() => {
    if (nav.dialog !== 'sync') overlay.byUser = false;
    else if (!overlay.shown) overlay.byUser = true;
  });
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
