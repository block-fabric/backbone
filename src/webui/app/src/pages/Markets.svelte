<script lang="ts">
  // Markets, on the app's own page (doc/unified-app.md §18): the prediction markets of the chains that have them
  // (Hivemind), with the price of each state; trading, decisions and votes are the chain's own pages, shown here inside
  // this one, their questions asked in the app's trusted dialog.
  import { t } from '../lib/i18n.svelte';
  import SafeText from '../components/SafeText.svelte';
  import ChainBadge from '../components/ChainBadge.svelte';
  import ChainChips from '../components/ChainChips.svelte';
  import ModuleEmbed from '../components/ModuleEmbed.svelte';
  import { errorText } from '../lib/dialogs.svelte';
  import { canEmbed, chainHost, chainsWith, filtered, loadCapabilities, viewFilter, type ChainMarket } from '../lib/chains.svelte';

  const chains = $derived(chainsWith('markets'));
  const shown = $derived(filtered(chains, viewFilter('markets', chains)));
  let markets = $state<Record<string, ChainMarket[]>>({});
  let errors = $state<Record<string, string>>({});
  let manage = $state<string | null>(null);

  $effect(() => {
    void loadCapabilities();
  });
  $effect(() => {
    for (const e of shown) {
      if (!e.running) continue;
      const s = e.id;
      chainHost.markets(s).then(
        (r) => (markets[s] = r.markets),
        (err) => (errors[s] = errorText(err)),
      );
    }
  });
</script>

<div class="page markets">
  <header class="page-head">
    <h1>{t('Markets')}</h1>
    <ChainChips page="markets" entries={chains} />
  </header>
  {#if chains.length === 0}<p class="muted">{t('No chain with markets is turned on.')}</p>{/if}
  {#each shown as e (e.id)}
    <section class="panel" data-markets-chain={e.id}>
      <div class="panel-head">
        <h2><ChainBadge id={e.id} name={e.name} /></h2>
        <span class="grow"></span>
        {#if canEmbed(e.id, 'markets')}<button type="button" class="btn small" onclick={() => (manage = manage === e.id ? null : e.id)}>{manage === e.id ? t('Hide') : t('Trade, decisions, votes…')}</button>{/if}
      </div>
      {#if errors[e.id]}<p class="panel-body error"><SafeText text={errors[e.id]} /></p>{/if}
      <table class="table">
        <thead><tr><th>{t('Market')}</th><th>{t('Status')}</th><th>{t('Prices')}</th></tr></thead>
        <tbody>
          {#each markets[e.id] ?? [] as m, i (i)}
            <tr>
              <td><SafeText text={m.text ?? m.id ?? ''} /></td>
              <td><SafeText text={m.status ?? ''} /></td>
              <td class="prices">{#each m.states ?? [] as st, j (j)}<span class="price"><SafeText text={st.answers ?? String(st.state ?? '')} /> <SafeText text={st.price ?? '–'} /></span>{/each}</td>
            </tr>
          {:else}
            <tr><td colspan="3" class="muted">{t('No markets yet')}</td></tr>
          {/each}
        </tbody>
      </table>
      {#if manage === e.id}<div class="panel-body"><ModuleEmbed stem={e.id} page="markets" name={e.name} /></div>{/if}
    </section>
  {/each}
</div>

<style>
  .markets {
    display: flex;
    flex-direction: column;
    gap: 1rem;
  }
  .page-head,
  .panel-head {
    display: flex;
    align-items: center;
    gap: 0.8rem;
    flex-wrap: wrap;
  }
  .panel-head h2 {
    margin: 0;
    font-size: 1rem;
  }
  .grow {
    flex: 1;
  }
  .table {
    width: 100%;
    border-collapse: collapse;
  }
  .table th,
  .table td {
    padding: 0.4rem 0.55rem;
    border-bottom: 1px solid var(--line);
    text-align: left;
  }
  .table th {
    color: var(--text-faint);
    font-size: 0.75rem;
    text-transform: uppercase;
  }
  .prices {
    display: flex;
    gap: 0.4rem;
    flex-wrap: wrap;
  }
  .price {
    padding: 0.05rem 0.45rem;
    border-radius: 999px;
    background: var(--surface-2);
    font-size: 0.82rem;
  }
</style>
