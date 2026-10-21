<script lang="ts">
  // Every pair of the chains shown (doc/unified-app.md §22): its price, its 24-hour change and volume (the chains' market
  // data, phase C: a dash until a chain gives them), what it holds, what it traded since it was made. A search by ticker
  // or hash, a sort by any column. A row opens the pool's page.
  import { t } from '../../lib/i18n.svelte';
  import Icon from '../Icon.svelte';
  import SafeText from '../SafeText.svelte';
  import ChainBadge from '../ChainBadge.svelte';
  import ModuleAmount from '../ModuleAmount.svelte';
  import AssetMark from './AssetMark.svelte';
  import { chainUnit, type ChainEntry } from '../../lib/chains.svelte';
  import { dex, feeText, indexRows, poolTab, searchRows, sortRows, trimNumber, type SortKey } from '../../lib/dex.svelte';
  import { go } from '../../lib/nav.svelte';

  let { chains }: { chains: ChainEntry[] } = $props();
  let q = $state('');
  let sort = $state<SortKey>('liquidity');
  let desc = $state(true);

  const many = $derived(chains.length > 1);
  const all = $derived(chains.flatMap((c) => indexRows(c.id, dex.pools[c.id])));
  const rows = $derived(sortRows(searchRows(all, q), sort, desc));
  const nameOf = (id: string) => chains.find((c) => c.id === id)?.name ?? id;
  const stats = $derived(chains.some((c) => dex.info[c.id]?.features.stats));

  function by(k: SortKey) {
    if (sort === k) desc = !desc;
    else {
      sort = k;
      desc = k !== 'pair';
    }
  }
  const COLS: { k: SortKey; label: string; num: boolean }[] = [
    { k: 'pair', label: 'Pair', num: false },
    { k: 'price', label: 'Price', num: true },
  ];
</script>

<section class="panel index" aria-label={t('Markets')} data-market-index>
  <header class="head">
    <h2>{t('Markets')}</h2>
    <span class="muted small">{rows.length === 1 ? t('1 pair') : t('%1 pairs', String(rows.length))}</span>
    <span class="grow"></span>
    <label class="search"><Icon name="search" size={15} /><input type="search" placeholder={t('Ticker or hash')} bind:value={q} aria-label={t('Search the pairs')} data-market-search /></label>
  </header>
  <div class="scroll">
    <table class="table">
      <thead>
        <tr>
          {#if many}<th>{t('Chain')}</th>{/if}
          {#each COLS as c (c.k)}
            <th class:num={c.num} aria-sort={sort === c.k ? (desc ? 'descending' : 'ascending') : undefined}><button type="button" class="sort" onclick={() => by(c.k)} data-market-sort={c.k}>{t(c.label)}{#if sort === c.k}<Icon name={desc ? 'down' : 'up'} size={12} />{/if}</button></th>
          {/each}
          <th class="num" title={t('Comes with the chains\' market data')}>{t('24 h')}</th>
          <th class="num" title={t('Comes with the chains\' market data')}>{t('Volume 24 h')}</th>
          <th class="num" aria-sort={sort === 'liquidity' ? (desc ? 'descending' : 'ascending') : undefined}><button type="button" class="sort" onclick={() => by('liquidity')} data-market-sort="liquidity">{t('Liquidity')}{#if sort === 'liquidity'}<Icon name={desc ? 'down' : 'up'} size={12} />{/if}</button></th>
          <th class="num" aria-sort={sort === 'swaps' ? (desc ? 'descending' : 'ascending') : undefined}><button type="button" class="sort" onclick={() => by('swaps')} data-market-sort="swaps">{t('Trades')}{#if sort === 'swaps'}<Icon name={desc ? 'down' : 'up'} size={12} />{/if}</button></th>
          <th class="num" aria-sort={sort === 'fee' ? (desc ? 'descending' : 'ascending') : undefined}><button type="button" class="sort" onclick={() => by('fee')} data-market-sort="fee">{t('Fee')}{#if sort === 'fee'}<Icon name={desc ? 'down' : 'up'} size={12} />{/if}</button></th>
        </tr>
      </thead>
      <tbody>
        {#each rows as r (r.chain + r.pool.pool)}
          <tr class="pick-row" tabindex="0" onclick={() => go('trade', poolTab(r.chain, r.pool.pool))} onkeydown={(e) => e.key === 'Enter' && go('trade', poolTab(r.chain, r.pool.pool))} data-market-row={r.pool.pool}>
            {#if many}<td><ChainBadge id={r.chain} name={nameOf(r.chain)} small /></td>{/if}
            <td><span class="pair"><AssetMark stem={r.chain} chainName={nameOf(r.chain)} id={r.pool.a.id} label={r.pool.a.label} size={1.3} /><strong><SafeText text={r.pool.a.label} /></strong><span class="faint">/ <SafeText text={r.pool.b.label} /></span></span></td>
            <td class="num" data-market-price><SafeText text={r.pool.price ? trimNumber(parseFloat(r.pool.price)) : '–'} /> <span class="faint small"><SafeText text={r.pool.b.label} /></span></td>
            <td class="num faint">–</td>
            <td class="num faint">–</td>
            <td class="num">{#if r.tvl !== null}≈ <ModuleAmount text={r.tvl.toFixed(2)} unit={chainUnit(r.chain)} />{:else}<span class="faint">–</span>{/if}</td>
            <td class="num">{r.pool.swaps.toLocaleString()}</td>
            <td class="num"><SafeText text={feeText(r.pool.fee)} /></td>
          </tr>
        {:else}
          <tr><td colspan={many ? 8 : 7} class="muted empty">{q ? t('No pair matches.') : t('No pair yet.')}</td></tr>
        {/each}
      </tbody>
    </table>
  </div>
  {#if !stats}<p class="faint small foot">{t('The 24-hour change and volume come with the chains\' market data. Prices are the pools\' now; liquidity is an estimate in each chain\'s coin.')}</p>{/if}
</section>

<style>
  .index {
    padding: 1.1rem 1.3rem 1rem;
    display: flex;
    flex-direction: column;
    gap: 0.8rem;
  }
  .head {
    display: flex;
    align-items: center;
    gap: 0.8rem;
    flex-wrap: wrap;
  }
  h2 {
    margin: 0;
    font-size: 1.2rem;
  }
  .grow {
    flex: 1;
  }
  .search {
    display: inline-flex;
    align-items: center;
    gap: 0.4rem;
    padding: 0 0.6rem;
    border: 1px solid var(--line);
    border-radius: 999px;
    background: var(--surface-2);
    color: var(--text-muted);
  }
  .search input {
    border: 0;
    background: none;
    padding: 0.35rem 0;
    width: 13rem;
    outline: none;
    color: var(--text);
  }
  .scroll {
    overflow-x: auto;
  }
  .table {
    width: 100%;
    border-collapse: collapse;
  }
  .table th,
  .table td {
    padding: 0.55rem 0.6rem;
    border-bottom: 1px solid var(--line);
    text-align: left;
    white-space: nowrap;
  }
  .table th {
    color: var(--text-faint);
    font-size: 0.75rem;
    text-transform: uppercase;
    font-weight: 650;
  }
  .sort {
    display: inline-flex;
    align-items: center;
    gap: 0.2rem;
    border: 0;
    background: none;
    color: inherit;
    font: inherit;
    text-transform: inherit;
    cursor: pointer;
    padding: 0;
  }
  .pick-row {
    cursor: pointer;
  }
  .pick-row:hover,
  .pick-row:focus-visible {
    background: var(--raised);
    outline: none;
  }
  .num {
    text-align: right !important;
    font-variant-numeric: tabular-nums;
  }
  .pair {
    display: inline-flex;
    align-items: center;
    gap: 0.45rem;
  }
  .empty {
    padding: 1.4rem 0.6rem;
    text-align: center !important;
  }
  .small {
    font-size: 0.82rem;
  }
  .foot {
    margin: 0;
  }
</style>
