<script lang="ts">
  // The latest blocks as a chain of boxes, newest at the left, after the outline of the block being mined
  // (the next height, what waits for it in the mempool, what it would pay, when it is expected). The strip
  // scrolls sideways on its own; the arrows move between the boxes. After the ribbon of the user's pool
  // site (epool.cash): components/BlockBox.svelte, lib/blocks.ts.
  import { t } from '../lib/i18n.svelte';
  import Amount from './Amount.svelte';
  import BlockBox from './BlockBox.svelte';
  import { intervalOf, medianInterval, shortInterval, type NextBlock, type RibbonBlock } from '../lib/blocks';
  import { formatCount, shortDuration } from '../lib/format';


  let {
    blocks,
    next,
    selected,
    fresh,
    now,
    onselect,
    onmempool,
  }: {
    blocks: readonly RibbonBlock[];
    next: NextBlock | null;
    selected: string | null;
    /** The block that came while the page is shown (it slides in once). */
    fresh: string | null;
    now: number;
    onselect: (hash: string) => void;
    onmempool: () => void;
  } = $props();

  const median = $derived(medianInterval(blocks));

  function keys(e: KeyboardEvent) {
    if (e.key !== 'ArrowRight' && e.key !== 'ArrowLeft') return;
    const boxes = [...(e.currentTarget as HTMLElement).querySelectorAll<HTMLElement>('button.blk')];
    const i = boxes.indexOf(document.activeElement as HTMLElement);
    if (i < 0) return;
    e.preventDefault();
    const to = boxes[Math.max(0, Math.min(boxes.length - 1, i + (e.key === 'ArrowRight' ? 1 : -1)))];
    to.focus();
    to.scrollIntoView?.({ block: 'nearest', inline: 'nearest' });
  }
</script>

<section class="ribbon" aria-label="Latest blocks">
  <div class="ribbon-head">
    <h2 class="caps">{t('Latest blocks')}</h2>
    <span class="faint mono">{median !== null ? `${t('median')} ${shortInterval(median)} ${t('between blocks')} · ${t('target')} 1m` : t('newest first')}</span>
  </div>
  <!-- svelte-ignore a11y_no_noninteractive_element_interactions (the arrows between the boxes; each box is a button) -->
  <div class="strip" role="list" onkeydown={keys}>
    {#if next}
      <div class="next-wrap" role="listitem">
        <button type="button" class="blk-next" onclick={onmempool} title={t('The block being mined: the transactions waiting for it')}>
          <span class="lid" aria-hidden="true"></span>
          <span class="side" aria-hidden="true"></span>
          <span class="face">
            <span class="caps next-word"><span class="dot" aria-hidden="true"></span>{t('Next')}</span>
            <span class="height mono">{formatCount(next.height)}</span>
            <span class="reward">{#if next.reward !== null}~<Amount sats={next.reward} masked={false} />{:else}–{/if}</span>
            <span class="faint small">{formatCount(next.txs)} {t('waiting')} · {next.vbytes < 1000 ? `${formatCount(next.vbytes)} vB` : `${formatCount(Math.round(next.vbytes / 1000))} kvB`}</span>
            {#if next.fees > 0}<span class="faint small">{t('fees')} <Amount sats={next.fees} masked={false} /></span>{/if}
            <span class="foot faint mono">{next.eta !== null ? `≈ ${shortDuration(next.eta)}` : `${t('target')} 1m`}</span>
          </span>
        </button>
      </div>
    {/if}
    {#each blocks as b, i (b.hash)}
      <div class="item" role="listitem">
        {#if i > 0 || next}<span class="link" aria-hidden="true"><i></i></span>{/if}
        <BlockBox block={b} secs={intervalOf(blocks, i)} {median} {now} selected={selected === b.hash} fresh={fresh === b.hash} {onselect} />
      </div>
    {/each}
  </div>
</section>

<style>
  .ribbon {
    margin: 0.9rem 0 1rem;
  }
  .ribbon-head {
    display: flex;
    align-items: baseline;
    justify-content: space-between;
    gap: 0.8rem;
    margin-bottom: 0.3rem;
  }
  .ribbon-head h2 {
    margin: 0;
    font-size: 0.78rem;
  }
  .ribbon-head span {
    font-size: 0.8rem;
  }
  /* Room above for the lids, at the right for the last side; the page never scrolls sideways. */
  .strip {
    display: flex;
    max-width: 100%;
    overflow-x: auto;
    overflow-y: hidden;
    padding: 2px 14px 12px 2px;
  }
  .item,
  .next-wrap {
    display: flex;
    align-items: stretch;
    flex: none;
  }
  .link {
    display: flex;
    align-items: center;
    flex: none;
    width: 1.1rem;
    margin-top: 12px;
  }
  .link i {
    display: block;
    width: 100%;
    height: 2px;
    background: var(--chain-link);
  }
  /* The block being mined: the same box in outline (it is not one yet). Still: no endless animation. */
  .blk-next {
    --blk-depth: 12px;
    position: relative;
    flex: none;
    width: 9.5rem;
    margin: var(--blk-depth) var(--blk-depth) 0 0;
    padding: 0;
    border: 0;
    background: none;
    color: inherit;
    font: inherit;
    text-align: left;
    cursor: pointer;
  }
  .blk-next:focus-visible {
    outline: none;
  }
  .blk-next:focus-visible .face {
    outline: 2px solid var(--focus);
    outline-offset: 2px;
  }
  .face {
    position: relative;
    z-index: 2;
    display: flex;
    flex-direction: column;
    gap: 0.35rem;
    height: 100%;
    min-height: 9.6rem;
    padding: 0.7rem 0.7rem 0.55rem;
    border: 1px dashed var(--accent-fg);
    border-radius: 3px;
    background: color-mix(in srgb, var(--accent) 6%, var(--surface));
  }
  .lid {
    position: absolute;
    left: 0;
    top: calc(var(--blk-depth) * -1);
    width: 100%;
    height: var(--blk-depth);
    transform: skewX(-45deg);
    transform-origin: bottom left;
    background: repeating-linear-gradient(90deg, color-mix(in srgb, var(--accent) 45%, transparent) 0 5px, transparent 5px 10px);
    border: 1px dashed var(--accent-fg);
    border-bottom: 0;
  }
  .side {
    position: absolute;
    right: calc(var(--blk-depth) * -1);
    top: 0;
    width: var(--blk-depth);
    height: 100%;
    transform: skewY(-45deg);
    transform-origin: top left;
    border: 1px dashed var(--accent-fg);
    border-left: 0;
  }
  .blk-next:hover .face {
    background: color-mix(in srgb, var(--accent) 12%, var(--surface));
  }
  .next-word {
    display: flex;
    align-items: center;
    gap: 0.35rem;
    color: var(--accent-fg);
    font-size: 0.7rem;
  }
  .dot {
    width: 0.4rem;
    height: 0.4rem;
    border-radius: 50%;
    background: var(--accent);
  }
  .height {
    font-size: 1.05rem;
    font-weight: 650;
  }
  .reward {
    font-size: 0.9rem;
  }
  .small {
    font-size: 0.76rem;
  }
  .foot {
    margin-top: auto;
    padding-top: 0.4rem;
    border-top: 1px dashed var(--line-strong);
    font-size: 0.76rem;
  }
</style>
