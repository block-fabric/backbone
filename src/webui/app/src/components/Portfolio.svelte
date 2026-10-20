<script lang="ts">
  // The combined portfolio of Home (doc/unified-app.md §14): what the wallet holds on the app's own chain and on each
  // module turned on, side by side, with the assets a module says its wallet holds. A row opens that module's Home.
  // The figures of a module are its pages' (its summary) or its node's (the overview of the app), never added up with
  // the app's: each chain's coins are its own.
  import { t } from '../lib/i18n.svelte';
  import Amount from './Amount.svelte';
  import Icon from './Icon.svelte';
  import SafeText from './SafeText.svelte';
  import ModuleAmount from './ModuleAmount.svelte';
  import { go } from '../lib/nav.svelte';
  import { wallets } from '../lib/state.svelte';
  import { hub, sidechainHost } from '../lib/sidechains.svelte';
  import { modules, openModule, portfolioRows, productName, productUnit } from '../lib/modules.svelte';
  import { describeState } from '../lib/unified';

  const on = $derived((hub.list?.chains ?? []).filter((c) => c.enabled));
  // A module not shown lately: how its node is doing, asked once here.
  $effect(() => {
    for (const c of on) {
      if (modules.overview[c.stem] || c.state !== 'running') continue;
      const stem = c.stem;
      sidechainHost.overview(stem).then(
        (o) => (modules.overview[stem] = o),
        () => undefined,
      );
    }
  });
  const rows = $derived(portfolioRows(on));
  const own = $derived(wallets.current !== null ? wallets.balances[wallets.current] : undefined);
</script>

{#if on.length > 0}
  <section class="panel portfolio" aria-labelledby="portfolio-title" data-portfolio>
    <header>
      <span class="icon"><Icon name="modules" size={17} /></span>
      <h2 id="portfolio-title">{t('Portfolio')}</h2>
      <span class="muted small">{t('Every chain this app runs, its coins its own')}</span>
    </header>
    <table class="table">
      <thead>
        <tr><th>{t('Chain')}</th><th>{t('Wallet')}</th><th class="num">{t('Available')}</th><th class="num">{t('Pending')}</th><th>{t('Assets')}</th></tr>
      </thead>
      <tbody>
        <tr data-portfolio-row="chains">
          <td><button type="button" class="link" onclick={() => go('overview')}><span class="mark">{productName().slice(0, 1)}</span> <SafeText text={productName()} /></button></td>
          <td class="muted"><SafeText text={wallets.current || t('default wallet')} /></td>
          <td class="num"><Amount sats={own?.trusted} /></td>
          <td class="num"><Amount sats={own ? own.untrustedPending + own.immature : undefined} /></td>
          <td class="muted">—</td>
        </tr>
        {#each rows as r (r.stem)}
          {@const st = describeState(r.entry)}
          <tr data-portfolio-row={r.stem}>
            <td><button type="button" class="link" onclick={() => openModule(r.stem, 'home')}><span class="mark">{r.name.slice(0, 1)}</span> <SafeText text={r.name} /></button> {#if st.tone !== 'ok'}<span class="badge small-badge">{t(st.key, ...st.args)}</span>{/if}</td>
            <td class="muted"><SafeText text={r.wallet || '—'} /></td>
            <td class="num" data-field="trusted"><ModuleAmount text={r.trusted} unit={r.unit} /></td>
            <td class="num"><ModuleAmount text={r.pending} /></td>
            <td>
              {#if r.assets.length}
                <span class="assets">{#each r.assets.slice(0, 4) as a, i (i)}<span class="asset"><ModuleAmount text={a.amount} /> <SafeText text={a.name} /></span>{/each}{#if r.assets.length > 4}<span class="muted">+{r.assets.length - 4}</span>{/if}</span>
              {:else}<span class="muted">—</span>{/if}
            </td>
          </tr>
        {/each}
      </tbody>
    </table>
    <p class="faint small foot">{t('Amounts in %1 for %2; each module in its own coin.', productUnit(), productName())}</p>
  </section>
{/if}

<style>
  .portfolio {
    margin: 0 0 1rem;
    padding: 0.9rem 1.1rem 0.6rem;
  }
  header {
    display: flex;
    align-items: center;
    gap: 0.6rem;
    margin-bottom: 0.5rem;
  }
  header h2 {
    font-size: 1rem;
    margin: 0;
  }
  .icon {
    display: inline-grid;
    place-items: center;
    width: 1.9rem;
    height: 1.9rem;
    border-radius: 0.6rem;
    background: var(--accent-soft);
    color: var(--accent-strong);
  }
  .table {
    width: 100%;
    border-collapse: collapse;
  }
  .table th,
  .table td {
    padding: 0.45rem 0.5rem;
    border-bottom: 1px solid var(--line);
    text-align: left;
    vertical-align: middle;
  }
  .table th {
    color: var(--text-faint);
    font-size: 0.78rem;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 0.04em;
  }
  .num {
    text-align: right !important;
    font-variant-numeric: tabular-nums;
    white-space: nowrap;
  }
  .link {
    display: inline-flex;
    align-items: center;
    gap: 0.45rem;
    padding: 0;
    border: 0;
    background: none;
    color: var(--text);
    font: inherit;
    font-weight: 650;
    cursor: pointer;
  }
  .link:hover {
    color: var(--accent-strong);
  }
  .mark {
    display: inline-grid;
    place-items: center;
    width: 1.5rem;
    height: 1.5rem;
    border-radius: 50%;
    background: var(--raised);
    font-size: 0.75rem;
    font-weight: 800;
  }
  .assets {
    display: flex;
    flex-wrap: wrap;
    gap: 0.3rem;
  }
  .asset {
    padding: 0.1rem 0.5rem;
    border-radius: 999px;
    background: var(--surface-2);
    font-size: 0.82rem;
    white-space: nowrap;
  }
  .small {
    font-size: 0.82rem;
  }
  .small-badge {
    font-size: 0.72rem;
  }
  .foot {
    margin: 0.5rem 0 0;
  }
</style>
