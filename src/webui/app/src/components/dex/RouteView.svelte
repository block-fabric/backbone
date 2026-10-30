<script lang="ts">
  // A route through a chain's pools, hop by hop: what goes in, each pool with its kind and fee tier and what comes out
  // of it, the price impact and the fees of the route. The trusted dialog says it again, as the app checked it.
  import { t } from '../../lib/i18n.svelte';
  import SafeText from '../SafeText.svelte';
  import ModuleAmount from '../ModuleAmount.svelte';
  import AssetMark from './AssetMark.svelte';
  import { feeText, type Route } from '../../lib/dex.svelte';

  let { stem, name, route, amountIn }: { stem: string; name: string; route: Route; amountIn: string } = $props();
  const curveWords: Record<string, string> = { 'constant-product': 'Constant product', stable: 'Stable', weighted: 'Weighted' };
  const impact = $derived(route.priceImpact != null ? parseFloat(route.priceImpact) : null);
</script>

<div class="route-view" data-route-hops={route.hops.length}>
  <p class="path" data-route-path>
    {#each route.labels as l, i (i)}
      {#if i > 0}<span class="arrow" aria-hidden="true">→</span>{/if}
      <span class="asset"><AssetMark {stem} chainName={name} id={route.path[i]} label={l} size={1.2} /><SafeText text={l} /></span>
    {/each}
  </p>
  <ol class="hops">
    <li class="start">
      <span class="dot" aria-hidden="true"></span>
      <span class="what">{t('You pay')}</span>
      <span class="num"><ModuleAmount text={amountIn} unit={route.labels[0]} /></span>
    </li>
    {#each route.hops as h, i (h.pool)}
      <li data-route-hop={i}>
        <span class="dot" aria-hidden="true"></span>
        <span class="what">
          <strong><SafeText text={`${route.labels[i]} / ${route.labels[i + 1]}`} /></strong>
          <span class="tags">
            <span class="badge" data-hop-curve={h.curve ?? 'constant-product'}>{t(curveWords[h.curve ?? 'constant-product'])}{#if h.curve === 'stable'} · A {h.amp}{/if}{#if h.curve === 'weighted'} · {h.weightA}/{100 - (h.weightA ?? 50)}{/if}</span>
            <span class="badge soft" data-hop-fee={h.fee ?? 3000}><SafeText text={feeText(h.fee ?? 3000)} /></span>
          </span>
          <span class="mono faint id" translate="no" title={h.pool}><SafeText text={`${h.pool.slice(0, 8)}…${h.pool.slice(-6)}`} /></span>
        </span>
        <span class="num"><ModuleAmount text={h.amountOut} unit={route.labels[i + 1]} /></span>
      </li>
    {/each}
  </ol>
  <dl class="facts">
    <dt>{t('Pools')}</dt>
    <dd>{route.hops.length}</dd>
    <dt>{t('Price impact')}</dt>
    <dd class="num" class:warn={impact !== null && impact >= 3}>{impact === null ? '–' : `${impact.toFixed(2)} %`}</dd>
    <dt>{t('Pool fees')}</dt>
    <dd class="num"><SafeText text={feeText(route.fees)} />{#if route.hops.length > 1}<span class="faint small all-hops">{t('(all hops)')}</span>{/if}</dd>
  </dl>
</div>

<style>
  .route-view {
    display: flex;
    flex-direction: column;
    gap: 0.8rem;
  }
  .path {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: 0.4rem;
    margin: 0;
    font-weight: 650;
  }
  .asset {
    display: inline-flex;
    align-items: center;
    gap: 0.35rem;
  }
  .arrow {
    color: var(--text-faint);
  }
  .hops {
    list-style: none;
    margin: 0;
    padding: 0;
    display: flex;
    flex-direction: column;
  }
  .hops li {
    position: relative;
    display: grid;
    grid-template-columns: 1rem minmax(0, 1fr) auto;
    gap: 0.6rem;
    align-items: start;
    padding: 0.45rem 0;
  }
  .hops li + li {
    border-top: 1px solid var(--line);
  }
  .dot {
    width: 0.6rem;
    height: 0.6rem;
    margin-top: 0.35rem;
    border-radius: 50%;
    background: var(--accent);
  }
  .start .dot {
    background: var(--text-faint);
  }
  .what {
    display: flex;
    flex-direction: column;
    gap: 0.2rem;
    min-width: 0;
  }
  .start .what {
    color: var(--text-muted);
  }
  .tags {
    display: flex;
    flex-wrap: wrap;
    gap: 0.3rem;
  }
  .badge.soft {
    background: var(--surface-2);
  }
  .id {
    font-size: 0.8rem;
  }
  .num {
    text-align: right;
    font-variant-numeric: tabular-nums;
  }
  .facts {
    display: grid;
    grid-template-columns: auto 1fr;
    gap: 0.3rem 1rem;
    margin: 0;
    padding-top: 0.6rem;
    border-top: 1px solid var(--line);
  }
  .facts dt {
    color: var(--text-muted);
  }
  .facts dd {
    margin: 0;
    text-align: right;
  }
  .warn {
    color: var(--warning);
  }
  .all-hops {
    margin-left: 0.3rem;
  }
  .small {
    font-size: 0.8rem;
  }
</style>
