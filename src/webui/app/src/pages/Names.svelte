<script lang="ts">
  // Names, on the app's own page (doc/unified-app.md §18): the names and reservations the wallets hold on the chains
  // that keep names (BitNames), with a filter; registering, records and transfers are the chain's own pages, shown here
  // inside this one, their questions asked in the app's trusted dialog.
  import { t } from '../lib/i18n.svelte';
  import SafeText from '../components/SafeText.svelte';
  import ChainBadge from '../components/ChainBadge.svelte';
  import ChainChips from '../components/ChainChips.svelte';
  import ModuleEmbed from '../components/ModuleEmbed.svelte';
  import { errorText } from '../lib/dialogs.svelte';
  import { canEmbed, chainHost, chainsWith, chainWallet, filtered, loadCapabilities, viewFilter, type ChainName } from '../lib/chains.svelte';

  const chains = $derived(chainsWith('names'));
  const shown = $derived(filtered(chains, viewFilter('names', chains)));
  let names = $state<Record<string, ChainName[]>>({});
  let errors = $state<Record<string, string>>({});
  let manage = $state<string | null>(null);

  $effect(() => {
    void loadCapabilities();
  });
  $effect(() => {
    for (const e of shown) {
      if (!e.running) continue;
      const s = e.id;
      chainHost.names(s, chainWallet(s)).then(
        (r) => (names[s] = r.names),
        (err) => (errors[s] = errorText(err)),
      );
    }
  });
</script>

<div class="page names">
  <header class="page-head">
    <h1>{t('Names')}</h1>
    <ChainChips page="names" entries={chains} />
  </header>
  {#if chains.length === 0}<p class="muted">{t('No chain with names is turned on.')}</p>{/if}
  {#each shown as e (e.id)}
    <section class="panel" data-names-chain={e.id}>
      <div class="panel-head">
        <h2><ChainBadge id={e.id} name={e.name} /></h2>
        <span class="grow"></span>
        {#if canEmbed(e.id, 'names')}<button type="button" class="btn small" onclick={() => (manage = manage === e.id ? null : e.id)}>{manage === e.id ? t('Hide') : t('Register, records, transfers…')}</button>{/if}
      </div>
      {#if errors[e.id]}<p class="panel-body error"><SafeText text={errors[e.id]} /></p>{/if}
      <table class="table">
        <thead><tr><th>{t('Name')}</th><th>{t('Kind')}</th><th>{t('Address')}</th></tr></thead>
        <tbody>
          {#each names[e.id] ?? [] as n, i (i)}
            <tr><td><strong><SafeText text={n.name ?? (n.namehash ? n.namehash.slice(0, 12) + '…' : '–')} /></strong></td><td><SafeText text={n.kind ?? ''} /></td><td class="mono small" translate="no"><SafeText text={n.address ?? ''} /></td></tr>
          {:else}
            <tr><td colspan="3" class="muted">{t('No names yet')}</td></tr>
          {/each}
        </tbody>
      </table>
      {#if manage === e.id}<div class="panel-body"><ModuleEmbed stem={e.id} page="names" name={e.name} /></div>{/if}
    </section>
  {/each}
</div>

<style>
  .names {
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
  .small {
    font-size: 0.82rem;
  }
</style>
