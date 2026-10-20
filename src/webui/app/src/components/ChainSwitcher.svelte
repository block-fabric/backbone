<script lang="ts">
  // The chain switcher of the sidebar (doc/unified-app.md): Chains, then each sidechain this app runs,
  // with a dot of how it is doing. Only there once a sidechain is turned on: Chains alone shows nothing.
  import { t } from '../lib/i18n.svelte';
  import { app } from '../lib/state.svelte';
  import { hub, refreshSidechains, selectChain, watchSidechains } from '../lib/sidechains.svelte';
  import { switcherItems } from '../lib/unified';
  import SafeText from './SafeText.svelte';

  let { collapsed = false }: { collapsed?: boolean } = $props();

  $effect(() => {
    if (app.phase !== 'ready') return;
    watchSidechains();
    void refreshSidechains();
  });

  const items = $derived(switcherItems(hub.list));
  const toneWords: Record<string, string> = { ok: 'Running', busy: 'Starting or stopping', warn: 'Has a problem', bad: 'Stopped: needs your attention', off: 'Off' };
</script>

{#if items.length > 0}
  <section class="switcher" class:collapsed aria-label={t('Chains this app runs')}>
    {#if !collapsed}<div class="caps head">Chains</div>{/if}
    <div class="list" role="radiogroup" aria-label={t('Chain shown')}>
      {#each items as item (item.id)}
        <button
          type="button"
          role="radio"
          class="chain"
          class:current={hub.selected === item.id}
          aria-checked={hub.selected === item.id}
          data-chain={item.id}
          title={t(toneWords[item.tone])}
          onclick={() => selectChain(item.id)}
        >
          <span class="mark" aria-hidden="true" data-chain={item.id}><SafeText text={item.id === 'chains' ? 'C' : item.id.slice(0, 1).toUpperCase()} /></span>
          {#if !collapsed}<span class="name"><SafeText text={item.id === 'chains' ? 'Chains' : item.name} /></span>{/if}
          <span class="dot {item.tone}" aria-label={t(toneWords[item.tone])}></span>
        </button>
      {/each}
    </div>
  </section>
{/if}

<style>
  .switcher {
    margin: 0.2rem 0.6rem 0.6rem;
    padding: 0.5rem;
    border: 1px solid var(--line);
    border-radius: var(--radius-l);
    background: var(--surface-2);
  }
  .head {
    padding: 0 0.35rem 0.35rem;
  }
  .list {
    display: flex;
    flex-direction: column;
    gap: 0.15rem;
  }
  .chain {
    display: flex;
    align-items: center;
    gap: 0.55rem;
    width: 100%;
    padding: 0.35rem 0.45rem;
    border: 0;
    border-radius: var(--radius-m);
    background: transparent;
    color: var(--text);
    font: inherit;
    text-align: left;
    cursor: pointer;
  }
  .chain:hover {
    background: var(--raised);
  }
  .chain.current {
    background: var(--accent-soft);
    color: var(--accent-strong);
    font-weight: 600;
  }
  .mark {
    display: inline-grid;
    place-items: center;
    width: 1.5rem;
    height: 1.5rem;
    flex: none;
    border-radius: 0.45rem;
    background: var(--raised);
    color: var(--text-muted);
    font-size: 0.78rem;
    font-weight: 700;
  }
  .chain.current .mark {
    background: var(--accent);
    color: var(--on-accent);
  }
  .name {
    flex: 1;
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .dot {
    width: 0.5rem;
    height: 0.5rem;
    flex: none;
    border-radius: 50%;
    background: var(--text-faint);
  }
  .dot.ok {
    background: var(--positive);
  }
  .dot.busy {
    background: var(--accent);
  }
  .dot.warn {
    background: var(--warning);
  }
  .dot.bad {
    background: var(--negative);
  }
  .collapsed {
    margin: 0.2rem 0.35rem 0.5rem;
    padding: 0.3rem;
  }
  .collapsed .chain {
    justify-content: center;
    position: relative;
  }
  .collapsed .dot {
    position: absolute;
    right: 0.2rem;
    top: 0.2rem;
  }
</style>
