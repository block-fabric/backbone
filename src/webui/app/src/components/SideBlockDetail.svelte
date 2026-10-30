<script lang="ts">
  // A block of a sidechain, under the strips of the block explorer (doc/unified-app.md §21.4): its facts and its
  // transactions, read from its node (chain.block); what the chain itself says of it (its peg, the block of Chains that
  // merged mined it, its own operations) is its own explorer's, a click away (Open in its explorer, a transaction).
  import { t, tn } from '../lib/i18n.svelte';
  import CopyButton from './CopyButton.svelte';
  import { request } from '../lib/bridge';
  import { formatBytes, formatCount, formatTime, shortHash } from '../lib/format';
  import { simpleMode } from '../lib/uimode.svelte';
  import type { Snippet } from 'svelte';

  interface SideBlock {
    stem: string;
    hash: string;
    height?: number;
    time?: number;
    confirmations?: number;
    size?: number;
    weight?: number;
    nTx?: number;
    difficulty?: number;
    previousblockhash?: string;
    merkleroot?: string;
    tx: string[];
  }

  // head: the head of its panel; ontx: a transaction of the block (its chain's explorer); onblock: the block before.
  // name: the chain's; onexplore: the block in that chain's own explorer.
  let { stem, name, hash, head, ontx, onblock, onexplore }: { stem: string; name: string; hash: string; head: Snippet; ontx: (txid: string) => void; onblock: (hash: string) => void; onexplore: () => void } = $props();

  let block = $state<SideBlock | null>(null);
  let error = $state('');
  const SHOWN = 40;
  let all = $state(false);

  $effect(() => {
    const h = hash;
    const s = stem;
    block = null;
    error = '';
    all = false;
    request<SideBlock>('host', 'chain.block', { stem: s, hash: h }).then(
      (b) => {
        if (h === hash && s === stem) block = b;
      },
      (e) => {
        if (h === hash && s === stem) error = e instanceof Error ? e.message : String((e as { message?: unknown })?.message ?? e);
      },
    );
  });
</script>

<section class="panel">
  <header class="panel-head">{@render head()}</header>
  <div class="panel-body">
    {#if error}
      <p class="error">{error}</p>
    {:else if !block}
      <p class="muted">{t('Loading…')}</p>
    {:else}
      <div class="grid">
        <dl class="facts" data-side-block={`${stem}:${block.height ?? ''}`}>
          <dt>{t('Hash')}</dt><dd class="line"><span class="mono" title={block.hash} translate="no">{shortHash(block.hash, 16)}</span><CopyButton text={block.hash} label={t('Copy the block hash')} /></dd>
          {#if block.time !== undefined}<dt>{t('Time')}</dt><dd>{block.height === 0 ? t('genesis') : formatTime(block.time)}</dd>{/if}
          {#if block.confirmations !== undefined}<dt>{t('Confirmations')}</dt><dd class="num">{formatCount(block.confirmations)}</dd>{/if}
          <dt>{t('Transactions')}</dt><dd class="num">{formatCount(block.nTx ?? block.tx.length)}</dd>
          {#if block.size !== undefined}<dt>{t('Size')}</dt><dd>{formatBytes(block.size)}{#if block.weight !== undefined}<span class="dot-sep">{' · '}</span>{t('weight %1', formatCount(block.weight))}{/if}</dd>{/if}
          {#if block.previousblockhash}<dt>{t('Previous block')}</dt><dd><button type="button" class="mono linkish" onclick={() => onblock(block!.previousblockhash!)}>{formatCount((block.height ?? 1) - 1)}</button></dd>{/if}
          {#if !simpleMode() && block.merkleroot}<dt>{t('Merkle root')}</dt><dd class="line"><span class="mono" title={block.merkleroot} translate="no">{shortHash(block.merkleroot, 16)}</span><CopyButton text={block.merkleroot} label={t('Copy the merkle root')} /></dd>{/if}
        </dl>
        <div>
          <h3>{tn('%n transaction(s)', block.tx.length)}</h3>
          <ul class="txs" aria-label={t('Transactions of the block')}>
            {#each all ? block.tx : block.tx.slice(0, SHOWN) as txid, i (txid)}
              <li><button type="button" class="mono" title={txid} onclick={() => ontx(txid)}><span class="faint">{i}</span> {shortHash(txid, 22)}</button></li>
            {/each}
          </ul>
          {#if !all && block.tx.length > SHOWN}<button type="button" class="btn small ghost" onclick={() => (all = true)}>{t('Show all %1', formatCount(block.tx.length))}</button>{/if}
        </div>
      </div>
      <p class="faint small">{t('Peg activity and the {chain} block that merged-mined it:')} <button type="button" class="linkish" onclick={onexplore}>{t('open it in the %1 explorer', name)}</button>.</p>
    {/if}
  </div>
</section>

<style>
  .grid {
    display: grid;
    grid-template-columns: minmax(0, 1.1fr) minmax(0, 1fr);
    gap: 1.2rem;
  }
  @media (max-width: 1000px) {
    .grid {
      grid-template-columns: minmax(0, 1fr);
    }
  }
  .facts {
    display: grid;
    grid-template-columns: max-content minmax(0, 1fr);
    gap: 0.35rem 1rem;
    margin: 0;
  }
  .facts dt {
    color: var(--text-muted);
  }
  .facts dd {
    margin: 0;
  }
  /* A hash on one line, shortened in the middle (its whole in its tooltip and its copy button). */
  .line {
    display: flex;
    align-items: center;
    gap: 0.35rem;
    min-width: 0;
    white-space: nowrap;
  }
  h3 {
    margin: 0 0 0.4rem;
    font-size: 0.95rem;
  }
  .txs {
    list-style: none;
    margin: 0 0 0.5rem;
    padding: 0;
    max-height: 16rem;
    overflow: auto;
  }
  .txs button,
  .linkish {
    border: 0;
    background: none;
    padding: 0.15rem 0;
    color: var(--link);
    font-size: 0.8rem;
    text-align: left;
    cursor: pointer;
    overflow-wrap: anywhere;
  }
  .small {
    font-size: 0.82rem;
    margin: 0.6rem 0 0;
  }
</style>
