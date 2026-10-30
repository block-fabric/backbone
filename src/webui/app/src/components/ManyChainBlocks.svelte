<script lang="ts">
  // The latest blocks of the chains shown (doc/unified-app.md §20.2: Home, Overview), or their waiting transactions
  // (mode "mempool"), as ONE table: the chain, its height (or the transaction), how many transactions (or its size and
  // fee), how long ago. With several chains each chain's newest come first so that every chain shows, then the rest by
  // time; a chain's first block says "genesis" (its makers wrote any time in it). A row opens it in the block explorer
  // on its chain. This app's own given by the page, a sidechain's from its node (chain.activity).
  import { t } from '../lib/i18n.svelte';
  import ChainBadge from './ChainBadge.svelte';
  import ModuleAmount from './ModuleAmount.svelte';
  import SafeText from './SafeText.svelte';
  import { ageText, formatCount, shortHash } from '../lib/format';
  import { chainLabel, HOST, setSelectedChain } from '../lib/chains.svelte';
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
    count = 8,
    perChain = 2,
  }: { chains: string[]; mode?: 'blocks' | 'mempool'; hostBlocks?: Block[]; hostMempool?: Waiting[]; hostWaiting?: number; now: number; count?: number; perChain?: number } = $props();

  const sides = $derived(chains.filter((c) => c !== HOST));
  $effect(() => {
    const list = sides;
    return poll(() => list.forEach((s) => void refreshActivity(s)), 8000);
  });
  const several = $derived(chains.length > 1);
  const nameOf = (id: string) => (id === HOST ? productName() : chainLabel(id));

  /** Each chain's newest first (up to perChain of each when several), then the others by time, `count` at most. */
  function pick<T extends { time: number }>(lists: { chain: string; rows: T[] }[]): { chain: string; row: T }[] {
    if (!several) return (lists[0]?.rows ?? []).slice(0, count).map((row) => ({ chain: lists[0].chain, row }));
    const first = lists.flatMap((l) => l.rows.slice(0, perChain).map((row) => ({ chain: l.chain, row })));
    const rest = lists.flatMap((l) => l.rows.slice(perChain).map((row) => ({ chain: l.chain, row }))).sort((a, b) => b.row.time - a.row.time);
    return [...first, ...rest].slice(0, Math.max(count, first.length));
  }

  const blockRows = $derived(
    pick(chains.map((id) => ({ chain: id, rows: id === HOST ? hostBlocks : (many.activity[id]?.blocks ?? []) }))),
  );
  const waitingRows = $derived(
    pick(chains.map((id) => ({ chain: id, rows: id === HOST ? hostMempool : (many.activity[id]?.mempool.rows ?? []) }))).sort((a, b) => b.row.time - a.row.time),
  );
  const waitingTotal = $derived(chains.reduce((n, id) => n + (id === HOST ? hostWaiting : (many.activity[id]?.mempool.size ?? 0)), 0));
  const missing = $derived(sides.filter((s) => !many.activity[s] && many.errors[`activity:${s}`]));

  function openBlock(chain: string, hash: string) {
    setSelectedChain(chain);
    go('explorer', chain === HOST ? `block:${hash}` : `search:${hash}`);
  }
  function openTx(chain: string, txid: string) {
    setSelectedChain(chain);
    go('explorer', chain === HOST ? `tx:${txid}` : `search:${txid}`);
  }
</script>

<div class="fit">
<table class="rows chain-rows" data-many-blocks={mode}>
  {#if mode === 'blocks'}
    <thead>
      <tr>{#if several}<th>{t('Chain')}</th>{/if}<th class="end">{t('Height')}</th><th class="end">{t('Transactions')}</th><th class="hash-col">{t('Hash')}</th><th class="end">{t('Age')}</th></tr>
    </thead>
    <tbody>
      {#each blockRows as { chain, row: b } (chain + b.hash)}
        <tr class="clickable" onclick={() => openBlock(chain, b.hash)} title={t('Open block %1 in the block explorer', String(b.height))} data-block-row={chain}>
          {#if several}<td><ChainBadge id={chain} name={nameOf(chain)} small /></td>{/if}
          <td class="end num strong"><button type="button" class="link" onclick={(e) => { e.stopPropagation(); openBlock(chain, b.hash); }}>{formatCount(b.height)}</button></td>
          <td class="end num">{formatCount(b.txs)}</td>
          <td class="mono hash-col faint" translate="no">{shortHash(b.hash, 6)}</td>
          <td class="end muted">{ageText(b.time, now, b.height)}</td>
        </tr>
      {:else}
        <tr><td colspan="5" class="muted">{t('Waiting for the nodes…')}</td></tr>
      {/each}
    </tbody>
  {:else}
    <thead>
      <tr>{#if several}<th>{t('Chain')}</th>{/if}<th>{t('Transaction')}</th><th class="end">{t('Size')}</th><th class="end">{t('Fee')}</th><th class="end">{t('Waiting for')}</th></tr>
    </thead>
    <tbody>
      {#each waitingRows as { chain, row: w } (chain + w.txid)}
        <tr class="clickable" onclick={() => openTx(chain, w.txid)} title={t('Open the transaction in the block explorer')} data-waiting-row={chain}>
          {#if several}<td><ChainBadge id={chain} name={nameOf(chain)} small /></td>{/if}
          <td class="mono" translate="no"><button type="button" class="link mono" onclick={(e) => { e.stopPropagation(); openTx(chain, w.txid); }}>{shortHash(w.txid, 8)}</button></td>
          <td class="end num">{formatCount(w.vsize)} vB</td>
          <td class="end"><ModuleAmount text={w.fee} coins /></td>
          <td class="end muted">{ageText(w.time, now)}</td>
        </tr>
      {:else}
        <tr><td colspan="5" class="muted">{several ? t('Nothing is waiting to be mined on any chain.') : t('Nothing is waiting to be mined.')}</td></tr>
      {/each}
    </tbody>
  {/if}
</table>
</div>
{#if mode === 'mempool' && waitingTotal > waitingRows.length}<p class="faint small more">{t('%1 waiting in all', formatCount(waitingTotal))}</p>{/if}
{#each missing as s (s)}<p class="error small"><SafeText text={`${chainLabel(s)}: ${many.errors[`activity:${s}`]}`} /></p>{/each}

<style>
  /* The table follows the room its panel has: the least useful columns go first (doc/unified-app.md §20.3). */
  .fit {
    container-type: inline-size;
    min-width: 0;
  }
  .chain-rows {
    width: 100%;
  }
  .chain-rows td,
  .chain-rows th {
    white-space: nowrap;
  }
  .strong {
    font-weight: 650;
  }
  .hash-col {
    max-width: 10rem;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .link {
    padding: 0;
    border: 0;
    background: none;
    color: inherit;
    font: inherit;
    cursor: pointer;
  }
  .link:hover {
    color: var(--link);
    text-decoration: underline;
  }
  .more {
    margin: 0.4rem 0 0;
  }
  @container (max-width: 30rem) {
    .hash-col {
      display: none;
    }
  }
</style>
