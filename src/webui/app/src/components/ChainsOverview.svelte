<script lang="ts">
  // Every chain this app runs at once (doc/unified-app.md §18), on Home and on Overview: a card per chain with what its
  // wallet holds (in its own coin, never added up across chains), what is on its way (pending, deposits made here not yet
  // credited, withdrawals not paid yet), its assets and shielded coins, and how its node is doing; a filter of chains
  // follows the top bar's selection (doc/unified-app.md §19): All, every chain; one sidechain, its card alone; this
  // app's own, nothing here (the page's own sections are that chain's). A card's buttons open the app's own Send and
  // Receive on that chain (selecting it).
  import { t } from '../lib/i18n.svelte';
  import Amount from './Amount.svelte';
  import ModuleAmount from './ModuleAmount.svelte';
  import SafeText from './SafeText.svelte';
  import ChainBadge from './ChainBadge.svelte';
  import Icon from './Icon.svelte';
  import { go } from '../lib/nav.svelte';
  import { prefs, wallets } from '../lib/state.svelte';
  import { openModule, productName } from '../lib/modules.svelte';
  import { describeState } from '../lib/unified';
  import {
    balanceOf,
    chainEntries,
    chainHost,
    chainLink,
    chainState,
    chainWallet,
    creditDeposits,
    filtered,
    ALL,
    HOST,
    loadCapabilities,
    pendingDeposits,
    refreshSummary,
    selectedChain,
    setSelectedChain,
    type ChainEntry,
    type ReadView,
  } from '../lib/chains.svelte';

  let { page = 'home' }: { page?: ReadView } = $props();

  const entries = $derived(chainEntries());
  const sidechains = $derived(entries.filter((e) => !e.host));
  const sel = $derived(selectedChain(entries));
  const shown = $derived(filtered(entries, sel));
  const own = $derived(wallets.current !== null ? wallets.balances[wallets.current] : undefined);

  $effect(() => {
    void loadCapabilities();
  });
  // Each sidechain's summary while shown, every few seconds; its history too while a deposit is on its way there.
  $effect(() => {
    const stems = sidechains.filter((e) => e.running).map((e) => e.id);
    const load = () => {
      for (const s of stems) {
        void refreshSummary(s, 3000);
        if (pendingDeposits(s).length) void chainHost.transactions(s, chainWallet(s), 50).then((r) => creditDeposits(s, r.rows), () => undefined);
      }
    };
    load();
    const timer = setInterval(() => {
      if (!document.hidden) load();
    }, 5000);
    return () => clearInterval(timer);
  });

  function send(e: ChainEntry, asset?: string) {
    chainLink.send = { chain: e.id, ...(asset ? { asset } : {}) };
    setSelectedChain(e.id);
    go('send');
  }
  function receive(e: ChainEntry) {
    chainLink.receive = { chain: e.id };
    setSelectedChain(e.id);
    go('receive');
  }
</script>

{#if sidechains.length > 0 && sel !== HOST}
  <section class="panel chains-overview" aria-labelledby="chains-overview-title" data-chains-overview={page}>
    <header>
      <span class="icon"><Icon name="modules" size={17} /></span>
      <h2 id="chains-overview-title">{sel === ALL ? t('Every chain') : t('On %1', shown[0]?.name ?? '')}</h2>
      <span class="muted small grow">{t('Each chain\'s coins are its own: never added up')}</span>
    </header>
    <div class="cards">
      {#each shown as e (e.id)}
        {#if e.host}
          <article class="card" data-chain-card={HOST}>
            <div class="top"><ChainBadge id={HOST} name={productName()} /><span class="muted small"><SafeText text={wallets.current || t('default wallet')} /></span></div>
            <div class="figure"><span class="muted small">{t('Available')}</span><strong><Amount sats={own?.trusted} /></strong></div>
            <div class="line"><span class="muted small">{t('Pending')}</span><Amount sats={own ? own.untrustedPending + own.immature : undefined} /></div>
            {#if pendingDeposits().length}
              <div class="line"><span class="muted small">{t('Deposits on their way')}</span><span>{pendingDeposits().length}</span></div>
            {/if}
            <div class="actions">
              <button type="button" class="btn small" onclick={() => send(e)} data-card-send={HOST}><Icon name="send" size={14} /> {t('Send')}</button>
              <button type="button" class="btn small" onclick={() => receive(e)}><Icon name="receive" size={14} /> {t('Receive')}</button>
            </div>
          </article>
        {:else}
          {@const s = chainState.summaries[e.id]}
          {@const b = balanceOf(s)}
          {@const st = e.entry ? describeState(e.entry) : null}
          <article class="card" data-chain-card={e.id}>
            <div class="top">
              <ChainBadge id={e.id} name={e.name} />
              {#if st && st.tone !== 'ok'}<span class="badge small-badge">{t(st.key, ...st.args)}</span>{/if}
              {#if s?.wallet}<span class="muted small"><SafeText text={s.wallet} />{#if s.walletMode === 'separate'} · {t('own backup')}{/if}</span>{/if}
            </div>
            <div class="figure"><span class="muted small">{t('Available')}</span><strong data-field="available"><ModuleAmount text={b.available} unit={e.unit} coins /></strong></div>
            <div class="line"><span class="muted small">{t('Pending')}</span><ModuleAmount text={b.waiting} coins /></div>
            {#if s?.shielded}
              <div class="line"><span class="muted small">{t('Shielded')}</span><ModuleAmount text={s.shielded.available} coins /></div>
            {/if}
            {#each pendingDeposits(e.id) as d (d.txid)}
              <div class="line way" data-deposit-pending={e.id}><span class="muted small">{t('Deposit on its way')}</span><ModuleAmount text={d.amount} coins /></div>
            {/each}
            {#if s?.withdrawals && s.withdrawals.count > 0}
              <div class="line way"><span class="muted small">{t('Withdrawals not paid yet: %1', String(s.withdrawals.count))}</span><ModuleAmount text={s.withdrawals.amount} coins /></div>
            {/if}
            {#if s?.assets?.length}
              <div class="assets" data-card-assets>
                {#each s.assets.slice(0, 6) as a (a.id)}<button type="button" class="asset" title={t('Send')} onclick={() => send(e, a.id)}><ModuleAmount text={a.balance} /> <SafeText text={a.label} /></button>{/each}
                {#if s.assets.length > 6}<button type="button" class="link small" onclick={() => go('assets')}>+{s.assets.length - 6}</button>{/if}
              </div>
            {/if}
            {#if s?.height !== undefined}<div class="faint small">{t('Block %1', String(s.height))}{#if s.mainchain?.state && s.mainchain.state !== 'ok'} · <SafeText text={s.mainchain.state} />{/if}</div>{/if}
            <div class="actions">
              <button type="button" class="btn small" disabled={!e.running} onclick={() => send(e)} data-card-send={e.id}><Icon name="send" size={14} /> {t('Send')}</button>
              <button type="button" class="btn small" disabled={!e.running} onclick={() => receive(e)}><Icon name="receive" size={14} /> {t('Receive')}</button>
              <button type="button" class="btn ghost small" onclick={() => openModule(e.id, 'home')} title={t('Its own pages')}><Icon name="modules" size={14} /></button>
            </div>
          </article>
        {/if}
      {/each}
    </div>
    {#if prefs.mask}<p class="faint small foot">{t('Amounts hidden: privacy mode')}</p>{/if}
  </section>
{/if}

<style>
  .chains-overview {
    margin: 0 0 1rem;
    padding: 0.9rem 1.1rem;
  }
  header {
    display: flex;
    align-items: center;
    gap: 0.6rem;
    margin-bottom: 0.8rem;
    flex-wrap: wrap;
  }
  header h2 {
    font-size: 1rem;
    margin: 0;
  }
  .grow {
    flex: 1;
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
  .cards {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(15.5rem, 1fr));
    gap: 0.8rem;
  }
  .card {
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
    padding: 0.8rem 0.9rem;
    border: 1px solid var(--line);
    border-radius: var(--radius-m, 0.8rem);
    background: var(--surface-2);
    min-width: 0;
  }
  .top {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    flex-wrap: wrap;
  }
  .figure {
    display: flex;
    flex-direction: column;
    gap: 0.1rem;
    font-size: 1.15rem;
  }
  .line {
    display: flex;
    justify-content: space-between;
    gap: 0.5rem;
    font-size: 0.9rem;
  }
  .way {
    color: var(--warning, var(--text));
  }
  .assets {
    display: flex;
    flex-wrap: wrap;
    gap: 0.3rem;
  }
  .asset {
    padding: 0.1rem 0.55rem;
    border: 0;
    border-radius: 999px;
    background: var(--raised);
    color: var(--text);
    font: inherit;
    font-size: 0.8rem;
    cursor: pointer;
    white-space: nowrap;
  }
  .actions {
    display: flex;
    gap: 0.4rem;
    margin-top: auto;
    padding-top: 0.3rem;
  }
  .link {
    padding: 0;
    border: 0;
    background: none;
    color: var(--accent-strong);
    cursor: pointer;
    font: inherit;
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
