<script lang="ts">
  // The asset of one side of a trade (or of a pool): a button with its mark and ticker, opening a list of what the
  // chain's pools trade (the wallet's balance of each, masked in privacy mode), searchable by ticker or hash. The
  // chain's coin first. Keys: arrows, Enter, Escape; a click outside closes it.
  import { t } from '../../lib/i18n.svelte';
  import Icon from '../Icon.svelte';
  import SafeText from '../SafeText.svelte';
  import ModuleAmount from '../ModuleAmount.svelte';
  import AssetMark from './AssetMark.svelte';
  import type { DexAsset } from '../../lib/dex.svelte';

  let {
    stem,
    chainName,
    assets,
    value,
    onpick,
    label,
    balances = {},
    exclude = '',
    side,
  }: { stem: string; chainName: string; assets: DexAsset[]; value: string; onpick: (id: string) => void; label: string; balances?: Record<string, string>; exclude?: string; side: string } = $props();

  let open = $state(false);
  let q = $state('');
  let at = $state(0);
  let root: HTMLElement | undefined = $state();
  let search: HTMLInputElement | undefined = $state();

  const chosen = $derived(assets.find((a) => a.id === value) ?? null);
  const shown = $derived.by(() => {
    const w = q.trim().toLowerCase();
    return assets.filter((a) => a.id !== exclude && (!w || a.label.toLowerCase().includes(w) || a.id.startsWith(w.replace(/^0x/, ''))));
  });

  function toggle() {
    open = !open;
    q = '';
    at = Math.max(0, shown.findIndex((a) => a.id === value));
    if (open) queueMicrotask(() => search?.focus());
  }
  function pick(id: string) {
    onpick(id);
    open = false;
  }
  function keys(e: KeyboardEvent) {
    if (e.key === 'Escape') {
      open = false;
      e.stopPropagation();
    } else if (e.key === 'ArrowDown') {
      at = Math.min(shown.length - 1, at + 1);
      e.preventDefault();
    } else if (e.key === 'ArrowUp') {
      at = Math.max(0, at - 1);
      e.preventDefault();
    } else if (e.key === 'Enter' && shown[at]) {
      pick(shown[at].id);
      e.preventDefault();
    }
  }
  function outside(e: PointerEvent) {
    if (open && root && !root.contains(e.target as Node)) open = false;
  }
</script>

<svelte:window onpointerdown={outside} />

<div class="picker" bind:this={root} data-asset-picker={side}>
  <button type="button" class="pick" aria-haspopup="listbox" aria-expanded={open} aria-label={label} onclick={toggle} data-asset-picked={value}>
    {#if chosen}
      <AssetMark {stem} {chainName} id={chosen.id} label={chosen.label} size={1.35} />
      <span class="ticker"><SafeText text={chosen.label} /></span>
    {:else}
      <span class="ticker muted">{t('Choose')}</span>
    {/if}
    <Icon name="chevron" size={15} />
  </button>
  {#if open}
    <div class="menu" role="presentation">
      {#if assets.length > 6}
        <input bind:this={search} class="search" type="search" placeholder={t('Ticker or hash')} bind:value={q} onkeydown={keys} aria-label={t('Search the assets')} />
      {:else}
        <input bind:this={search} class="sr-only" type="text" onkeydown={keys} aria-label={label} />
      {/if}
      <ul role="listbox" aria-label={label}>
        {#each shown as a, i (a.id)}
          <li role="option" aria-selected={a.id === value} class:at={i === at}>
            <button type="button" onclick={() => pick(a.id)} onpointerenter={() => (at = i)} data-asset-option={a.id}>
              <AssetMark {stem} {chainName} id={a.id} label={a.label} size={1.3} />
              <span class="ticker"><SafeText text={a.label} /></span>
              <span class="bal">{#if balances[a.id] !== undefined}<ModuleAmount text={balances[a.id]} />{/if}</span>
            </button>
          </li>
        {:else}
          <li class="none muted">{t('No asset of that name in the pools')}</li>
        {/each}
      </ul>
    </div>
  {/if}
</div>

<style>
  .picker {
    position: relative;
    flex: none;
  }
  .pick {
    display: inline-flex;
    align-items: center;
    gap: 0.45rem;
    min-width: 8.5rem;
    padding: 0.4rem 0.6rem 0.4rem 0.45rem;
    border: 1px solid var(--line);
    border-radius: 999px;
    background: var(--surface-2);
    color: var(--text);
    font: inherit;
    font-weight: 650;
    cursor: pointer;
  }
  .pick:hover {
    border-color: var(--line-strong);
  }
  .ticker {
    flex: 1;
    text-align: left;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
    max-width: 9rem;
  }
  .menu {
    position: absolute;
    z-index: 30;
    top: calc(100% + 0.3rem);
    right: 0;
    width: 17rem;
    max-height: 20rem;
    display: flex;
    flex-direction: column;
    gap: 0.3rem;
    padding: 0.4rem;
    border: 1px solid var(--line-strong, var(--line));
    border-radius: var(--radius-l);
    background: var(--surface);
    box-shadow: var(--shadow-l, 0 8px 24px rgb(0 0 0 / 0.25));
  }
  .search {
    width: 100%;
  }
  ul {
    list-style: none;
    margin: 0;
    padding: 0;
    overflow: auto;
  }
  li button {
    display: flex;
    align-items: center;
    gap: 0.55rem;
    width: 100%;
    padding: 0.45rem 0.5rem;
    border: 0;
    border-radius: var(--radius-m);
    background: none;
    color: var(--text);
    font: inherit;
    cursor: pointer;
  }
  li.at button,
  li button:hover {
    background: var(--raised);
  }
  li[aria-selected='true'] .ticker {
    color: var(--accent-strong);
  }
  .bal {
    color: var(--text-muted);
    font-size: 0.85rem;
  }
  .none {
    padding: 0.5rem;
  }
</style>
