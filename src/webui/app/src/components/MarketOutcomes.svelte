<script lang="ts">
  // The outcomes of a prediction market as bars (the app's Markets page, doc/unified-app.md §24), whatever its stage:
  // while it trades, the chance of the likeliest; once trading closed, the last prices it closed at (a state a resolved
  // decision answered against said ruled out); once it paid out, the outcome that won checked, marked Won and filled
  // with what a share of it paid, the others muted at what they paid, each bar with a thin faint line where its last
  // price before trading closed stood (did the market call it?). A market on a scale that paid out: its scale, the answer
  // of the vote and where the market had put it. In the colours Hivemind's own pages give each state. Drawn without
  // inline styles (the content security policy).
  import { t } from '../lib/i18n.svelte';
  import { css } from '../lib/actions';
  import Icon from './Icon.svelte';
  import SafeText from './SafeText.svelte';
  import type { MarketResultView, OutcomeLabel, OutcomeRow } from '../lib/marketresults';

  let { view, limit = 3 }: { view: MarketResultView; limit?: number } = $props();

  const paidOut = $derived(view.stage === 'paid' && view.rows.some((r) => r.pays !== null));
  const rows = $derived(view.rows.filter((r) => r.exclusive || r.won).slice(0, limit));
  const pct = (x: number) => `${Math.max(0, Math.min(100, x * 100))}%`;
  const chance = (x: number) => {
    const p = x * 100;
    return `${p >= 10 || p === 0 ? p.toFixed(1) : p.toFixed(2)}%`;
  };
  const fmt = (x: number) => (Number.isInteger(x) ? String(x) : x.toFixed(2));
  /** A word of the app for an outcome. */
  function wordOf(w: 'Yes' | 'No' | 'High' | 'Low' | 'None of them'): string {
    switch (w) {
      case 'Yes':
        return t('Yes');
      case 'No':
        return t('No');
      case 'High':
        return t('High');
      case 'Low':
        return t('Low');
      default:
        return t('None of them');
    }
  }
  /** An outcome in words, for a sentence (the options as the chain wrote them). */
  const words = (l: OutcomeLabel) => ('word' in l ? wordOf(l.word) : l.options.join(' + '));
  const fav = $derived(view.favourite === null ? null : (view.rows.find((r) => r.state === view.favourite) ?? null));
  const place = (x: number, s: { min: number; max: number }) => (s.max > s.min ? Math.max(0, Math.min(1, (x - s.min) / (s.max - s.min))) : 0);
</script>

{#snippet label(r: OutcomeRow)}
  {#if 'word' in r.label}{wordOf(r.label.word)}{:else}{#each r.label.options as o, i (i)}{#if i > 0}<span class="plus">{' + '}</span>{/if}<SafeText text={o} />{/each}{/if}
{/snippet}

<div class="outcomes" data-market-outcomes={view.stage}>
  {#if paidOut && view.scale && view.scale.answer !== null}
    {@const s = view.scale}
    {@const answer = s.answer ?? 0}
    <div class="scale" role="img" aria-label={t('On a scale from %1 to %2: the vote said %3, the market %4', fmt(s.min), fmt(s.max), fmt(answer), fmt(s.estimate))}>
      <span class="answer"><span class="check" aria-hidden="true"><Icon name="check" size={10} /></span>{t('Resolved: %1', fmt(answer))}</span>
      <span class="track">
        <span class="fill won" use:css={{ width: pct(place(answer, s)) }}></span>
        <span class="dot est" use:css={{ left: pct(place(s.estimate, s)) }}></span>
        <span class="dot val" use:css={{ left: pct(place(answer, s)) }}></span>
      </span>
      <span class="ends num"><span>{fmt(s.min)}</span><span>{fmt(s.max)}</span></span>
      <span class="note">{t('The market said %1 when trading closed', fmt(s.estimate))}</span>
    </div>
  {:else}
    <ul>
      {#each rows as r (r.state)}
        <li class="c{r.state % 8}" class:lost={paidOut && r.lost} class:out={r.out}>
          <span class="name">
            {#if r.won}<span class="check" aria-hidden="true"><Icon name="check" size={10} /></span>{:else}<span class="dot"></span>{/if}
            <span class="cut">{@render label(r)}</span>
            {#if r.won}<span class="chip">{t('Won')}</span>{:else if paidOut && view.tie}<span class="chip tie">{t('Tie')}</span>{/if}
          </span>
          {#if paidOut}
            <span class="num pct" title={t('A share paid %1 of a coin; %2 when trading closed', chance(r.pays ?? 0), chance(r.price))}>{chance(r.pays ?? 0)}</span>
          {:else if r.out}
            <span class="faint small">{t('Ruled out')}</span>
          {:else}
            <span class="num pct">{chance(r.price)}</span>
          {/if}
          <span class="bar" class:won={r.won}>
            <span class="track"><span class="fill" use:css={{ width: pct(paidOut ? (r.pays ?? 0) : r.price) }}></span></span>
            {#if paidOut}<span class="mark" use:css={{ left: pct(r.price) }} title={t('%1 when trading closed', chance(r.price))}></span>{/if}
          </span>
        </li>
      {/each}
    </ul>
    {#if paidOut && view.tie}
      <span class="note"><span class="mark-key" aria-hidden="true"></span>{t('A tie: every share paid half a coin')}</span>
    {:else if paidOut && view.winner}
      <span class="note"><span class="mark-key" aria-hidden="true"></span>{#if view.favourite === view.winner.state}{t('Last price at close: the market called it')}{:else if fav}{t('Last price at close: the market had %1', words(fav.label))}{:else}{t('Last price at close')}{/if}</span>
    {:else if view.stage !== 'trading' && view.stage !== 'paid'}
      <span class="note">{t('Last prices before trading closed')}</span>
    {/if}
  {/if}
</div>

<style>
  .outcomes {
    display: flex;
    flex-direction: column;
    gap: 0.35rem;
    min-width: 12rem;
  }
  ul {
    list-style: none;
    margin: 0;
    padding: 0;
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
  }
  li {
    display: grid;
    grid-template-columns: minmax(0, 1fr) auto;
    gap: 0.2rem 0.6rem;
    align-items: center;
  }
  /* The colour of a state, as Hivemind's pages give it: No in the colour of a loss, Yes in the accent. */
  .c0 {
    --c: var(--negative);
  }
  .c1 {
    --c: var(--accent);
  }
  .c2 {
    --c: var(--warning);
  }
  .c3 {
    --c: var(--link);
  }
  .c4 {
    --c: var(--text-muted);
  }
  .c5 {
    --c: var(--accent-strong);
  }
  .c6 {
    --c: var(--positive);
  }
  .c7 {
    --c: var(--line-strong);
  }
  .name {
    display: flex;
    align-items: center;
    gap: 0.4rem;
    min-width: 0;
  }
  .cut {
    min-width: 0;
    overflow: hidden;
    white-space: nowrap;
    text-overflow: ellipsis;
  }
  .plus {
    color: var(--text-faint);
    white-space: pre;
  }
  .dot {
    flex: none;
    width: 0.6rem;
    height: 0.6rem;
    border-radius: 50%;
    background: var(--c);
  }
  .check {
    display: inline-grid;
    place-items: center;
    flex: none;
    width: 0.95rem;
    height: 0.95rem;
    border-radius: 50%;
    background: var(--positive);
    color: var(--surface);
  }
  .chip {
    padding: 0.02rem 0.45rem;
    border-radius: 999px;
    font-size: 0.72rem;
    font-weight: 650;
    letter-spacing: 0.02em;
    white-space: nowrap;
    background: var(--accent-soft);
    color: var(--text);
  }
  .chip.tie {
    background: var(--raised);
  }
  .pct {
    font-weight: 700;
  }
  .lost,
  .out {
    color: var(--text-muted);
  }
  .lost .dot,
  .out .dot {
    opacity: 0.45;
  }
  .lost .pct {
    font-weight: 600;
  }
  .bar {
    position: relative;
    grid-column: 1 / -1;
    display: block;
  }
  .track {
    position: relative;
    display: block;
    height: 0.4rem;
    border-radius: 999px;
    background: var(--raised);
    overflow: hidden;
  }
  .fill {
    position: absolute;
    inset: 0 auto 0 0;
    border-radius: 999px;
    background: var(--c);
  }
  .bar.won .fill,
  .fill.won {
    background: var(--positive);
  }
  .lost .fill,
  .out .fill {
    opacity: 0.4;
  }
  /* Where the last price before trading closed stood: a thin faint line, ringed in the surface (on the fill and the track alike). */
  .mark {
    position: absolute;
    top: -0.2rem;
    bottom: -0.2rem;
    width: 2px;
    margin-left: -1px;
    border-radius: 1px;
    background: var(--text-muted);
    box-shadow: 0 0 0 1px var(--surface);
  }
  .mark-key {
    display: inline-block;
    width: 2px;
    height: 0.8rem;
    margin-right: 0.35rem;
    border-radius: 1px;
    background: var(--text-muted);
    vertical-align: -0.1rem;
  }
  .note {
    color: var(--text-faint);
    font-size: 0.78rem;
  }
  .small {
    font-size: 0.8rem;
  }
  .scale {
    display: flex;
    flex-direction: column;
    gap: 0.3rem;
  }
  .answer {
    display: inline-flex;
    align-items: center;
    gap: 0.4rem;
    font-weight: 650;
  }
  .scale .track {
    overflow: visible;
    margin: 0.25rem 0;
  }
  .scale .fill {
    opacity: 0.45;
  }
  .scale .dot {
    position: absolute;
    top: 50%;
    width: 0.85rem;
    height: 0.85rem;
    margin: -0.425rem 0 0 -0.425rem;
    box-sizing: border-box;
  }
  .dot.val {
    background: var(--positive);
    border: 2px solid var(--surface);
    box-shadow: 0 0 0 1px var(--positive);
  }
  .dot.est {
    width: 0.72rem;
    height: 0.72rem;
    margin: -0.36rem 0 0 -0.36rem;
    background: var(--surface);
    border: 2px solid var(--text-faint);
  }
  .ends {
    display: flex;
    justify-content: space-between;
    color: var(--text-faint);
    font-size: 0.75rem;
  }
</style>
