<script lang="ts">
  // A card of the dashboard: a title that opens the full page, what it shows, its quick actions.
  import type { Snippet } from 'svelte';
  import Icon, { type IconName } from '../../components/Icon.svelte';

  let {
    title,
    icon,
    open,
    openLabel,
    wide = false,
    children,
    actions,
  }: {
    title: string;
    icon: IconName;
    /** Opens the full page of the card. */
    open: () => void;
    openLabel: string;
    wide?: boolean;
    children: Snippet;
    actions?: Snippet;
  } = $props();

  const id = `card-${Math.random().toString(36).slice(2)}`;
</script>

<section class="card panel" class:wide aria-labelledby={id}>
  <header>
    <span class="icon"><Icon name={icon} size={18} /></span>
    <h2 {id}>{title}</h2>
    <button type="button" class="btn ghost small open" onclick={open} aria-label={openLabel} title={openLabel}>
      <span class="word">Open</span><Icon name="arrow" size={15} />
    </button>
  </header>
  <div class="body">{@render children()}</div>
  {#if actions}<footer>{@render actions()}</footer>{/if}
</section>

<style>
  .card {
    display: flex;
    flex-direction: column;
    min-width: 0;
    transition: border-color 120ms;
  }
  .card:hover {
    border-color: var(--line-strong);
  }
  .wide {
    grid-column: span 2;
  }
  header {
    display: flex;
    align-items: center;
    gap: 0.55rem;
    padding: 0.8rem 0.8rem 0.4rem 1rem;
  }
  .icon {
    display: inline-flex;
    padding: 0.3rem;
    border-radius: var(--radius-s);
    background: var(--accent-soft);
    color: var(--accent);
  }
  h2 {
    flex: 1;
    font-size: 1rem;
  }
  .open {
    color: var(--text-muted);
  }
  .word {
    font-size: 0.85rem;
  }
  .body {
    flex: 1;
    padding: 0.3rem 1rem 0.9rem;
    min-width: 0;
  }
  footer :global(.btn) {
    min-height: 1.9rem;
    padding: 0 0.7rem;
    font-size: 0.9rem;
  }
  footer {
    display: flex;
    flex-wrap: wrap;
    gap: 0.45rem;
    padding: 0.65rem 1rem;
    border-top: 1px solid var(--line);
    background: var(--surface-2);
    border-radius: 0 0 var(--radius-m) var(--radius-m);
  }
  @media (max-width: 1100px) {
    .wide {
      grid-column: auto;
    }
  }
</style>
