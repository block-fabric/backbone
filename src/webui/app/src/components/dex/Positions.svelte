<script lang="ts">
  // The wallet's liquidity on every chain shown (doc/unified-app.md §22): each pool it provides to, its share, what the
  // shares are worth, and where its fees are. Constant-product shares are always in range and their fees are in what
  // they are worth; concentrated positions (a range, fees to collect) come with the chains' pool kinds: the columns are
  // there for them. Amounts masked in privacy mode.
  import { t } from '../../lib/i18n.svelte';
  import SafeText from '../SafeText.svelte';
  import ChainBadge from '../ChainBadge.svelte';
  import ModuleAmount from '../ModuleAmount.svelte';
  import AssetMark from './AssetMark.svelte';
  import type { ChainEntry } from '../../lib/chains.svelte';
  import { curveText, dex, feeText, pairText, poolTab } from '../../lib/dex.svelte';
  import { go } from '../../lib/nav.svelte';

  let { chains }: { chains: ChainEntry[] } = $props();
  const many = $derived(chains.length > 1);
  const rows = $derived(chains.flatMap((c) => (dex.positions[c.id] ?? []).map((p) => ({ chain: c, p, pool: dex.pools[c.id]?.pools.find((x) => x.pool === p.pool) ?? null }))));
  const loading = $derived(chains.some((c) => !dex.positions[c.id]));
</script>

<section class="panel positions" aria-label={t('Your liquidity')} data-positions>
  <header class="head">
    <h2>{t('Your liquidity')}</h2>
    <span class="muted small">{rows.length === 1 ? t('1 position') : t('%1 positions', String(rows.length))}</span>
  </header>
  <div class="scroll">
    <table class="table">
      <thead>
        <tr>
          {#if many}<th>{t('Chain')}</th>{/if}
          <th>{t('Pool')}</th>
          <th>{t('Range')}</th>
          <th class="num">{t('Your share')}</th>
          <th class="num">{t('Worth')}</th>
          <th>{t('Fees earned')}</th>
          <th></th>
        </tr>
      </thead>
      <tbody>
        {#each rows as r (r.chain.id + r.p.pool)}
          <tr data-position={r.p.pool}>
            {#if many}<td><ChainBadge id={r.chain.id} name={r.chain.name} small /></td>{/if}
            <td>
              <span class="pair">
                <span class="marks"><AssetMark stem={r.chain.id} chainName={r.chain.name} id={r.p.a.id} label={r.p.a.label} size={1.3} /><AssetMark stem={r.chain.id} chainName={r.chain.name} id={r.p.b.id} label={r.p.b.label} size={1.3} /></span>
                <span class="col"><strong><SafeText text={pairText(r.p.a, r.p.b)} /></strong>{#if r.pool}<span class="faint small"><SafeText text={curveText(r.pool)} /><span class="dot-sep">{' · '}</span><SafeText text={feeText(r.pool.fee)} /></span>{/if}</span>
              </span>
            </td>
            <td><span class="badge positive" data-position-range="full">{t('Full range')}</span></td>
            <td class="num"><ModuleAmount text={r.p.percent} unit="%" /></td>
            <td class="num"><span class="col right"><ModuleAmount text={r.p.valueA} unit={r.p.a.label} /><ModuleAmount text={r.p.valueB} unit={r.p.b.label} /></span></td>
            <td><span class="muted small">{t('In what the shares are worth')}</span></td>
            <td class="acts">
              <button type="button" class="btn small" onclick={() => go('trade', poolTab(r.chain.id, r.p.pool))} data-position-open={r.p.pool}>{t('Manage')}</button>
              <button type="button" class="btn ghost small" disabled title={t('For concentrated positions: their fees are collected apart')}>{t('Collect')}</button>
            </td>
          </tr>
        {:else}
          <tr><td colspan={many ? 7 : 6} class="muted empty">{loading ? t('Reading your liquidity…') : t('You provide no liquidity yet. Pick a pool to add some.')}</td></tr>
        {/each}
      </tbody>
    </table>
  </div>
  <p class="faint small foot">{t('A share of a constant-product pool is always in range and earns its fees inside the pool. Concentrated positions (a price range, in or out of it, fees to collect) come with the pool kinds of the chains.')}</p>
</section>

<style>
  .positions {
    padding: 1.1rem 1.3rem 1rem;
    display: flex;
    flex-direction: column;
    gap: 0.8rem;
  }
  .head {
    display: flex;
    align-items: baseline;
    gap: 0.8rem;
  }
  h2 {
    margin: 0;
    font-size: 1.2rem;
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
    vertical-align: middle;
  }
  .table th {
    color: var(--text-faint);
    font-size: 0.75rem;
    text-transform: uppercase;
    font-weight: 650;
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
  .col {
    display: inline-flex;
    flex-direction: column;
    gap: 0.1rem;
  }
  .col.right {
    align-items: flex-end;
  }
  .acts {
    text-align: right !important;
  }
  .acts .btn + .btn {
    margin-left: 0.3rem;
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
