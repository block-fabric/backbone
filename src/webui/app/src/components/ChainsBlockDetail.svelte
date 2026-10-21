<script lang="ts">
  // A block of this app's own chain, under the strips of the block explorer (doc/unified-app.md §21.4): what its page in
  // the explorer of Chains says (components/MainExplorer.svelte) — its facts, its coinbase and its drivechain messages
  // (components/BlockActivity.svelte), its transactions — read from the node when it is opened. A transaction or an
  // address opens in the explorer of Chains.
  import { t, tn } from '../lib/i18n.svelte';
  import Amount from './Amount.svelte';
  import BlockActivity from './BlockActivity.svelte';
  import CopyButton from './CopyButton.svelte';
  import { formatBytes, formatCount, formatTime } from '../lib/format';
  import { host, rpc } from '../lib/rpc';
  import { simpleMode } from '../lib/uimode.svelte';
  import type { BlockDetail } from '../lib/explorer';
  import type { Snippet } from 'svelte';

  interface Block {
    hash: string;
    height: number;
    time: number;
    mediantime: number;
    confirmations: number;
    size: number;
    weight: number;
    difficulty: number;
    bits: string;
    merkleroot: string;
    version: number;
    nonce: number;
    previousblockhash?: string;
    tx: string[];
  }

  // head: the head of its panel (its title and the actions of the page it is in).
  let { hash, head, ontx, onaddress, onblock }: { hash: string; head: Snippet; ontx: (txid: string) => void; onaddress: (address: string) => void; onblock: (hash: string) => void } = $props();

  let block = $state<Block | null>(null);
  let activity = $state<BlockDetail | null>(null);
  let error = $state('');
  /** The transactions listed: the first SHOWN, the rest a click away. */
  const SHOWN = 40;
  let all = $state(false);

  $effect(() => {
    const h = hash;
    block = null;
    activity = null;
    error = '';
    all = false;
    rpc.raw<Block>('getblock', [h, 1]).then(
      (b) => {
        if (h === hash) block = b;
      },
      (e) => {
        if (h === hash) error = e instanceof Error ? e.message : String(e);
      },
    );
    host.explorerBlock(h).then(
      (d) => {
        if (h === hash) activity = d;
      },
      () => undefined,
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
        <dl class="facts" data-chains-block={block.height}>
          <dt>{t('Hash')}</dt><dd class="mono break">{block.hash} <CopyButton text={block.hash} label={t('Copy the block hash')} /></dd>
          <dt>{t('Time')}</dt><dd>{formatTime(block.time)}</dd>
          <dt>{t('Confirmations')}</dt><dd class="num">{formatCount(block.confirmations)}</dd>
          <dt>{t('Transactions')}</dt><dd class="num">{formatCount(block.tx.length)}</dd>
          <dt>{t('Size')}</dt><dd>{formatBytes(block.size)}<span class="dot-sep">{' · '}</span>{t('weight %1', formatCount(block.weight))}</dd>
          <dt>{t('Reward')}</dt><dd>{#if activity}<Amount sats={activity.reward} masked={false} />{#if activity.fees !== null}{' '}<span class="faint">({t('fees')}{' '}<Amount sats={activity.fees} masked={false} />)</span>{/if}{:else}<span class="faint">…</span>{/if}</dd>
          {#if block.previousblockhash}<dt>{t('Previous block')}</dt><dd class="mono break"><button type="button" class="mono linkish" onclick={() => onblock(block!.previousblockhash!)}>{formatCount(block.height - 1)}</button></dd>{/if}
          {#if !simpleMode()}
            <dt>{t('Difficulty')}</dt><dd class="num">{block.difficulty.toPrecision(8)}{' '}<span class="faint mono">{t('bits %1', block.bits)}</span></dd>
            <dt>{t('Merkle root')}</dt><dd class="mono break">{block.merkleroot}</dd>
          {/if}
        </dl>
        <div class="txs-box">
          <h3>{tn('%n transaction(s)', block.tx.length)}</h3>
          <ul class="txs" aria-label={t('Transactions of the block')}>
            {#each all ? block.tx : block.tx.slice(0, SHOWN) as txid, i (txid)}
              <li><button type="button" class="mono" onclick={() => ontx(txid)}><span class="faint">{i}</span> {txid}</button></li>
            {/each}
          </ul>
          {#if !all && block.tx.length > SHOWN}<button type="button" class="btn small ghost" onclick={() => (all = true)}>{t('Show all %1', formatCount(block.tx.length))}</button>{/if}
        </div>
      </div>
    {/if}
  </div>
</section>
<!-- Its coinbase and drivechain messages: panels of their own after it, as in Chains' explorer. -->
{#if block && activity}<BlockActivity detail={activity} {ontx} {onaddress} />{/if}

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
  .break {
    overflow-wrap: anywhere;
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
  .txs button {
    border: 0;
    background: none;
    padding: 0.15rem 0;
    color: var(--link);
    font-size: 0.8rem;
    text-align: left;
    cursor: pointer;
    overflow-wrap: anywhere;
  }
  .linkish {
    border: 0;
    background: none;
    padding: 0;
    color: var(--link);
    cursor: pointer;
  }
</style>
