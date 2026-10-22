<script lang="ts">
  // Markets, on the app's own page (doc/unified-app.md §18, §24): the prediction markets of the chains that have them
  // (Hivemind), each with its stage and its outcomes as bars: the chance of each while it trades, the last prices once
  // trading closed, and once it paid out what it came to (the outcome that won, what a share of each paid, the last
  // price of each before trading closed, when it paid). Trading, decisions and votes are the chain's own pages, shown
  // here inside this one, their questions asked in the app's trusted dialog.
  import { t } from '../lib/i18n.svelte';
  import SafeText from '../components/SafeText.svelte';
  import ChainBadge from '../components/ChainBadge.svelte';
  import ModuleEmbed from '../components/ModuleEmbed.svelte';
  import MarketPositions from '../components/MarketPositions.svelte';
  import NotOnThisChain from '../components/NotOnThisChain.svelte';
  import { errorText } from '../lib/dialogs.svelte';
  import { nav } from '../lib/nav.svelte';
  import MarketOutcomes from '../components/MarketOutcomes.svelte';
  import { formatTime } from '../lib/format';
  import { categoryOf, marketView, titleWithoutCategory } from '../lib/marketresults';
  import { canEmbed, chainHost, chainsWith, loadCapabilities, selectedWith, type ChainDecision, type ChainMarket, type ChainMarketsInfo } from '../lib/chains.svelte';

  // The chains of the top bar's selection that have markets (doc/unified-app.md §19).
  const chains = $derived(chainsWith('markets'));
  const shown = $derived(selectedWith('markets'));
  const TABS = [
    { id: '', label: 'Markets' },
    { id: 'positions', label: 'Positions' },
  ];
  const tab = $derived(nav.tab === 'positions' ? 'positions' : '');
  /** The words of a stage, short (a chip). */
  function describeStage(stage: string): string {
    switch (stage) {
      case 'trading':
        return t('Live');
      case 'voting':
        return t('Voting');
      case 'revealing':
        return t('Revealing');
      case 'counting':
        return t('Counting');
      case 'partly':
        return t('Partly decided');
      default:
        return t('Paid out');
    }
  }
  /** The markets of a chain with what they came to: those that trade first, then those voted on, then those that paid out; the newest first. */
  const ORDER: Record<string, number> = { trading: 0, voting: 1, revealing: 1, counting: 1, partly: 1, paid: 2 };
  function rowsOf(stem: string) {
    return (markets[stem] ?? [])
      .map((m) => ({ m, v: marketView(m, decisions[stem] ?? new Map(), infos[stem]) }))
      .sort((a, b) => ORDER[a.v.stage] - ORDER[b.v.stage] || (b.m.height ?? 0) - (a.m.height ?? 0));
  }
  let markets = $state<Record<string, ChainMarket[]>>({});
  let decisions = $state<Record<string, Map<string, ChainDecision>>>({});
  let infos = $state<Record<string, ChainMarketsInfo | null>>({});
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
        (r) => {
          markets[s] = r.markets ?? [];
          decisions[s] = new Map((r.decisions ?? []).filter((d) => typeof d.id === 'string').map((d) => [d.id!, d]));
          infos[s] = r.info ?? null;
        },
        (err) => (errors[s] = errorText(err)),
      );
    }
  });
</script>

<div class="page markets">
  <header class="page-head">
    <h1>{t('Markets')}</h1>
  </header>
  {#if shown.length === 0}
    <NotOnThisChain what="markets" have={chains} none="No chain with markets is turned on." />
  {:else}
  <div class="tabs" role="tablist" aria-label={t('Markets')}>
    {#each TABS as tb (tb.id)}
      <button type="button" role="tab" aria-selected={tab === tb.id} data-markets-tab={tb.id || 'markets'} onclick={() => (nav.tab = tb.id)}>{t(tb.label)}</button>
    {/each}
  </div>
  {#if tab === 'positions'}
    <MarketPositions />
  {:else}
  {#each shown as e (e.id)}
    <section class="panel" data-markets-chain={e.id}>
      <div class="panel-head">
        <h2><ChainBadge id={e.id} name={e.name} /></h2>
        <span class="grow"></span>
        {#if canEmbed(e.id, 'markets')}<button type="button" class="btn small" onclick={() => (manage = manage === e.id ? null : e.id)}>{manage === e.id ? t('Hide') : t('Trade, decisions, votes…')}</button>{/if}
      </div>
      {#if errors[e.id]}<p class="panel-body error"><SafeText text={errors[e.id]} /></p>{/if}
      <div class="panel-body">
      <table class="rows market-rows">
        <thead><tr><th>{t('Market')}</th><th>{t('Status')}</th><th>{t('Outcomes')}</th></tr></thead>
        <tbody>
          {#each rowsOf(e.id) as { m, v }, i (m.id ?? i)}
            {@const category = categoryOf(m.text ?? '')}
            <tr data-market={m.id ?? ''} data-stage={v.stage}>
              <td class="title">
                <SafeText text={titleWithoutCategory(m.text ?? '') || m.id || ''} />
                <span class="faint small">{#if category}<SafeText text={category} /><span class="dot-sep">{' · '}</span>{/if}{v.kind === 'binary' ? t('Yes or no') : v.kind === 'scalar' ? t('Scale') : t('%1 outcomes', String(v.rows.filter((r) => r.exclusive).length))}</span>
              </td>
              <td class="stage-cell">
                <span class="badge" class:accent={v.stage === 'trading'} class:warning={v.stage !== 'trading' && v.stage !== 'paid'}>{describeStage(v.stage)}</span>
                {#if v.paid}
                  <span class="faint small num">{v.paid.height !== undefined ? t('Paid out at block %1', String(v.paid.height)) : v.paid.time !== undefined ? t('Paid out at the start of period %1, %2', String(v.paid.period), formatTime(v.paid.time)) : t('Paid out in period %1', String(v.paid.period))}</span>
                {:else if v.stage !== 'trading' && v.closes !== null}
                  <span class="faint small num">{t('Voted on until period %1', String(v.closes))}</span>
                {/if}
              </td>
              <td class="odds"><MarketOutcomes view={v} /></td>
            </tr>
          {:else}
            <tr><td colspan="3" class="muted">{t('No markets yet')}</td></tr>
          {/each}
        </tbody>
      </table>
      </div>
      {#if manage === e.id}<div class="panel-body"><ModuleEmbed stem={e.id} page="markets" name={e.name} /></div>{/if}
    </section>
  {/each}
  {/if}
  {/if}
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
  .market-rows td {
    vertical-align: top;
    padding-top: 0.7rem;
    padding-bottom: 0.7rem;
  }
  .title {
    max-width: 22rem;
    overflow-wrap: anywhere;
  }
  .title .small,
  .stage-cell .small {
    display: block;
    margin-top: 0.25rem;
  }
  .stage-cell {
    white-space: nowrap;
  }
  .odds {
    width: 45%;
    min-width: 14rem;
  }
  .small {
    font-size: 0.8rem;
  }
</style>
