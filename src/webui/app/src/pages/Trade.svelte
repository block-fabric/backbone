<script lang="ts">
  // Trade: the DEX of every chain that has one, as one page of the app (doc/unified-app.md §22), beside each chain's
  // own Trade page (its group in the sidebar). Its tabs: Swap (one chain, picked by its tile, in step with the top bar's
  // selector), Pools (a pool's page, a new pool), Positions (the wallet's liquidity), Orders (limit orders and the book:
  // phase B, laid out) and Markets (every pair, searched and sorted). With All in the top bar, the read tabs show every
  // chain with a DEX and Swap asks which chain; a chain without a DEX selected is said, with those that have one.
  // Simple mode: Swap alone, simpler.
  //
  // Light: only what the tab shown needs, of the chains it shows, while the window is seen (lib/dex.svelte.ts poll).
  import { t } from '../lib/i18n.svelte';
  import ChainTiles from '../components/ChainTiles.svelte';
  import SafeText from '../components/SafeText.svelte';
  import NotOnThisChain from '../components/NotOnThisChain.svelte';
  import SwapPanel from '../components/dex/SwapPanel.svelte';
  import PoolsList from '../components/dex/PoolsList.svelte';
  import PoolPage from '../components/dex/PoolPage.svelte';
  import CreatePool from '../components/dex/CreatePool.svelte';
  import Positions from '../components/dex/Positions.svelte';
  import Orders from '../components/dex/Orders.svelte';
  import MarketIndex from '../components/dex/MarketIndex.svelte';
  import { ALL, chainEntries, chainsWith, chainWallet, loadCapabilities, refreshSummary, selectedChain, setSelectedChain } from '../lib/chains.svelte';
  import { loadInfo, loadPools, loadPositions, parsePoolTab, poll } from '../lib/dex.svelte';
  import { nav } from '../lib/nav.svelte';
  import { simpleMode } from '../lib/uimode.svelte';

  $effect(() => {
    void loadCapabilities();
  });

  const TABS = [
    { id: '', label: 'Swap' },
    { id: 'pools', label: 'Pools' },
    { id: 'positions', label: 'Positions' },
    { id: 'orders', label: 'Orders' },
    { id: 'markets', label: 'Markets' },
  ];
  const simple = $derived(simpleMode());
  const poolAt = $derived(parsePoolTab(nav.tab));
  const view = $derived(simple ? '' : poolAt ? 'pool' : nav.tab === 'create' ? 'create' : TABS.some((x) => x.id === nav.tab) ? nav.tab : '');
  const tabShown = $derived(view === 'pool' || view === 'create' ? 'pools' : view);

  const entries = $derived(chainEntries());
  const dexChains = $derived(chainsWith('dex', entries));
  const dexIds = $derived(dexChains.map((e) => e.id));
  const sel = $derived(selectedChain(entries));
  const selEntry = $derived(sel === ALL ? null : (entries.find((e) => e.id === sel) ?? null));
  /** The chain of a pool's page (its link names it), else the one selected when it has a DEX. */
  const one = $derived(poolAt ? (dexChains.find((e) => e.id === poolAt.stem) ?? null) : (dexChains.find((e) => e.id === sel) ?? null));
  /** The chains a read tab shows: the one selected, or with All every chain with a DEX that runs. */
  const shown = $derived(one ? [one] : sel === ALL ? dexChains.filter((e) => e.running) : []);
  const offTopic = $derived(!!selEntry && !one && !poolAt);

  // What the tab shown reads, of the chains it shows, at its own pace.
  $effect(() => {
    for (const e of shown) if (e.running) void loadInfo(e.id);
  });
  $effect(() => {
    const ids = shown.filter((e) => e.running).map((e) => e.id);
    if (!ids.length || view === 'pool' || view === 'orders') return;
    if (view === '' || view === 'create') {
      // Swap and a new pool: one chain's pools (the assets they trade) and its wallet's balances.
      if (!one) return;
      return poll(() => {
        void loadPools(one.id, 15_000);
        void refreshSummary(one.id, 8000);
      }, 20_000);
    }
    return poll(() => {
      for (const id of ids) {
        void loadPools(id, 15_000);
        if (view === 'positions' || view === 'pools') void loadPositions(id, chainWallet(id));
      }
    }, view === 'positions' ? 30_000 : 20_000);
  });
</script>

<div class="page trade" data-trade-view={view} data-trade-chain={one?.id ?? ''}>
  <header class="page-head">
    <h1>{simple ? t('Swap') : t('Trade')}</h1>
    {#if !simple}
      <div class="tabs" role="tablist" aria-label={t('Trade')}>
        {#each TABS as tb (tb.id)}
          <button type="button" role="tab" aria-selected={tabShown === tb.id} onclick={() => (nav.tab = tb.id)} data-trade-tab={tb.id || 'swap'}>{t(tb.label)}</button>
        {/each}
      </div>
    {/if}
  </header>

  {#if dexChains.length === 0}
    <section class="panel note"><p class="muted">{t('No chain with a DEX is turned on. Turn on Layers or BitAssets in Settings → Sidechains.')}</p></section>
  {:else if offTopic}
    <NotOnThisChain what="DEX" have={dexChains} none="No chain with a DEX is turned on." />
  {:else}
    {#if view !== 'pool'}
      <ChainTiles value={one?.id ?? null} label={view === '' ? 'Trade on' : 'Chain'} kind="trade" only={dexIds} />
    {/if}

    {#if view === ''}
      {#if one}
        {#key one.id}<SwapPanel stem={one.id} name={one.name} {simple} />{/key}
      {/if}
    {:else if view === 'pool' && poolAt}
      {#if one}
        {#key poolAt.pool}<PoolPage stem={one.id} name={one.name} poolId={poolAt.pool} />{/key}
      {:else}
        <section class="panel note"><p class="muted">{t('That chain is not turned on.')}</p></section>
      {/if}
    {:else if view === 'create'}
      {#if one}
        {#key one.id}<CreatePool stem={one.id} name={one.name} />{/key}
      {:else}
        <section class="panel note" data-trade-pick><p class="muted">{t('Pick the chain of the new pool above.')}</p></section>
      {/if}
    {:else if view === 'pools'}
      <PoolsList chains={shown} />
    {:else if view === 'positions'}
      <Positions chains={shown} />
    {:else if view === 'orders'}
      <Orders name={one?.name ?? t('each chain')} />
    {:else if view === 'markets'}
      <MarketIndex chains={shown} />
    {/if}
    {#if sel === ALL && view !== '' && view !== 'pool' && view !== 'create' && dexChains.length > 1}
      <p class="faint small">{t('Every chain with a DEX is shown: pick one above, or at the top, to see only its own.')} <button type="button" class="btn ghost small" onclick={() => setSelectedChain(dexChains[0].id)}><SafeText text={dexChains[0].name} /></button></p>
    {/if}
  {/if}
</div>

<style>
  .trade {
    display: flex;
    flex-direction: column;
    gap: 1.1rem;
  }
  .page-head {
    display: flex;
    align-items: center;
    gap: 1.1rem;
    flex-wrap: wrap;
  }
  .page-head .tabs {
    margin: 0;
  }
  .note {
    padding: 1rem 1.3rem;
  }
  .note p {
    margin: 0;
  }
  .small {
    font-size: 0.85rem;
  }
  p {
    margin: 0;
  }
</style>
