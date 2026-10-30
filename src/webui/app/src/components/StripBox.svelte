<script lang="ts">
  // One block of a chain's strip in the block explorer (doc/unified-app.md §21.4): a box of the explorer's ribbon
  // (components/BlockBox.svelte), smaller — a lid in the chain's colour and a side give it its depth (two skewed faces,
  // still: nothing moves), its face says its height, its transactions, the sidechains it merged mined (a block of
  // Chains) and how long ago. `pending`: the block being made, an outline, with what waits for it.
  import { t, tn } from '../lib/i18n.svelte';
  import ChainBadge from './ChainBadge.svelte';
  import { formatCount, niceTimeOffset } from '../lib/format';
  import { chainColor } from '../lib/chaincolors';
  import { HOST, slotLabel } from '../lib/chains.svelte';
  import { hub } from '../lib/sidechains.svelte';
  import { css } from '../lib/actions';
  import type { StripBlock } from '../lib/strips.svelte';

  let {
    chain,
    block = null,
    pending = false,
    next = 0,
    waiting = 0,
    now = 0,
    selected = false,
    large = false,
    onselect,
  }: {
    chain: string;
    block?: StripBlock | null;
    pending?: boolean;
    /** The height of the block being made (pending). */
    next?: number;
    /** Transactions waiting for it (pending). */
    waiting?: number;
    now?: number;
    selected?: boolean;
    large?: boolean;
    onselect: () => void;
  } = $props();

  const color = $derived(chain === HOST ? 'var(--accent)' : chainColor(chain) || 'var(--accent)');
  /** The sidechains a block of Chains merged mined, by their mark when the app knows them. */
  const bmm = $derived(
    (block?.bmm ?? []).map((slot) => {
      const c = hub.list?.chains.find((x) => x.slot === slot);
      return { slot, id: c?.stem ?? '', name: c?.name ?? slotLabel(slot) };
    }),
  );
  const ago = $derived(block ? niceTimeOffset(Math.max(0, now - block.time)) : '');
  const label = $derived(
    pending
      ? t('Block %1, being made: %2 waiting', formatCount(next), formatCount(waiting))
      : block
        ? `${t('Block %1', formatCount(block.height))}: ${tn('%n transaction(s)', block.txs)}, ${t('%1 ago', ago)}` + (bmm.length ? `, ${t('merged mined: %1', bmm.map((b) => b.name || t('slot %1', String(b.slot))).join(', '))}` : '')
        : '',
  );
</script>

<button
  type="button"
  class="sbox"
  class:pending
  class:large
  class:is-open={selected}
  aria-pressed={pending ? undefined : selected}
  aria-label={label}
  title={label}
  data-strip-block={pending ? 'next' : block?.hash}
  use:css={{ '--lid': color }}
  onclick={onselect}
>
  <span class="lid" aria-hidden="true"></span>
  <span class="side" aria-hidden="true"></span>
  <span class="face">
    {#if pending}
      <span class="height mono faint">{formatCount(next)}</span>
      <span class="what">{t('Next block')}</span>
      <span class="foot faint">{tn('%n waiting', waiting)}</span>
    {:else if block}
      <span class="height mono">{formatCount(block.height)}</span>
      <span class="txs">{tn('%n tx', block.txs)}</span>
      <span class="marks" aria-hidden="true">
        {#each bmm.slice(0, 5) as b (b.slot)}
          {#if b.id}<ChainBadge id={b.id} name={b.name} small markOnly />{:else}<span class="badge">{b.slot}</span>{/if}
        {/each}
      </span>
      <!-- A chain's first block: its makers wrote any time in it (doc §20.3). -->
      <span class="foot faint">{block.height === 0 ? t('genesis') : t('%1 ago', ago)}</span>
    {/if}
  </span>
</button>

<style>
  /* The box of components/BlockBox.svelte, smaller: a lid skewed about its bottom edge, a side about its left edge. */
  .sbox {
    --depth: 9px;
    position: relative;
    flex: none;
    width: 7.4rem;
    margin: var(--depth) var(--depth) 0 0;
    padding: 0;
    border: 0;
    background: none;
    color: inherit;
    font: inherit;
    text-align: left;
    cursor: pointer;
  }
  .sbox.large {
    --depth: 12px;
    width: 9.4rem;
  }
  .sbox:focus-visible {
    outline: none;
  }
  .sbox:focus-visible .face {
    outline: 2px solid var(--focus);
    outline-offset: 2px;
  }
  .face {
    position: relative;
    z-index: 2;
    display: flex;
    flex-direction: column;
    gap: 0.2rem;
    min-height: 6.2rem;
    padding: 0.55rem 0.6rem 0.45rem;
    border: 1px solid var(--line-strong);
    border-radius: 3px;
    background: linear-gradient(180deg, var(--surface), var(--surface-2));
  }
  .large .face {
    min-height: 7.6rem;
    padding: 0.7rem 0.7rem 0.55rem;
  }
  .lid {
    position: absolute;
    left: 0;
    top: calc(var(--depth) * -1);
    width: 100%;
    height: var(--depth);
    transform: skewX(-45deg);
    transform-origin: bottom left;
    background: linear-gradient(180deg, var(--lid), color-mix(in srgb, var(--lid) 60%, var(--surface)));
    border: 1px solid color-mix(in srgb, var(--lid) 70%, var(--line-strong));
    border-bottom: 0;
  }
  .side {
    position: absolute;
    right: calc(var(--depth) * -1);
    top: 0;
    width: var(--depth);
    height: 100%;
    transform: skewY(-45deg);
    transform-origin: top left;
    background: linear-gradient(100deg, color-mix(in srgb, var(--line-strong) 70%, var(--bg)), var(--bg));
    border: 1px solid var(--line-strong);
    border-left: 0;
  }
  /* The block being made: an outline, no fill. */
  .pending .face {
    border-style: dashed;
    background: transparent;
  }
  .pending .lid,
  .pending .side {
    opacity: 0.45;
    border-style: dashed;
  }
  .sbox:hover .face {
    border-color: var(--text-faint);
  }
  .sbox.is-open .face {
    border-color: var(--accent-fg);
    box-shadow: 0 0 0 1px var(--accent-fg);
  }
  .sbox.is-open .lid,
  .sbox.is-open .side {
    border-color: var(--accent-fg);
  }
  .height {
    font-size: 0.98rem;
    font-weight: 650;
    color: var(--text);
  }
  .large .height {
    font-size: 1.1rem;
  }
  .txs,
  .what {
    font-size: 0.8rem;
    color: var(--text-muted);
    white-space: nowrap;
  }
  .marks {
    display: flex;
    flex-wrap: wrap;
    gap: 0.15rem;
    min-height: 1.2rem;
  }
  .marks .badge {
    padding: 0 0.3rem;
    font-size: 0.66rem;
  }
  .foot {
    margin-top: auto;
    padding-top: 0.3rem;
    border-top: 1px solid var(--line);
    font-size: 0.74rem;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
</style>
