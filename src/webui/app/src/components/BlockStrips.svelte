<script lang="ts">
  // The block explorer's main page (doc/unified-app.md §21.4; user, 2026-10-21: "the block representation of all active
  // sidechains on the block explorer main page rather than the latest blocks table … when we click on any block its
  // description would open below"): a strip of boxes for each chain the top selector shows (this app's own first), its
  // newest blocks newest first after the one being made; a box clicked opens that block under the strips — a block of
  // Chains as its explorer says it (facts, coinbase, drivechain messages, transactions), a sidechain's facts and
  // transactions from its node (its own explorer a click away, for what the chain says of it). ← and → go from a box to
  // the next; with a block open they open it.
  //
  // Light on the processor: a strip is read again in parts (lib/strips.svelte.ts: only the new blocks, at the tip of
  // Chains, every few seconds for a sidechain while shown) and a box keeps its place (keyed by its hash): a new block is
  // one box more at the head; the boxes are still (no animation), their ages follow at most every 30 s.
  import { untrack } from 'svelte';
  import { t } from '../lib/i18n.svelte';
  import ChainBadge from './ChainBadge.svelte';
  import CopyButton from './CopyButton.svelte';
  import Icon from './Icon.svelte';
  import StripBox from './StripBox.svelte';
  import ChainsBlockDetail from './ChainsBlockDetail.svelte';
  import SideBlockDetail from './SideBlockDetail.svelte';
  import { formatCount } from '../lib/format';
  import { node } from '../lib/state.svelte';
  import { every } from '../lib/poll';
  import { go } from '../lib/nav.svelte';
  import { chainLabel, HOST, setSelectedChain } from '../lib/chains.svelte';
  import { productName } from '../lib/modules.svelte';
  import { refreshHostStrip, refreshSideStrip, strips, type StripBlock } from '../lib/strips.svelte';

  let { chains, large = false }: { chains: string[]; large?: boolean } = $props();

  const sides = $derived(chains.filter((c) => c !== HOST));
  const nameOf = (id: string) => (id === HOST ? productName() : chainLabel(id));

  // This app's own: at its tip (taken at most once a second, lib/state.svelte.ts).
  $effect(() => {
    if (!chains.includes(HOST)) return;
    const height = node.tip?.height;
    // What it reads of the strip is not followed (its answer would read it again).
    if (height !== undefined) untrack(() => void refreshHostStrip(height));
  });
  // A sidechain's: now, then every few seconds while the page is seen.
  $effect(() => {
    const list = sides;
    return untrack(() => every(8000, () => list.forEach((s) => void refreshSideStrip(s)), { now: true }));
  });

  let now = $state(Math.floor(Date.now() / 1000));
  $effect(() => every(30000, () => (now = Math.floor(Date.now() / 1000))));

  /** The block open under the strips. */
  let open = $state<{ chain: string; hash: string } | null>(null);
  const openBlock = $derived(open ? ((strips.blocks[open.chain] ?? []).find((b) => b.hash === open!.hash) ?? null) : null);
  // A chain no longer shown: its block closes.
  $effect(() => {
    if (open && !chains.includes(open.chain)) open = null;
  });

  function select(chain: string, b: StripBlock) {
    open = open?.chain === chain && open.hash === b.hash ? null : { chain, hash: b.hash };
  }
  function selectHash(chain: string, hash: string) {
    open = { chain, hash };
  }
  /** The block being made: the waiting transactions, in that chain's explorer. */
  function openWaiting(chain: string) {
    setSelectedChain(chain);
    go('explorer', chain === HOST ? 'mempool' : '');
  }
  /** That block in its chain's explorer (all of it: its page there). */
  function inExplorer(chain: string, hash: string) {
    setSelectedChain(chain);
    go('explorer', chain === HOST ? `block:${hash}` : `search:${hash}`);
  }

  /** ← and →: the box before or after; with a block of this strip open, that one opened. */
  function keys(e: KeyboardEvent, chain: string) {
    if (e.key !== 'ArrowRight' && e.key !== 'ArrowLeft') return;
    const boxes = [...(e.currentTarget as HTMLElement).querySelectorAll<HTMLButtonElement>('button[data-strip-block]')];
    const i = boxes.indexOf(document.activeElement as HTMLButtonElement);
    if (i < 0) return;
    e.preventDefault();
    const to = boxes[Math.max(0, Math.min(boxes.length - 1, i + (e.key === 'ArrowRight' ? 1 : -1)))];
    to.focus();
    to.scrollIntoView?.({ block: 'nearest', inline: 'nearest' });
    const hash = to.dataset.stripBlock;
    if (open?.chain === chain && hash && hash !== 'next') selectHash(chain, hash);
  }
  /** ← and → in the block open, as in its strip: the box at its left (newer, -1) or at its right (older, +1). */
  function step(delta: 1 | -1) {
    if (!open) return;
    const list = strips.blocks[open.chain] ?? [];
    const i = list.findIndex((b) => b.hash === open!.hash);
    const to = i >= 0 ? list[i + delta] : undefined;
    if (to) open = { chain: open.chain, hash: to.hash };
  }
  function detailKeys(e: KeyboardEvent) {
    if (e.target instanceof HTMLInputElement || e.target instanceof HTMLTextAreaElement) return;
    if (e.key === 'ArrowLeft') step(-1);
    else if (e.key === 'ArrowRight') step(1);
    else if (e.key === 'Escape') open = null;
  }
  const exploreTx = (txid: string) => {
    setSelectedChain(HOST);
    go('explorer', `tx:${txid}`);
  };
  const exploreAddress = (address: string) => {
    setSelectedChain(HOST);
    go('explorer', `address:${address}`);
  };
</script>

<section class="panel strips" class:large aria-labelledby="strips-title" data-strips={chains.join(',')}>
  <div class="panel-head">
    <h2 id="strips-title">{chains.length > 1 ? t('Blocks of each chain') : t('Blocks')}</h2>
    <span class="muted small">{t('Newest first. Click a block to see it below.')}</span>
  </div>
  <div class="panel-body rows">
  {#each chains as id (id)}
    {@const list = strips.blocks[id] ?? []}
    <div class="row" data-strip={id}>
      <div class="label">
        <ChainBadge {id} name={nameOf(id)} />
        <span class="faint mono">{list[0] ? formatCount(list[0].height) : '…'}</span>
        {#if strips.errors[id] && !list.length}<span class="error small">{t('Not read')}</span>{/if}
      </div>
      <!-- svelte-ignore a11y_no_noninteractive_element_interactions (← and → between the boxes; each box is a button) -->
      <div class="strip" role="list" aria-label={t('Blocks of %1', nameOf(id))} onkeydown={(e) => keys(e, id)}>
        <div class="item" role="listitem">
          <StripBox chain={id} pending next={(list[0]?.height ?? 0) + 1} waiting={id === HOST ? node.mempool.size : (strips.waiting[id] ?? 0)} {large} onselect={() => openWaiting(id)} />
        </div>
        {#each list as b (b.hash)}
          <div class="item" role="listitem">
            <StripBox chain={id} block={b} {now} {large} selected={open?.chain === id && open.hash === b.hash} onselect={() => select(id, b)} />
          </div>
        {/each}
      </div>
    </div>
  {/each}
  </div>
</section>

{#if open}
  {@const o = open}
  <!-- The block open: its panel (and, for a block of Chains, the panels of its coinbase and drivechain messages after it,
       as in Chains' explorer). ← → and Escape in it. -->
  <!-- svelte-ignore a11y_no_noninteractive_element_interactions (← → and Escape in the block open) -->
  <div class="detail" role="region" aria-labelledby="strip-detail-title" data-strip-detail={`${o.chain}:${o.hash}`} onkeydown={detailKeys} tabindex="-1">
    {#snippet head()}
      <h2 id="strip-detail-title">
        <ChainBadge id={o.chain} name={nameOf(o.chain)} small />
        <span>{openBlock ? t('Block %1', formatCount(openBlock.height)) : t('Block')}</span>
      </h2>
      <span class="grow"></span>
      <button type="button" class="btn small ghost" onclick={() => step(-1)} title={t('The newer block (←)')} aria-label={t('The newer block')} data-strip-newer><Icon name="chevron-left" size={15} /></button>
      <button type="button" class="btn small ghost" onclick={() => step(1)} title={t('The older block (→)')} aria-label={t('The older block')} data-strip-older><Icon name="chevron-right" size={15} /></button>
      <CopyButton text={o.hash} label={t('Copy the block hash')} />
      <button type="button" class="btn small" onclick={() => inExplorer(o.chain, o.hash)}>{t('Open in its explorer')}</button>
      <button type="button" class="btn small ghost" onclick={() => (open = null)} aria-label={t('Close')} title={t('Close (Esc)')} data-strip-close>✕</button>
    {/snippet}
    {#if o.chain === HOST}
      <ChainsBlockDetail hash={o.hash} {head} ontx={exploreTx} onaddress={exploreAddress} onblock={(h) => selectHash(HOST, h)} />
    {:else}
      <SideBlockDetail stem={o.chain} name={nameOf(o.chain)} hash={o.hash} {head} ontx={(txid) => inExplorer(o.chain, txid)} onblock={(h) => selectHash(o.chain, h)} onexplore={() => inExplorer(o.chain, o.hash)} />
    {/if}
  </div>
{/if}

<style>
  .rows {
    display: flex;
    flex-direction: column;
    gap: 0.3rem;
  }
  .strips .panel-head {
    gap: 0.8rem;
  }
  .row {
    display: grid;
    grid-template-columns: 9.5rem minmax(0, 1fr);
    align-items: center;
    gap: 0.6rem;
    padding: 0.35rem 0;
    border-bottom: 1px solid var(--line);
  }
  .row:last-child {
    border-bottom: 0;
  }
  .label {
    display: flex;
    flex-direction: column;
    gap: 0.2rem;
    min-width: 0;
  }
  .strip {
    display: flex;
    overflow-x: auto;
    overflow-y: hidden;
    padding: 0.2rem 0 0.5rem;
  }
  .item {
    display: flex;
  }
  .large .row {
    grid-template-columns: 11rem minmax(0, 1fr);
  }
  .detail {
    display: flex;
    flex-direction: column;
    gap: 1rem;
    outline: none;
  }
  .detail :global(.panel-head) {
    display: flex;
    align-items: center;
    gap: 0.4rem;
    flex-wrap: wrap;
  }
  .detail :global(.panel-head h2) {
    display: inline-flex;
    align-items: center;
    gap: 0.5rem;
    margin: 0;
  }
  .panel-head {
    display: flex;
    align-items: center;
    gap: 0.4rem;
    flex-wrap: wrap;
  }
  .panel-head h2 {
    display: inline-flex;
    align-items: center;
    gap: 0.5rem;
    margin: 0;
  }
  .grow {
    flex: 1;
  }
  .small {
    font-size: 0.8rem;
  }
</style>
