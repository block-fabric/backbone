<script lang="ts">
  // A page that is not built yet: what it will do, and the functions of chains-qt it takes over.
  import Icon from '../components/Icon.svelte';
  import type { PageInfo } from '../lib/nav.svelte';

  let { page, note = '' }: { page: PageInfo; note?: string } = $props();
</script>

<div class="page planned">
  <header class="page-head"><h1>{page.title}</h1></header>
  <div class="panel body">
    <Icon name={page.icon} size={40} />
    <div>
      <p class="summary">{page.summary}.</p>
      <p class="muted">
        This page is being rebuilt for the new interface{note ? `: ${note}` : ''}. Until it is here, the same can be done with the
        console of the node window.
      </p>
      {#if page.parity.length}
        <p class="faint ids">Parity items: {page.parity.join(', ')}</p>
      {/if}
    </div>
  </div>
</div>

<style>
  .body {
    display: flex;
    gap: 1.2rem;
    align-items: flex-start;
    margin-top: 1rem;
    padding: 1.5rem;
    color: var(--accent);
  }
  .body > div {
    color: var(--text);
  }
  .summary {
    font-size: 1.1rem;
    font-weight: 550;
  }
  .ids {
    font-size: 0.85rem;
  }
</style>
