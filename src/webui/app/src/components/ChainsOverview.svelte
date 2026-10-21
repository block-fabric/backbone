<script lang="ts">
  // What the wallet holds on each chain shown (doc/unified-app.md §20.2), as rows of one table, never added up across
  // chains: the chain (its mark, its name, the wallet), what is available in its own coin, what is pending (and what is
  // on its way between the chains: deposits made here not yet credited, withdrawals not paid yet), its assets and
  // shielded coins, and its Send and Receive. Overview shows it as a panel of its own (`panel`); Home's Balances card
  // shows the short form (`compact`: a line per chain, what else there is in small words under it).
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
    HOST,
    loadCapabilities,
    pendingDeposits,
    refreshSummary,
    selectedChain,
    setSelectedChain,
    type ChainEntry,
    type ReadView,
  } from '../lib/chains.svelte';

  let { page = 'home', compact = false, panel = true }: { page?: ReadView; compact?: boolean; panel?: boolean } = $props();

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
    const stems = shown.filter((e) => !e.host && e.running).map((e) => e.id);
    const load = () => {
      for (const s of stems) {
        void refreshSummary(s, 4000);
        if (pendingDeposits(s).length) void chainHost.transactions(s, chainWallet(s), 50).then((r) => creditDeposits(s, r.rows), () => undefined);
      }
    };
    load();
    const timer = setInterval(() => {
      if (!document.hidden) load();
    }, 8000);
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
  function moveCoins(e: ChainEntry) {
    chainLink.peg = e.id;
    setSelectedChain(e.id);
    go('sidechains');
  }

  // `said`: words of the app, already translated (t()).
  type UnderPart = { kind: 'pendingHost'; sats: bigint } | { kind: 'pendingSide'; amount: string } | { kind: 'said'; said: string; way: boolean };
  /** The short line under a chain's balance (compact): what is pending, on its way, and how many assets. */
  function underParts(e: ChainEntry): UnderPart[] {
    const out: UnderPart[] = [];
    if (e.host) {
      if (own && own.untrustedPending + own.immature > 0n) out.push({ kind: 'pendingHost', sats: own.untrustedPending + own.immature });
    } else {
      const waiting = balanceOf(chainState.summaries[e.id]).waiting;
      if (waiting && /[1-9]/.test(waiting)) out.push({ kind: 'pendingSide', amount: waiting });
    }
    for (const m of extras(e)) out.push({ kind: 'said', said: m.said, way: m.tone === 'way' });
    const assets = e.host ? 0 : (chainState.summaries[e.id]?.assets?.length ?? 0);
    if (assets) out.push({ kind: 'said', said: assets === 1 ? t('1 asset') : t('%1 assets', String(assets)), way: false });
    return out;
  }

  /** What else is there on a chain, in short words: its assets, shielded coins, coins on their way. */
  function extras(e: ChainEntry): { said: string; tone?: 'way'; kind: 'deposit' | 'withdrawal' }[] {
    const out: { said: string; tone?: 'way'; kind: 'deposit' | 'withdrawal' }[] = [];
    if (e.host) {
      const n = pendingDeposits().length;
      if (n) out.push({ said: n === 1 ? t('1 deposit on its way') : t('%1 deposits on their way', String(n)), tone: 'way', kind: 'deposit' });
      return out;
    }
    const s = chainState.summaries[e.id];
    const deposits = pendingDeposits(e.id).length;
    if (deposits) out.push({ said: deposits === 1 ? t('1 deposit on its way here') : t('%1 deposits on their way here', String(deposits)), tone: 'way', kind: 'deposit' });
    const w = s?.withdrawals?.count ?? 0;
    if (w) out.push({ said: w === 1 ? t('1 withdrawal on its way back') : t('%1 withdrawals on their way back', String(w)), tone: 'way', kind: 'withdrawal' });
    return out;
  }
</script>

{#snippet rows()}
  {#each shown as e (e.id)}
    {@const s = e.host ? undefined : chainState.summaries[e.id]}
    {@const b = e.host ? null : balanceOf(s)}
    {@const st = e.host || !e.entry ? null : describeState(e.entry)}
    {@const more = extras(e)}
    {@const assets = s?.assets ?? []}
    <tr data-chain-card={e.id} class:off={!e.host && !e.running}>
      <td class="chain">
        <ChainBadge id={e.id} name={e.name} />
        <span class="wallet faint">{#if e.host}<SafeText text={wallets.current || t('default wallet')} />{:else if s?.wallet}<SafeText text={s.wallet} />{/if}{#if st && st.tone !== 'ok'}<span class="dot-sep">{' · '}</span><span class="state">{t(st.key, ...st.args)}</span>{/if}</span>
      </td>
      <td class="end figure" data-field="available">
        {#if e.host}<Amount sats={own?.trusted} />{:else}<ModuleAmount text={b?.available} unit={e.unit} coins />{/if}
        {#if compact}
          {@const parts = underParts(e)}
          {#if parts.length}
            <span class="under">{#each parts as p, i (i)}{#if i > 0}<span class="dot-sep">{' · '}</span>{/if}{#if p.kind === 'pendingHost'}{t('pending')} <Amount sats={p.sats} showUnit={false} />{:else if p.kind === 'pendingSide'}{t('pending')} <ModuleAmount text={p.amount} coins />{:else}<span class:way={p.way}>{p.said}</span>{/if}{/each}</span>
          {/if}
        {/if}
      </td>
      {#if !compact}
        <td class="end">
          {#if e.host}<Amount sats={own ? own.untrustedPending + own.immature : undefined} />{:else}<ModuleAmount text={b?.waiting} coins />{/if}
          {#each more as m, i (i)}<span class="under way" data-deposit-pending={m.kind === 'deposit' ? e.id : undefined}>{m.said}</span>{/each}
        </td>
        <td class="assets-cell">
          {#if assets.length}
            <span class="assets" data-card-assets>
              {#each assets.slice(0, 2) as a (a.id)}<button type="button" class="asset" title={t('Send')} onclick={() => send(e, a.id)}><ModuleAmount text={a.balance} /> <SafeText text={a.label} /></button>{/each}
              {#if assets.length > 2}<button type="button" class="link small" onclick={() => openModule(e.id, 'my-assets')}>{t('+%1 more', String(assets.length - 2))}</button>{/if}
            </span>
          {:else if s?.shielded}
            <span class="small">{t('Shielded')} <ModuleAmount text={s.shielded.available} coins /></span>
          {:else}<span class="faint">–</span>{/if}
        </td>
        <td class="end actions">
          <button type="button" class="btn ghost small icon" title={t('Send on %1', e.name)} aria-label={t('Send on %1', e.name)} disabled={!e.host && !e.running} onclick={() => send(e)} data-card-send={e.id}><Icon name="send" size={15} /></button>
          <button type="button" class="btn ghost small icon" title={t('Receive on %1', e.name)} aria-label={t('Receive on %1', e.name)} disabled={!e.host && !e.running} onclick={() => receive(e)}><Icon name="receive" size={15} /></button>
          {#if !e.host}<button type="button" class="btn ghost small icon" title={t('Move coins in or out of %1', e.name)} aria-label={t('Move coins in or out of %1', e.name)} onclick={() => moveCoins(e)}><Icon name="sidechains" size={15} /></button>{/if}
        </td>
      {/if}
    </tr>
  {/each}
{/snippet}

{#if sidechains.length > 0 && sel !== HOST}
  {#if panel}
    <section class="panel chains-overview" aria-labelledby="chains-overview-title" data-chains-overview={page}>
      <div class="panel-head">
        <h2 id="chains-overview-title">{t('Balances')}</h2>
        <span class="muted small">{t('Each chain in its own coin')}</span>
      </div>
      <div class="panel-body">
        <table class="rows" class:compact>
          {#if !compact}
            <thead><tr><th>{t('Chain')}</th><th class="end">{t('Available')}</th><th class="end">{t('Pending')}</th><th>{t('Assets')}</th><th class="end"><span class="sr-only">{t('Actions')}</span></th></tr></thead>
          {/if}
          <tbody>{@render rows()}</tbody>
        </table>
        {#if prefs.mask}<p class="faint small foot">{t('Amounts hidden: privacy mode')}</p>{/if}
      </div>
    </section>
  {:else}
    <table class="rows compact bare" data-chains-overview={page}><tbody>{@render rows()}</tbody></table>
  {/if}
{/if}

<style>
  .chain {
    min-width: 0;
  }
  .chain :global(.chain-badge) {
    font-size: 0.95rem;
  }
  .wallet {
    display: block;
    margin: 0.1rem 0 0 1.85rem;
    font-size: 0.78rem;
    white-space: nowrap;
  }
  .state {
    color: var(--warning);
  }
  .figure {
    font-weight: 600;
  }
  .under {
    display: block;
    margin-top: 0.1rem;
    color: var(--text-muted);
    font-size: 0.78rem;
    font-weight: 450;
    white-space: nowrap;
  }
  .way {
    color: var(--warning);
  }
  tr.off .figure {
    opacity: 0.6;
  }
  .assets-cell {
    max-width: 16rem;
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
    white-space: nowrap;
  }
  .actions .btn {
    min-height: 2rem;
    padding: 0 0.45rem;
  }
  .link {
    padding: 0;
    border: 0;
    background: none;
    color: var(--link);
    cursor: pointer;
    font: inherit;
  }
  .small {
    font-size: 0.82rem;
  }
  .foot {
    margin: 0.5rem 0 0;
  }
  table.compact td {
    padding-top: 0.45rem;
    padding-bottom: 0.45rem;
  }
  table.bare td {
    border-bottom-color: color-mix(in srgb, var(--line) 70%, transparent);
  }
</style>
