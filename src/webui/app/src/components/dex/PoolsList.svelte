<script lang="ts">
  // The pools of the chains shown (doc/unified-app.md §22): the pair, its curve and fee tier, what it holds (about, in
  // the chain's coin), its volume since it was made, a yearly rate of its fees (an estimate, said so), and the share the
  // wallet holds. A row opens the pool's page. Amounts masked in privacy mode.
  import { t } from '../../lib/i18n.svelte';
  import Icon from '../Icon.svelte';
  import SafeText from '../SafeText.svelte';
  import ChainBadge from '../ChainBadge.svelte';
  import ModuleAmount from '../ModuleAmount.svelte';
  import AssetMark from './AssetMark.svelte';
  import { chainUnit, type ChainEntry } from '../../lib/chains.svelte';
  import { curveText, dex, feeText, indexRows, poolTab, type IndexRow } from '../../lib/dex.svelte';
  import { go } from '../../lib/nav.svelte';

  let { chains }: { chains: ChainEntry[] } = $props();

  const many = $derived(chains.length > 1);
  const rows = $derived<IndexRow[]>(chains.flatMap((c) => indexRows(c.id, dex.pools[c.id])).sort((x, y) => (y.tvl ?? -1) - (x.tvl ?? -1)));
  const nameOf = (id: string) => chains.find((c) => c.id === id)?.name ?? id;
  const shareOf = (stem: string, pool: string) => dex.positions[stem]?.find((p) => p.pool === pool)?.percent ?? null;
  const loading = $derived(chains.some((c) => !dex.pools[c.id]));
</script>

<section class="panel pools" aria-label={t('Pools')} data-pools-list>
  <header class="head">
    <h2>{t('Pools')}</h2>
    <span class="muted small">{rows.length === 1 ? t('1 pool') : t('%1 pools', String(rows.length))}</span>
    <span class="grow"></span>
    <button type="button" class="btn" onclick={() => go('trade', 'create')} data-pool-create><Icon name="plus" size={16} /> {t('Create a pool')}</button>
  </header>
  <div class="scroll">
    <table class="table">
      <thead>
        <tr>
          {#if many}<th>{t('Chain')}</th>{/if}
          <th>{t('Pair')}</th>
          <th>{t('Curve')}</th>
          <th class="num">{t('Fee')}</th>
          <th class="num" title={t('What the pool holds, about, in the chain\'s coin')}>{t('Liquidity')}</th>
          <th class="num" title={t('Traded since the pool was made, in the chain\'s coin')}>{t('Volume')}</th>
          <th class="num" title={t('A yearly rate of its fees for the providers, from its trades since it was made: an estimate')}>{t('APR, est.')}</th>
          <th class="num">{t('Your share')}</th>
        </tr>
      </thead>
      <tbody>
        {#each rows as r (r.chain + r.pool.pool)}
          {@const share = shareOf(r.chain, r.pool.pool)}
          <tr class="pick-row" tabindex="0" onclick={() => go('trade', poolTab(r.chain, r.pool.pool))} onkeydown={(e) => e.key === 'Enter' && go('trade', poolTab(r.chain, r.pool.pool))} data-pool-row={r.pool.pool}>
            {#if many}<td><ChainBadge id={r.chain} name={nameOf(r.chain)} small /></td>{/if}
            <td>
              <span class="pair">
                <span class="marks"><AssetMark stem={r.chain} chainName={nameOf(r.chain)} id={r.pool.a.id} label={r.pool.a.label} size={1.35} /><AssetMark stem={r.chain} chainName={nameOf(r.chain)} id={r.pool.b.id} label={r.pool.b.label} size={1.35} /></span>
                <strong><SafeText text={r.pair} /></strong>
                {#if r.pool.abandoned}<span class="badge warning">{t('no providers')}</span>{/if}
              </span>
            </td>
            <td><span class="badge" data-pool-curve={r.pool.curve}><SafeText text={curveText(r.pool)} /></span></td>
            <td class="num" data-pool-fee={r.pool.fee}><SafeText text={feeText(r.pool.fee)} /></td>
            <td class="num">{#if r.tvl !== null}<span class="approx">≈</span><ModuleAmount text={r.tvl.toFixed(2)} unit={chainUnit(r.chain)} />{:else}<span class="faint">–</span>{/if}</td>
            <td class="num">{#if r.volume !== null}<span class="approx">≈</span><ModuleAmount text={r.volume.toFixed(2)} unit={chainUnit(r.chain)} />{:else}<ModuleAmount text={r.pool.volumeA} unit={r.pool.a.label} />{/if}</td>
            <td class="num" data-pool-apr>{r.apr === null ? '–' : `${r.apr.toFixed(r.apr < 10 ? 2 : 1)} %`}</td>
            <td class="num">{#if share}<ModuleAmount text={share} unit="%" />{:else}<span class="faint">–</span>{/if}</td>
          </tr>
        {:else}
          <tr><td colspan={many ? 8 : 7} class="muted empty">{loading ? t('Reading the pools…') : t('No pool yet. Create the first one.')}</td></tr>
        {/each}
      </tbody>
    </table>
  </div>
  <p class="faint small foot">{t('Liquidity and the yearly rate are estimates from the pools\' prices in the chain\'s coin; a pool with no price in it shows –.')}</p>
</section>

<style>
  .pools {
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
    gap: 0.55rem;
  }
  .marks {
    display: inline-flex;
  }
  .marks :global(> :nth-child(2)) {
    margin-left: -0.2rem;
    box-shadow: 0 0 0 2px var(--surface);
    border-radius: 50%;
  }
  .approx {
    color: var(--text-faint);
    margin-right: 0.2rem;
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
