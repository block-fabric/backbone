<script lang="ts">
  // The open positions of the wallet on the chains of markets shown (Hivemind; doc/unified-app.md §19): each market, the
  // outcome held, the shares, the price of that outcome now and what the shares are worth at it, in the chain's coin.
  // On Home only when there are any (a card, its link to Markets); on Markets → Positions always. Every number goes
  // through ModuleAmount: privacy mode hides them (§13.5 #21, #30).
  import { t } from '../lib/i18n.svelte';
  import Icon from './Icon.svelte';
  import SafeText from './SafeText.svelte';
  import ChainBadge from './ChainBadge.svelte';
  import ModuleAmount from './ModuleAmount.svelte';
  import { openChainItem, chainGroupItems } from '../lib/chaingroups';
  import { selectedWith, type ChainEntry } from '../lib/chains.svelte';
  import { many, poll, positionValue, refreshPositions, type ChainPosition } from '../lib/manychains.svelte';
  import { optionTexts, titleWithoutCategory } from '../lib/marketresults';

  let { card = false }: { card?: boolean } = $props();

  const chains = $derived(selectedWith('markets'));
  $effect(() => {
    const stems = chains.filter((e) => e.running).map((e) => e.id);
    return poll(() => stems.forEach((s) => void refreshPositions(s)), 10000);
  });

  const rows = $derived(chains.flatMap((e) => (many.positions[e.id] ?? []).map((p) => ({ e, p }))));
  const errors = $derived(chains.map((e) => ({ e, error: many.errors[`positions:${e.id}`] })).filter((x) => x.error));

  /** The outcome in words: Yes or No for a market of one decision, else the answer to each. */
  function outcome(p: ChainPosition): string {
    const a = p.answers ?? '';
    if (a === 'Y') return t('Yes');
    if (a === 'N') return t('No');
    return a ? a.split('').join(' · ') : t('Outcome %1', String(p.state));
  }
  /** The options of a market on several decisions the outcome answers yes (text of the chain), when its decisions are known. */
  function options(p: ChainPosition): string[] | null {
    const ds = p.decisions ?? [];
    if (ds.length < 2 || ds.some((d) => typeof d.text !== 'string')) return null;
    return optionTexts(ds.map((d) => d.text!)).filter((_, i) => ((p.state >> i) & 1) === 1);
  }
  /** Whether a decision the vote resolved answered against the outcome held: it pays nothing, whatever the others say. */
  function ruledOut(p: ChainPosition): boolean {
    return (p.decisions ?? []).some((d, i) => {
      if (d.status !== 'resolved') return false;
      const o = Number(d.outcome);
      return (o === 0 || o === 1) && ((p.state >> i) & 1) !== o;
    });
  }
  // A position is open while its market trades or waits for the vote ("Voting": trading closed, its price the last of its
  // trading). Once the market pays out, the chain pays every share and keeps no position: it leaves this list, and its
  // result shows on the Markets page and the chain's own pages (doc/unified-app.md §24).
  // The chain's own Portfolio (its group's item), else its Markets.
  function openMarkets(e: ChainEntry) {
    const items = chainGroupItems(e);
    const item = items.find((i) => i.id === 'portfolio') ?? items.find((i) => i.id === 'markets');
    if (item) openChainItem(e.id, item);
  }
</script>

{#if !card || rows.length > 0}
  <section class="panel positions" class:card aria-labelledby="positions-title" data-market-positions={rows.length}>
    <header class="head">
      {#if card}<span class="icon"><Icon name="dashboard" size={17} /></span>{/if}
      <h2 id="positions-title">{card ? t('Open positions') : t('Your positions')}</h2>
      {#if card && chains.length === 1}<ChainBadge id={chains[0].id} name={chains[0].name} small />{/if}
      <span class="grow"></span>
      {#if card && chains[0]}
        <button type="button" class="btn ghost small" onclick={() => openMarkets(chains[0])} data-open-markets><span>{t('Markets')}</span><Icon name="arrow" size={15} /></button>
      {/if}
    </header>
    {#each errors as x (x.e.id)}<p class="error small"><SafeText text={x.e.name} />: <SafeText text={x.error ?? ''} /></p>{/each}
    <table class="table">
      <thead>
        <tr>
          {#if chains.length > 1}<th>{t('Chain')}</th>{/if}
          <th>{t('Market')}</th><th>{t('Outcome')}</th><th class="num">{t('Shares')}</th><th class="num">{t('Price now')}</th><th class="num">{t('Worth now')}</th>
        </tr>
      </thead>
      <tbody>
        {#each rows.slice(0, card ? 5 : 200) as { e, p } (e.id + p.market + p.state + p.account)}
          {@const named = options(p)}
          {@const out = ruledOut(p)}
          <tr data-position={p.market}>
            {#if chains.length > 1}<td><ChainBadge id={e.id} name={e.name} small /></td>{/if}
            <td class="market"><SafeText text={titleWithoutCategory(p.text ?? '') || `${p.market.slice(0, 12)}…`} />{#if p.status === 'closed'}{' '}<span class="badge warning" data-position-status="closed">{t('Voting')}</span>{:else if p.status && p.status !== 'trading'}{' '}<span class="badge" data-position-status={p.status}><SafeText text={p.status} /></span>{/if}</td>
            <td>
              {#if named}
                <span class="badge" class:accent={!out}>{#if named.length}{#each named as o, i (i)}{#if i > 0}{' + '}{/if}<SafeText text={o} />{/each}{:else}{t('None of them')}{/if}</span>
              {:else}
                <span class="badge" class:accent={!out}>{outcome(p)}</span>
              {/if}
            </td>
            <td class="num"><ModuleAmount text={p.shares} /></td>
            {#if out}
              <td class="num faint" data-ruled-out title={t('A decision the vote resolved answered against this outcome: it pays nothing')}>{t('Ruled out')}</td>
              <td class="num"><strong><ModuleAmount text="0" unit={e.unit} coins /></strong></td>
            {:else}
              <td class="num" title={p.status === 'closed' ? t('Trading closed: its last price, until the vote decides what a share pays') : undefined}><ModuleAmount text={p.price ?? null} />{#if p.status === 'closed' && p.price}<span class="final faint">{t('final')}</span>{/if}</td>
              <td class="num"><strong><ModuleAmount text={positionValue(p) || null} unit={e.unit} coins /></strong></td>
            {/if}
          </tr>
        {:else}
          <tr><td colspan="6" class="muted">{chains.length ? t('No open positions: shares bought on a market show here.') : t('No chain of markets is shown.')}</td></tr>
        {/each}
      </tbody>
    </table>
    {#if card && rows.length > 5}<p class="faint small more">{t('%1 more on the Markets page', String(rows.length - 5))}</p>{/if}
  </section>
{/if}

<style>
  .positions {
    padding: 0.9rem 1.1rem;
  }
  .head {
    display: flex;
    align-items: center;
    gap: 0.6rem;
    margin-bottom: 0.6rem;
  }
  .head h2 {
    margin: 0;
    font-size: 1rem;
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
    letter-spacing: 0.04em;
  }
  .table .num {
    text-align: right;
  }
  .market {
    max-width: 22rem;
  }
  .small {
    font-size: 0.82rem;
  }
  .more {
    margin: 0.5rem 0 0;
  }
  .final {
    margin-left: 0.35rem;
    font-size: 0.72rem;
    text-transform: uppercase;
    letter-spacing: 0.03em;
  }
</style>
