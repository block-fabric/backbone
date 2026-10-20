<script lang="ts">
  // The latest blocks of every chain shown (doc/unified-app.md §19: Home, Overview), or their mempools (mode
  // "mempool": the transactions waiting there): each chain a column of its own rows, its mark and name at the head;
  // this app's own given by the page (its explorer a click away), a sidechain's from its node (chain.activity).
  import { t } from '../lib/i18n.svelte';
  import ChainBadge from './ChainBadge.svelte';
  import ModuleAmount from './ModuleAmount.svelte';
  import SafeText from './SafeText.svelte';
  import { formatCount, formatTime, niceTimeOffset, shortHash } from '../lib/format';
  import { chainLabel, HOST } from '../lib/chains.svelte';
  import { many, poll, refreshActivity } from '../lib/manychains.svelte';
  import { productName } from '../lib/modules.svelte';
  import { go } from '../lib/nav.svelte';

  interface Block {
    height: number;
    hash: string;
    time: number;
    txs: number;
  }
  interface Waiting {
    txid: string;
    time: number;
    /** In coins, as text. */
    fee: string;
    vsize: number;
  }

  let {
    chains,
    mode = 'blocks',
    hostBlocks = [],
    hostMempool = [],
    hostWaiting = 0,
    now,
    count = 5,
  }: { chains: string[]; mode?: 'blocks' | 'mempool'; hostBlocks?: Block[]; hostMempool?: Waiting[]; hostWaiting?: number; now: number; count?: number } = $props();

  const sides = $derived(chains.filter((c) => c !== HOST));
  $effect(() => {
    const list = sides;
    return poll(() => list.forEach((s) => void refreshActivity(s)), 6000);
  });

  const columns = $derived(
    chains.map((id) => {
      if (id === HOST) return { id, name: productName(), blocks: hostBlocks.slice(0, count), waiting: hostMempool.slice(0, count), size: hostWaiting, error: '' };
      const a = many.activity[id];
      return { id, name: chainLabel(id), blocks: (a?.blocks ?? []).slice(0, count), waiting: (a?.mempool.rows ?? []).slice(0, count), size: a?.mempool.size ?? 0, error: many.errors[`activity:${id}`] ?? '' };
    }),
  );
</script>

<div class="columns" data-many-blocks={mode}>
  {#each columns as c (c.id)}
    <div class="column" data-chain-column={c.id}>
      <div class="col-head">
        <ChainBadge id={c.id} name={c.name} small />
        {#if mode === 'mempool'}<span class="muted small">{t('%1 waiting', formatCount(c.size))}</span>{:else if c.blocks[0]}<span class="muted small num">{formatCount(c.blocks[0].height)}</span>{/if}
      </div>
      {#if c.error && !c.blocks.length}<p class="error small"><SafeText text={c.error} /></p>{/if}
      <ol>
        {#if mode === 'blocks'}
          {#each c.blocks as b (b.hash)}
            <li>
              <button type="button" class="row" disabled={c.id !== HOST} title={c.id === HOST ? t('Show block %1 in the block explorer', String(b.height)) : formatTime(b.time)} onclick={() => go('explorer', `search:${b.hash}`)}>
                <span class="num strong">{formatCount(b.height)}</span>
                <span class="mono muted hash">{shortHash(b.hash, 5)}</span>
                <span class="num muted">{b.txs} tx</span>
                <time class="muted" datetime={new Date(b.time * 1000).toISOString()}>{niceTimeOffset(Math.max(0, now - b.time))}</time>
              </button>
            </li>
          {:else}
            <li class="muted small empty">{t('Waiting for its node…')}</li>
          {/each}
        {:else}
          {#each c.waiting as w (w.txid)}
            <li>
              <button type="button" class="row" disabled={c.id !== HOST} title={c.id === HOST ? t('Show the transaction in the block explorer') : w.txid} onclick={() => go('explorer', `search:${w.txid}`)}>
                <span class="mono hash">{shortHash(w.txid, 5)}</span>
                <span class="num muted">{formatCount(w.vsize)} vB</span>
                <span class="num muted"><ModuleAmount text={w.fee} coins /></span>
                <time class="muted" datetime={new Date(w.time * 1000).toISOString()}>{niceTimeOffset(Math.max(0, now - w.time))}</time>
              </button>
            </li>
          {:else}
            <li class="muted small empty">{t('Nothing waiting to be mined.')}</li>
          {/each}
        {/if}
      </ol>
    </div>
  {/each}
</div>

<style>
  .columns {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(15rem, 1fr));
    gap: 0.9rem;
  }
  .column {
    min-width: 0;
  }
  .col-head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 0.5rem;
    padding-bottom: 0.35rem;
    border-bottom: 1px solid var(--line);
  }
  ol {
    margin: 0;
    padding: 0;
    list-style: none;
  }
  li {
    border-bottom: 1px solid var(--line);
  }
  li:last-child {
    border-bottom: 0;
  }
  .row {
    display: grid;
    grid-template-columns: 4.2rem minmax(0, 1fr) auto auto;
    gap: 0.5rem;
    align-items: center;
    width: 100%;
    padding: 0.4rem 0.15rem;
    border: 0;
    background: none;
    color: inherit;
    font: inherit;
    font-size: 0.88rem;
    text-align: left;
    cursor: pointer;
  }
  .row:disabled {
    cursor: default;
  }
  .row:hover:not(:disabled) {
    background: var(--raised);
  }
  [data-many-blocks='mempool'] .row {
    grid-template-columns: minmax(0, 1fr) auto auto auto;
  }
  .hash {
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .strong {
    font-weight: 650;
  }
  time {
    text-align: right;
    font-size: 0.82rem;
  }
  .small {
    font-size: 0.82rem;
  }
  .empty {
    padding: 0.5rem 0;
  }
</style>
