<script lang="ts">
  // Assets, on the app's own page (doc/unified-app.md §18): what the wallets hold on every chain that has assets
  // (Layers, BitAssets) in one list with a Chain column and a filter, each sent from the app's Send page on its chain;
  // and their markets (Trade: the chain's DEX), the chain's own pages shown inside this one, on the chain picked.
  import { t } from '../lib/i18n.svelte';
  import Icon from '../components/Icon.svelte';
  import SafeText from '../components/SafeText.svelte';
  import ChainBadge from '../components/ChainBadge.svelte';
  import ChainChips from '../components/ChainChips.svelte';
  import ChainPicker from '../components/ChainPicker.svelte';
  import ModuleAmount from '../components/ModuleAmount.svelte';
  import ModuleEmbed from '../components/ModuleEmbed.svelte';
  import { go, nav } from '../lib/nav.svelte';
  import { actionChain, canEmbed, chainLink, chainsWith, chainState, filtered, loadCapabilities, mergeAssets, refreshSummary, rowsOf, setActionChain, viewFilter, type ManyChainAsset } from '../lib/chains.svelte';

  const TABS = [
    { id: '', label: 'Holdings' },
    { id: 'trade', label: 'Trade' },
  ];
  const tab = $derived(nav.tab === 'trade' ? 'trade' : '');
  const chains = $derived(chainsWith('assets'));
  const dexChains = $derived(chainsWith('dex'));
  const filter = $derived(viewFilter('assets', chains));
  let tradeChain = $state(actionChain('trade', chainsWith('dex')));
  $effect(() => {
    if (dexChains.length && !dexChains.some((e) => e.id === tradeChain)) tradeChain = dexChains[0].id;
  });
  const tradeOn = $derived(dexChains.find((e) => e.id === tradeChain));

  $effect(() => {
    void loadCapabilities();
  });
  $effect(() => {
    const stems = filtered(chains, filter).filter((e) => e.running).map((e) => e.id);
    const load = () => stems.forEach((s) => void refreshSummary(s, 3000));
    load();
    const timer = setInterval(() => {
      if (!document.hidden) load();
    }, 6000);
    return () => clearInterval(timer);
  });

  const rows = $derived<ManyChainAsset[]>(rowsOf(mergeAssets(chains.map((e) => chainState.summaries[e.id]).filter((s) => !!s)), filter));

  function send(a: ManyChainAsset) {
    chainLink.send = { chain: a.chain, asset: a.id };
    setActionChain('send', a.chain);
    go('send');
  }
</script>

<div class="page assets">
  <header class="page-head">
    <h1>{t('Assets')}</h1>
    {#if tab === ''}<ChainChips page="assets" entries={chains} />{:else}<span data-trade-chain><ChainPicker page="trade" bind:value={tradeChain} entries={dexChains} /></span>{/if}
  </header>
  <div class="tabs" role="tablist" aria-label={t('Assets')}>
    {#each TABS as tb (tb.id)}
      <button type="button" role="tab" aria-selected={tab === tb.id} onclick={() => (nav.tab = tb.id)}>{t(tb.label)}</button>
    {/each}
  </div>

  {#if chains.length === 0}
    <p class="muted">{t('No chain with assets is turned on. Turn on Layers or BitAssets in Settings → Sidechains.')}</p>
  {:else if tab === ''}
    <section class="panel">
      <table class="table" data-assets-table>
        <thead><tr><th>{t('Chain')}</th><th>{t('Asset')}</th><th class="num">{t('Balance')}</th><th class="num">{t('Pending')}</th><th>{t('Control')}</th><th class="num">{t('Supply')}</th><th></th></tr></thead>
        <tbody>
          {#each rows as a (a.chain + a.id)}
            <tr data-asset-row={a.chain} data-asset={a.id}>
              <td><ChainBadge id={a.chain} name={a.name} small /></td>
              <td><strong><SafeText text={a.label} /></strong> <span class="mono faint small" translate="no"><SafeText text={a.id.slice(0, 10)} />…</span></td>
              <td class="num"><ModuleAmount text={a.balance} /></td>
              <td class="num"><ModuleAmount text={a.pending} /></td>
              <td>{#if a.control}<span class="badge accent">{t('you control it')}</span>{/if}</td>
              <td class="num"><ModuleAmount text={a.supply} /></td>
              <td><button type="button" class="btn small" onclick={() => send(a)} data-asset-send={a.id}><Icon name="send" size={14} /> {t('Send')}</button></td>
            </tr>
          {:else}
            <tr><td colspan="7" class="muted">{t('No assets in the wallets of these chains')}</td></tr>
          {/each}
        </tbody>
      </table>
    </section>
  {:else if tradeOn}
    {#if canEmbed(tradeOn.id, 'trade')}
      {#key tradeOn.id}<ModuleEmbed stem={tradeOn.id} page="trade" name={tradeOn.name} height="40rem" />{/key}
    {/if}
  {:else}
    <p class="muted">{t('No chain with a market of assets is turned on.')}</p>
  {/if}
</div>

<style>
  .assets {
    display: flex;
    flex-direction: column;
    gap: 1rem;
  }
  .page-head {
    display: flex;
    align-items: center;
    gap: 1rem;
    flex-wrap: wrap;
  }
  .table {
    width: 100%;
    border-collapse: collapse;
  }
  .table th,
  .table td {
    padding: 0.45rem 0.55rem;
    border-bottom: 1px solid var(--line);
    text-align: left;
  }
  .table th {
    color: var(--text-faint);
    font-size: 0.75rem;
    text-transform: uppercase;
  }
  .num {
    text-align: right !important;
    font-variant-numeric: tabular-nums;
  }
  .small {
    font-size: 0.8rem;
  }
</style>
