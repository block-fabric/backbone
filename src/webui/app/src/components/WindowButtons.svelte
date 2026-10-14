<script lang="ts">
  // The buttons of a window the page frames: minimize, maximize or restore, close (ThemedFrame).
  import { closeWindow, frame, minimize, toggleMaximized } from '../lib/frame.svelte';
</script>

{#if frame.framed}
  <div class="buttons">
    <button type="button" class="wb" title="Minimize" aria-label="Minimize" onclick={minimize}>
      <svg viewBox="0 0 12 12" aria-hidden="true"><path d="M2 6.5h8" /></svg>
    </button>
    <button type="button" class="wb" title={frame.maximized ? 'Restore' : 'Maximize'} aria-label={frame.maximized ? 'Restore' : 'Maximize'} onclick={toggleMaximized}>
      {#if frame.maximized}
        <svg viewBox="0 0 12 12" aria-hidden="true"><path d="M3.5 4.5h4v4h-4zM4.5 4.5V3h4v4H7.5" /></svg>
      {:else}
        <svg viewBox="0 0 12 12" aria-hidden="true"><path d="M2.5 2.5h7v7h-7z" /></svg>
      {/if}
    </button>
    <button type="button" class="wb close" title="Close" aria-label="Close" onclick={closeWindow}>
      <svg viewBox="0 0 12 12" aria-hidden="true"><path d="M3 3l6 6M9 3l-6 6" /></svg>
    </button>
  </div>
{/if}

<style>
  .buttons {
    display: flex;
    align-self: stretch;
    margin-inline-start: auto;
  }
  .wb {
    display: grid;
    place-items: center;
    width: 2.6rem;
    border: 0;
    background: none;
    color: var(--text-muted);
    cursor: pointer;
  }
  .wb svg {
    width: 0.8rem;
    height: 0.8rem;
    fill: none;
    stroke: currentColor;
    stroke-width: 1.2;
  }
  .wb:hover,
  .wb:focus-visible {
    background: var(--raised);
    color: var(--text);
  }
  .wb.close:hover,
  .wb.close:focus-visible {
    background: var(--negative);
    color: var(--on-accent);
  }
</style>
