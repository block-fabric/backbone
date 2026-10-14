<script lang="ts">
  // The title bar of a window the page frames, before the main window shows (start, intro, an error).
  import WindowButtons from './WindowButtons.svelte';
  import { barMouseDown, frame } from '../lib/frame.svelte';
</script>

{#if frame.framed}
  <!-- svelte-ignore a11y_no_static_element_interactions -->
  <header class="framebar" onmousedown={barMouseDown}>
    <svg class="mark" viewBox="0 0 32 32" aria-hidden="true">
      <rect x="3" y="9" width="12" height="14" rx="3.5" />
      <rect x="17" y="9" width="12" height="14" rx="3.5" />
      <path d="M11 16h10" />
    </svg>
    <span class="title" translate="no">{frame.title}</span>
    <WindowButtons />
  </header>
{/if}

<style>
  .framebar {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    min-height: 2.2rem;
    padding-inline-start: 0.7rem;
    border-bottom: 1px solid var(--line);
    background: var(--surface);
    user-select: none;
  }
  .mark {
    width: 1.1rem;
    height: 1.1rem;
    fill: none;
    stroke: var(--accent-text);
    stroke-width: 2.6;
  }
  .title {
    overflow: hidden;
    white-space: nowrap;
    text-overflow: ellipsis;
    color: var(--text-muted);
    font-size: 0.9rem;
  }
</style>
