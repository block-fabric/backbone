<script lang="ts">
  // The coinbase of a block and what the block did to the sidechains, in the block explorer of Chains (after the
  // blocks of the pool's site): the subsidy and the fees, each output of the coinbase and what it is (the miner's
  // payout, the witness commitment, the BIP300 messages M1-M4, the BIP301 accepts M7, other data), then the blind
  // merged mining it took, the sidechains proposed, acked and activated, its votes on withdrawal bundles, the bundles
  // proposed, paid out or failed and those still pending, and the deposits and withdrawals of the escrows.
  // All of it decoded by the node (host explorer.block, src/webui/explorer.cpp).
  import Amount from './Amount.svelte';
  import CopyButton from './CopyButton.svelte';
  import SafeText from './SafeText.svelte';
  import { coinbaseTag } from '../lib/blocks';
  import { outputKindText, voteEntryText, voteFormText, type BlockDetail } from '../lib/explorer';
  import { formatCount, shortHash } from '../lib/format';
  import { t } from '../lib/i18n.svelte';

  let { detail, ontx }: { detail: BlockDetail; ontx: (txid: string) => void } = $props();

  const tag = $derived(coinbaseTag(detail.script, 60));
  const hidden = $derived(detail.outputCount - detail.outputs.length);
  const castVotes = $derived(detail.votes.entries.filter((v) => v.vote !== 'abstain').length);
  const nothing = $derived(
    !detail.bmm.length && !detail.proposals.length && !detail.acks.length && !detail.activations.length && !detail.votes.form && !detail.proposed.length && !detail.closed.length && !detail.pending.length && !detail.escrow.length,
  );
</script>

{#snippet sidechain(s: { slot?: number; title?: string; position?: number })}
  {#if s.slot === undefined}<span class="faint">#{s.position}</span>
  {:else}<span class="sc">{#if s.title}<SafeText text={s.title} /> {/if}<span class="faint">slot {s.slot}</span></span>{/if}
{/snippet}
{#snippet hash(h: string, label: string)}
  <span class="mono hash" title={h}>{shortHash(h, 10)}</span> <CopyButton text={h} {label} />
{/snippet}
{#snippet tx(txid: string)}
  <button type="button" class="mono linkish" title={txid} onclick={() => ontx(txid)}>{shortHash(txid, 10)}</button>
{/snippet}

<section class="panel activity" aria-label="Coinbase">
  <div class="panel-head"><h2>Coinbase</h2></div>
  <div class="panel-body">
    <dl class="facts money">
      <dt>Block subsidy</dt><dd><Amount sats={detail.subsidy} masked={false} /></dd>
      <dt>Fees</dt><dd>{#if detail.fees === null}<span class="faint">not known (the spent coins are pruned)</span>{:else}<Amount sats={detail.fees} masked={false} />{/if}</dd>
      <dt>Reward paid</dt><dd><Amount sats={detail.reward} masked={false} />{#if detail.unclaimed}<span class="faint"> · <Amount sats={detail.unclaimed} masked={false} /> left unclaimed</span>{/if}</dd>
      {#if tag}<dt>Miner's tag</dt><dd><SafeText text={tag} /></dd>{/if}
      <dt>Transaction</dt><dd>{@render tx(detail.coinbase)}</dd>
    </dl>
    <h3>Outputs ({formatCount(detail.outputCount)})</h3>
    <div class="rows outputs" role="table" aria-label="Outputs of the coinbase">
      {#each detail.outputs as o (o.n)}
        <div class="row-line" role="row" data-kind={o.kind}>
          <span class="faint n" role="cell">{o.n}</span>
          <span class="kind" role="cell"><span class="badge" class:accent={o.kind !== 'payout' && o.kind !== 'witness' && o.kind !== 'data'}>{outputKindText(o)}</span></span>
          <span class="what" role="cell">
            {#if o.kind === 'payout'}<span class="mono break">{#if o.address}<SafeText text={o.address} />{:else}{o.type}{/if}</span>
            {:else if o.kind === 'vote'}{voteFormText(o.form ?? '')} <span class="faint">· {formatCount(o.count ?? 0)} entries</span>
            {:else if o.kind === 'witness'}{@render hash(o.hash ?? '', 'Copy the witness commitment')}
            {:else if o.kind === 'data'}{#if o.text}<SafeText text={o.text} class="mono" />{:else}<span class="mono break faint">{o.hex}</span>{/if}
            {:else}{@render sidechain(o)}{#if o.hash} {@render hash(o.hash, 'Copy the hash')}{/if}{/if}
          </span>
          <span class="amt" role="cell">{#if o.value}<Amount sats={o.value} masked={false} />{:else}<span class="faint">0</span>{/if}</span>
        </div>
      {/each}
    </div>
    {#if hidden > 0}<p class="faint">And {formatCount(hidden)} more outputs.</p>{/if}
  </div>
</section>

<section class="panel activity" aria-label="Drivechain activity">
  <div class="panel-head"><h2>Drivechain activity</h2></div>
  <div class="panel-body">
    {#if nothing}
      <p class="muted">This block did nothing on sidechains: no BMM, no votes, no deposits.</p>
    {/if}
    {#if detail.bmm.length}
      <h3>Blind merged mining ({detail.bmm.length})</h3>
      <div class="rows" role="table" aria-label="BMM accepts">
        {#each detail.bmm as b (b.slot)}
          <div class="row-line" role="row">
            <span role="cell">{@render sidechain(b)}</span>
            <span role="cell"><span class="faint">sidechain block</span> {@render hash(b.hash, 'Copy the sidechain block hash')}</span>
            <span role="cell">{#if b.request}<span class="faint">request</span> {@render tx(b.request)}{/if}</span>
            <span class="amt" role="cell">{#if b.bid !== undefined}<span class="faint">bid</span> <Amount sats={b.bid} masked={false} />{/if}</span>
          </div>
        {/each}
      </div>
    {/if}
    {#if detail.proposals.length || detail.acks.length || detail.activations.length}
      <h3>Sidechain proposals</h3>
      <div class="rows" role="table" aria-label="Sidechain proposals and acks">
        {#each detail.activations as a (a.slot)}
          <div class="row-line" role="row"><span role="cell"><span class="badge accent">Activated</span></span><span role="cell">{@render sidechain(a)}</span></div>
        {/each}
        {#each detail.proposals as p (p.hash)}
          <div class="row-line" role="row">
            <span role="cell"><span class="badge accent">Proposed</span></span>
            <span role="cell">{@render sidechain(p)}{#if p.replaces} <span class="faint">replacing</span> <SafeText text={p.replaces} />{/if}</span>
            <span class="desc" role="cell"><SafeText text={p.description} /></span>
            <span role="cell">{@render hash(p.hash, 'Copy the proposal hash')}</span>
          </div>
        {/each}
        {#each detail.acks as a (a.slot + a.hash)}
          <div class="row-line" role="row"><span role="cell"><span class="badge">Ack</span></span><span role="cell">{@render sidechain(a)}</span><span role="cell">{@render hash(a.hash, 'Copy the proposal hash')}</span></div>
        {/each}
      </div>
    {/if}
    {#if detail.votes.form || detail.proposed.length || detail.closed.length || detail.pending.length}
      <h3>Withdrawal bundles</h3>
      <p class="faint votes-form">Votes: {voteFormText(detail.votes.form)}{#if detail.votes.form && castVotes}&nbsp;· {formatCount(castVotes)} cast{/if}{#if !detail.votes.known}&nbsp;· the sidechains they are for cannot be told{/if}</p>
      <div class="rows" role="table" aria-label="Bundle votes and bundles">
        {#each detail.votes.entries as v, i (i)}
          <div class="row-line" role="row" data-vote={v.vote}>
            <span role="cell"><span class="badge" class:accent={v.vote === 'upvote'} class:danger={v.vote === 'downvote'}>{voteEntryText(v)}</span></span>
            <span role="cell">{@render sidechain(v)}</span>
            <span role="cell">{#if v.bundle}{@render hash(v.bundle, 'Copy the bundle hash')}{/if}</span>
          </div>
        {/each}
        {#each detail.proposed as b (b.hash)}
          <div class="row-line" role="row"><span role="cell"><span class="badge accent">Bundle proposed</span></span><span role="cell">{@render sidechain(b)}</span><span role="cell">{@render hash(b.hash, 'Copy the bundle hash')}</span></div>
        {/each}
        {#each detail.closed as b (b.hash)}
          <div class="row-line" role="row"><span role="cell"><span class="badge" class:accent={b.paid} class:danger={!b.paid}>{b.paid ? t('Bundle paid out') : t('Bundle failed')}</span></span><span role="cell">{@render sidechain(b)}</span><span role="cell">{@render hash(b.hash, 'Copy the bundle hash')}</span></div>
        {/each}
        {#each detail.pending as p (p.slot)}
          {#each p.bundles as b (b.hash)}
            <div class="row-line" role="row">
              <span role="cell"><span class="badge">Pending</span></span>
              <span role="cell">{@render sidechain(p)}</span>
              <span role="cell">{@render hash(b.hash, 'Copy the bundle hash')}</span>
              <span class="amt" role="cell"><span class="faint">score</span> {formatCount(b.score)} / {formatCount(detail.minScore)}</span>
            </div>
          {/each}
        {/each}
      </div>
    {/if}
    {#if detail.escrow.length}
      <h3>Deposits and withdrawals ({formatCount(detail.escrowCount)})</h3>
      <div class="rows" role="table" aria-label="Escrow changes">
        {#each detail.escrow as e (e.txid + e.slot)}
          <div class="row-line" role="row" data-escrow={e.kind}>
            <span role="cell"><span class="badge" class:accent={e.kind === 'deposit'} class:warning={e.kind === 'withdrawal'}>{e.kind === 'deposit' ? t('Deposit') : t('Withdrawal')}</span></span>
            <span role="cell">{@render sidechain(e)}</span>
            <span role="cell">
              {#if e.kind === 'deposit'}<span class="faint">to</span> <SafeText text={e.destination} class="mono" />
              {:else}<span class="faint">{formatCount(e.payouts ?? 0)} payouts of</span> <Amount sats={e.paid ?? 0} masked={false} />{#if e.fee !== undefined} <span class="faint">· fee</span> <Amount sats={e.fee} masked={false} />{/if}{/if}
              <span class="faint">· tx</span> {@render tx(e.txid)}
            </span>
            <span class="amt" role="cell">{#if e.kind === 'deposit'}<Amount sats={e.amount} plus masked={false} />{/if} <span class="faint">escrow</span> <Amount sats={e.total} masked={false} /></span>
          </div>
        {/each}
      </div>
    {/if}
    {#if !detail.events && detail.height > 0}<p class="faint">The node has no drivechain record of this block.</p>{/if}
  </div>
</section>

<style>
  .activity {
    margin-bottom: 1rem;
  }
  .activity h3 {
    margin: 1rem 0 0.4rem;
  }
  .activity h3:first-child {
    margin-top: 0;
  }
  .money {
    margin-bottom: 0.5rem;
  }
  .rows {
    display: flex;
    flex-direction: column;
    border: 1px solid var(--line);
    border-radius: var(--radius-s);
    max-height: 24rem;
    overflow: auto;
  }
  .row-line {
    display: grid;
    grid-template-columns: minmax(9rem, auto) minmax(10rem, 1fr) minmax(10rem, 1.2fr) minmax(8rem, auto);
    gap: 0.4rem 0.9rem;
    align-items: baseline;
    padding: 0.35rem 0.6rem;
    font-size: 0.9rem;
    border-top: 1px solid var(--line);
  }
  .row-line:first-child {
    border-top: 0;
  }
  .outputs .row-line {
    grid-template-columns: 2rem minmax(11rem, auto) minmax(12rem, 1fr) minmax(8rem, auto);
  }
  .amt {
    text-align: end;
    white-space: nowrap;
  }
  .what,
  .desc {
    min-width: 0;
    overflow-wrap: anywhere;
  }
  .what {
    display: flex;
    flex-wrap: wrap;
    gap: 0 0.5em;
    align-items: baseline;
  }
  .hash {
    white-space: nowrap;
  }
  .sc {
    display: inline-flex;
    flex-wrap: wrap;
    gap: 0 0.4em;
    align-items: baseline;
  }
  .linkish {
    padding: 0;
    border: 0;
    background: none;
    color: var(--link);
    cursor: pointer;
  }
  .votes-form {
    margin: 0 0 0.4rem;
  }
  @media (max-width: 900px) {
    .row-line,
    .outputs .row-line {
      grid-template-columns: minmax(0, 1fr) minmax(0, 1fr);
    }
    .amt {
      text-align: start;
    }
  }
</style>
