<script lang="ts">
  // The module switcher (doc/unified-app.md §14): the app's own pages, then each sidechain this app runs, as modules
  // of one window. Choosing one puts its pages in the sidebar and shows them in the main area; nothing else changes
  // (the same header, status bar, bell and palette). A dot says how each one is doing. Only there once a sidechain
  // is turned on: the app alone shows nothing.
  import { t } from '../lib/i18n.svelte';
  import { app } from '../lib/state.svelte';
  import { hub, refreshSidechains, selectChain, watchSidechains } from '../lib/sidechains.svelte';
  import { productName } from '../lib/modules.svelte';
  import { switcherItems } from '../lib/unified';
  import SafeText from './SafeText.svelte';

  let { collapsed = false }: { collapsed?: boolean } = $props();

  $effect(() => {
    if (app.phase !== 'ready') return;
    watchSidechains();
    void refreshSidechains();
  });

  const items = $derived(switcherItems(hub.list, productName()));
  const toneWords: Record<string, string> = { ok: 'Running', busy: 'Starting or stopping', warn: 'Has a problem', bad: 'Stopped: needs your attention', off: 'Off' };

  function key(e: KeyboardEvent, index: number) {
    const keys: Record<string, number> = { ArrowRight: 1, ArrowDown: 1, ArrowLeft: -1, ArrowUp: -1 };
    if (!(e.key in keys)) return;
    e.preventDefault();
    const next = items[(index + keys[e.key] + items.length) % items.length];
    selectChain(next.id);
    (e.currentTarget as HTMLElement).parentElement?.querySelector<HTMLElement>(`[data-chain="${next.id}"]`)?.focus();
  }
</script>

{#if items.length > 0}
  <section class="switcher" class:collapsed aria-label={t('Modules')}>
    <div class="list" role="radiogroup" aria-label={t('Module shown')}>
      {#each items as item, i (item.id)}
        {@const current = hub.selected === item.id}
        <button
          type="button"
          role="radio"
          class="chain"
          class:current
          aria-checked={current}
          tabindex={current ? 0 : -1}
          data-chain={item.id}
          title={`${item.name} · ${t(toneWords[item.tone])}`}
          onclick={() => selectChain(item.id)}
          onkeydown={(e) => key(e, i)}
        >
          <span class="mark" aria-hidden="true" data-chain={item.id}><SafeText text={item.name.slice(0, 1).toUpperCase()} /></span>
          {#if !collapsed}<span class="name"><SafeText text={item.name} /></span>{/if}
          <span class="dot {item.tone}" aria-label={t(toneWords[item.tone])}></span>
        </button>
      {/each}
    </div>
  </section>
{/if}

<style>
  .switcher {
    margin: 0.1rem 0 0.4rem;
  }
  .list {
    display: flex;
    flex-wrap: wrap;
    gap: 0.3rem;
    padding: 0.25rem;
    border: 1px solid var(--line);
    border-radius: var(--radius-l);
    background: var(--surface-2);
  }
  .chain {
    position: relative;
    display: inline-flex;
    align-items: center;
    gap: 0.4rem;
    flex: 1 1 auto;
    min-width: 0;
    padding: 0.3rem 0.55rem 0.3rem 0.3rem;
    border: 0;
    border-radius: 999px;
    background: transparent;
    color: var(--text-muted);
    font: inherit;
    font-size: 0.86rem;
    font-weight: 600;
    text-align: left;
    cursor: pointer;
  }
  .chain:hover {
    background: var(--raised);
    color: var(--text);
  }
  .chain.current {
    background: var(--accent);
    color: var(--on-accent);
    box-shadow: 0 6px 18px -8px var(--accent);
  }
  .mark {
    display: inline-grid;
    place-items: center;
    width: 1.45rem;
    height: 1.45rem;
    flex: none;
    border-radius: 50%;
    background: var(--raised);
    color: var(--text-muted);
    font-size: 0.74rem;
    font-weight: 750;
  }
  .chain.current .mark {
    background: color-mix(in srgb, var(--on-accent) 22%, transparent);
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
    width: 0.45rem;
    height: 0.45rem;
    flex: none;
    border-radius: 50%;
    background: var(--text-faint);
  }
  .dot.ok {
    background: var(--positive);
  }
  .dot.busy {
    background: var(--warning);
  }
  .dot.warn {
    background: var(--warning);
  }
  .dot.bad {
    background: var(--negative);
  }
  .chain.current .dot.ok {
    background: color-mix(in srgb, var(--on-accent) 85%, var(--positive));
  }
  .collapsed .list {
    flex-direction: column;
    flex-wrap: nowrap;
    padding: 0.2rem;
  }
  .collapsed .chain {
    justify-content: center;
    padding: 0.3rem 0;
  }
  .collapsed .dot {
    position: absolute;
    right: 0.25rem;
    top: 0.2rem;
  }
</style>
