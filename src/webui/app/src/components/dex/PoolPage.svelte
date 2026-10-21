<script lang="ts">
  // One pool (doc/unified-app.md §22): its pair, curve and fee tier; its price over time (its time-weighted averages,
  // where the chain keeps them); what it holds; its trades; the wallet's share of it; liquidity in and out. Read while
  // shown: the pool every 20 s, the wallet's share every 30 s, the chart every minute.
  import { t } from '../../lib/i18n.svelte';
  import Icon from '../Icon.svelte';
  import SafeText from '../SafeText.svelte';
  import ChainBadge from '../ChainBadge.svelte';
  import ModuleAmount from '../ModuleAmount.svelte';
  import CopyButton from '../CopyButton.svelte';
  import AssetMark from './AssetMark.svelte';
  import PriceChart from './PriceChart.svelte';
  import LiquidityForms from './LiquidityForms.svelte';
  import { chainUnit, chainWallet, refreshSummary } from '../../lib/chains.svelte';
  import { aprEstimate, coinPrices, curveText, dex, feeText, loadChart, loadInfo, loadPools, loadPositions, oriented, pairText, poll, poolValue, trimNumber } from '../../lib/dex.svelte';
  import { go } from '../../lib/nav.svelte';

  let { stem, name, poolId }: { stem: string; name: string; poolId: string } = $props();

  // The pool, and the wallet's balances (what may go in).
  $effect(() =>
    poll(() => {
      void loadPools(stem);
      void refreshSummary(stem, 8000);
    }, 20_000),
  );
  $effect(() => poll(() => void loadPositions(stem, chainWallet(stem)), 30_000));
  $effect(() => poll(() => void loadChart(stem, poolId), 60_000));
  $effect(() => void loadInfo(stem));
  $effect(() => void refreshSummary(stem, 1000));

  const list = $derived(dex.pools[stem]);
  const raw = $derived(list?.pools.find((p) => p.pool === poolId) ?? null);
  const pool = $derived(raw ? oriented(raw) : null);
  const position = $derived(dex.positions[stem]?.find((p) => p.pool === poolId) ?? null);
  const chart = $derived(dex.charts[`${stem}:${poolId}`]);
  const prices = $derived(coinPrices(list?.pools ?? []));
  const tvl = $derived(pool ? poolValue(pool, prices) : null);
  const apr = $derived(pool && list ? aprEstimate(pool, prices, list.height, parseFloat(list.blockSeconds)) : null);
  let inverse = $state(false);
  let liqTab = $state<'add' | 'remove'>('add');

  // The chart's prices are of the pool's first asset (as the chain keeps it) in the other: turned to what is shown.
  const points = $derived.by(() => {
    if (!chart?.points?.length || !pool) return [];
    const flip = (!!chart.labelA && chart.labelA !== pool.a.label) !== inverse;
    return chart.points.map((p) => ({ t: p.t, price: flip ? 1 / parseFloat(p.price) : parseFloat(p.price) })).filter((p) => Number.isFinite(p.price) && p.price > 0);
  });
  const base = $derived(pool ? (inverse ? pool.b.label : pool.a.label) : '');
  const quoteUnit = $derived(pool ? (inverse ? pool.a.label : pool.b.label) : '');
  const spot = $derived(pool && parseFloat(pool.reserveA) > 0 ? (inverse ? parseFloat(pool.reserveA) / parseFloat(pool.reserveB) : parseFloat(pool.reserveB) / parseFloat(pool.reserveA)) : null);
</script>

<div class="pool-page" data-pool-page={poolId}>
  <header class="head">
    <button type="button" class="btn ghost small" onclick={() => go('trade', 'pools')} data-pool-back><Icon name="chevron-left" size={16} /> {t('Pools')}</button>
    {#if pool}
      <span class="marks"><AssetMark {stem} chainName={name} id={pool.a.id} label={pool.a.label} size={1.8} /><AssetMark {stem} chainName={name} id={pool.b.id} label={pool.b.label} size={1.8} /></span>
      <h2><SafeText text={pairText(pool.a, pool.b)} /></h2>
      <span class="badge" data-pool-curve={pool.curve}><SafeText text={curveText(pool)} /></span>
      <span class="badge soft" data-pool-fee={pool.fee}><SafeText text={feeText(pool.fee)} /></span>
      {#if pool.abandoned}<span class="badge warning">{t('no providers')}</span>{/if}
      <span class="grow"></span>
      <span class="on muted small">{t('on')} <ChainBadge id={stem} {name} small /></span>
    {/if}
  </header>

  {#if !list}
    <p class="muted">{t('Reading the pool…')}</p>
  {:else if !pool}
    <section class="panel note"><p>{t('This pool is not among the pools of %1 now.', name)}</p></section>
  {:else}
    <div class="grid">
      <div class="main">
        <section class="panel price" aria-label={t('Price')}>
          <div class="price-head">
            <div>
              <span class="caps faint">{t('Price')}</span>
              <p class="big num" data-pool-price>1 <SafeText text={base} /> = <SafeText text={spot === null ? '–' : trimNumber(spot)} /> <SafeText text={quoteUnit} /></p>
            </div>
            <button type="button" class="btn ghost small" onclick={() => (inverse = !inverse)} title={t('Show the price the other way round')} data-pool-invert><Icon name="swap" size={15} /> <SafeText text={`${quoteUnit} / ${base}`} /></button>
          </div>
          {#if chart?.reason === 'none'}
            <p class="muted empty">{t('%1 keeps no price history of its pools.', name)}</p>
          {:else}
            <PriceChart {points} unit={quoteUnit} {base} />
          {/if}
        </section>
        <LiquidityForms {stem} {name} {pool} {position} bind:tab={liqTab} />
      </div>

      <div class="side">
        <section class="panel facts-panel" aria-label={t('The pool')}>
          <h3 class="caps faint">{t('The pool')}</h3>
          <dl class="facts">
            <dt>{t('Holds')}</dt>
            <dd class="num"><ModuleAmount text={pool.reserveA} unit={pool.a.label} /></dd>
            <dt></dt>
            <dd class="num"><ModuleAmount text={pool.reserveB} unit={pool.b.label} /></dd>
            <dt>{t('Liquidity')}</dt>
            <dd class="num">{#if tvl !== null}≈ <ModuleAmount text={tvl.toFixed(2)} unit={chainUnit(stem)} />{:else}–{/if}</dd>
            <dt>{t('Traded since made')}</dt>
            <dd class="num"><ModuleAmount text={pool.volumeA} unit={pool.a.label} /></dd>
            <dt></dt>
            <dd class="num"><ModuleAmount text={pool.volumeB} unit={pool.b.label} /></dd>
            <dt>{t('Trades')}</dt>
            <dd class="num">{pool.swaps.toLocaleString()}</dd>
            <dt>{t('Fee to providers')}</dt>
            <dd class="num"><SafeText text={feeText(pool.fee)} /></dd>
            <dt title={t('A yearly rate of its fees for the providers, from its trades since it was made: an estimate')}>{t('APR, est.')}</dt>
            <dd class="num" data-pool-apr>{apr === null ? '–' : `${apr.toFixed(2)} %`}</dd>
            <dt>{t('Made in block')}</dt>
            <dd class="num">{pool.created.toLocaleString()}</dd>
          </dl>
          <p class="id"><span class="mono faint" translate="no" title={pool.pool}><SafeText text={`${pool.pool.slice(0, 12)}…${pool.pool.slice(-8)}`} /></span><CopyButton text={pool.pool} label={t('Copy the pool\'s id')} /></p>
        </section>

        <section class="panel facts-panel" aria-label={t('Your share')} data-pool-share>
          <h3 class="caps faint">{t('Your share')}</h3>
          {#if position}
            <dl class="facts">
              <dt>{t('Of the pool')}</dt>
              <dd class="num"><ModuleAmount text={position.percent} unit="%" /></dd>
              <dt>{t('Worth')}</dt>
              <dd class="num"><ModuleAmount text={position.valueA} unit={position.a.label} /></dd>
              <dt></dt>
              <dd class="num"><ModuleAmount text={position.valueB} unit={position.b.label} /></dd>
              <dt>{t('Shares')}</dt>
              <dd class="num"><ModuleAmount text={position.shares} /></dd>
            </dl>
            <p class="faint small">{t('Its fees are in what the shares are worth: nothing to collect.')}</p>
          {:else}
            <p class="muted">{t('You provide no liquidity to this pool.')}</p>
          {/if}
        </section>
      </div>
    </div>
  {/if}
</div>

<style>
  .pool-page {
    display: flex;
    flex-direction: column;
    gap: 1.1rem;
  }
  .head {
    display: flex;
    align-items: center;
    gap: 0.7rem;
    flex-wrap: wrap;
  }
  .head h2 {
    margin: 0;
    font-size: 1.35rem;
  }
  .marks {
    display: inline-flex;
  }
  .marks :global(> :nth-child(2)) {
    margin-left: -0.25rem;
    box-shadow: 0 0 0 2px var(--bg, var(--surface));
    border-radius: 50%;
  }
  .grow {
    flex: 1;
  }
  .on {
    display: inline-flex;
    align-items: center;
    gap: 0.35rem;
  }
  .badge.soft {
    background: var(--surface-2);
  }
  .grid {
    display: grid;
    grid-template-columns: minmax(0, 1fr) minmax(16rem, 22rem);
    gap: 1.1rem;
    align-items: start;
  }
  @media (max-width: 62rem) {
    .grid {
      grid-template-columns: minmax(0, 1fr);
    }
  }
  .main,
  .side {
    display: flex;
    flex-direction: column;
    gap: 1.1rem;
    min-width: 0;
  }
  .price {
    padding: 1.1rem 1.3rem 1rem;
    display: flex;
    flex-direction: column;
    gap: 0.8rem;
  }
  .price-head {
    display: flex;
    justify-content: space-between;
    align-items: flex-start;
    gap: 1rem;
    flex-wrap: wrap;
  }
  .big {
    margin: 0.2rem 0 0;
    font-size: 1.35rem;
    font-weight: 650;
  }
  .facts-panel {
    padding: 1rem 1.3rem;
    display: flex;
    flex-direction: column;
    gap: 0.6rem;
  }
  h3 {
    margin: 0;
    font-size: 0.8rem;
  }
  .facts {
    display: grid;
    grid-template-columns: auto 1fr;
    gap: 0.35rem 1rem;
    margin: 0;
  }
  .facts dt {
    color: var(--text-muted);
  }
  .facts dd {
    margin: 0;
    text-align: right;
    font-variant-numeric: tabular-nums;
  }
  .id {
    display: flex;
    align-items: center;
    gap: 0.4rem;
    margin: 0;
    font-size: 0.82rem;
  }
  .note {
    padding: 1rem 1.3rem;
  }
  .empty {
    padding: 2rem 0;
    text-align: center;
    margin: 0;
  }
  .small {
    font-size: 0.82rem;
  }
  p {
    margin: 0;
  }
</style>
