<script lang="ts">
  // Assets, on the app's own page (doc/unified-app.md §18): what the wallets hold on every chain that has assets
  // (Layers, BitAssets) in one list with a Chain column and a filter, each sent from the app's Send page on its chain;
  // and their markets (Trade: the chain's DEX), the chain's own pages shown inside this one, on the chain picked.
  import { t } from '../lib/i18n.svelte';
  import Icon from '../components/Icon.svelte';
  import SafeText from '../components/SafeText.svelte';
  import ChainBadge from '../components/ChainBadge.svelte';
  import NotOnThisChain from '../components/NotOnThisChain.svelte';
  import ModuleAmount from '../components/ModuleAmount.svelte';
  import ModuleEmbed from '../components/ModuleEmbed.svelte';
  import AssetLogo from '../components/AssetLogo.svelte';
  import { go, nav } from '../lib/nav.svelte';
  import { canEmbed, chainLink, chainsWith, chainState, loadCapabilities, mergeAssets, refreshSummary, selectedWith, setSelectedChain, type ManyChainAsset } from '../lib/chains.svelte';

  const TABS = [
    { id: '', label: 'Holdings' },
    { id: 'trade', label: 'Trade' },
  ];
  const tab = $derived(nav.tab === 'trade' ? 'trade' : '');
  // The chains of the top bar's selection (doc/unified-app.md §19): with All, every chain's holdings in one list; Trade
  // is one chain's market, so with All it asks which.
  const chains = $derived(chainsWith('assets'));
  const shownChains = $derived(selectedWith('assets'));
  const dexChains = $derived(selectedWith('dex'));
  const tradeOn = $derived(dexChains.length === 1 ? dexChains[0] : undefined);

  $effect(() => {
    void loadCapabilities();
  });
  $effect(() => {
    const stems = shownChains.filter((e) => e.running).map((e) => e.id);
    const load = () => stems.forEach((s) => void refreshSummary(s, 3000));
    load();
    const timer = setInterval(() => {
      if (!document.hidden) load();
    }, 6000);
    return () => clearInterval(timer);
  });

  const rows = $derived<ManyChainAsset[]>(mergeAssets(shownChains.map((e) => chainState.summaries[e.id]).filter((s) => !!s)));

  function send(a: ManyChainAsset) {
    chainLink.send = { chain: a.chain, asset: a.id };
    setSelectedChain(a.chain);
    go('send');
  }
</script>

<div class="page assets">
  <header class="page-head">
    <h1>{t('Assets')}</h1>
  </header>
  <div class="tabs" role="tablist" aria-label={t('Assets')}>
    {#each TABS as tb (tb.id)}
      <button type="button" role="tab" aria-selected={tab === tb.id} onclick={() => (nav.tab = tb.id)}>{t(tb.label)}</button>
    {/each}
  </div>

  {#if shownChains.length === 0}
    <NotOnThisChain what="assets" have={chains} none="No chain with assets is turned on. Turn on Layers or BitAssets in Settings → Sidechains." />
  {:else if tab === ''}
    <section class="panel">
      <table class="table" data-assets-table>
        <thead><tr><th>{t('Chain')}</th><th>{t('Asset')}</th><th class="num">{t('Balance')}</th><th class="num">{t('Pending')}</th><th>{t('Control')}</th><th class="num">{t('Supply')}</th><th></th></tr></thead>
        <tbody>
          {#each rows as a (a.chain + a.id)}
            <tr data-asset-row={a.chain} data-asset={a.id}>
              <td><ChainBadge id={a.chain} name={a.name} small /></td>
              <td><span class="asset-name"><AssetLogo stem={a.chain} id={a.id} label={a.label} /><strong><SafeText text={a.label} /></strong></span> <span class="mono faint small" translate="no"><SafeText text={a.id.slice(0, 10)} />…</span></td>
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
  {:else if dexChains.length > 1}
    <!-- Trade is one chain's market: All shown, the chain is asked (and selected for every page). -->
    <section class="panel pick-trade" data-trade-pick>
      <p><strong>{t('Trade on which chain?')}</strong></p>
      <div class="choices">
        {#each dexChains as e (e.id)}<button type="button" class="btn" onclick={() => setSelectedChain(e.id)} data-trade-chain={e.id}><ChainBadge id={e.id} name={e.name} small /></button>{/each}
      </div>
    </section>
  {:else}
    <NotOnThisChain what="a market of assets" have={chainsWith('dex')} none="No chain with a market of assets is turned on." />
  {/if}
</div>

<style>
  .pick-trade {
    padding: 1.1rem 1.3rem;
  }
  .choices {
    display: flex;
    gap: 0.5rem;
    flex-wrap: wrap;
  }
  .asset-name {
    display: inline-flex;
    align-items: center;
    gap: 0.5rem;
  }
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
