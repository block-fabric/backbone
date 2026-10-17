<script lang="ts">
  // The coinbase of a block and what the block did to the sidechains, in the block explorer of Chains (after the
  // blocks of the pool's site): the subsidy and the fees, each output of the coinbase and what it is (the miner's
  // payout, the witness commitment, the BIP300 messages M1-M4, the BIP301 accepts M7, other data), then the blind
  // merged mining it took, the sidechains proposed, acked and activated, its votes on withdrawal bundles, the bundles
  // proposed, paid out or failed and those still pending, and the deposits and withdrawals of the escrows.
  // All of it decoded by the node (host explorer.block, src/webui/explorer.cpp). Its ids link to their views: transactions
  // (ontx), addresses of this chain (onaddress, those of the open wallet marked "yours"), bundles and proposals on the
  // Sidechains page; a sidechain's block hash (BMM) is no block of this chain: it says where to see it.
  import Amount from './Amount.svelte';
  import AddressLink from './AddressLink.svelte';
  import { goTo } from '../lib/nav.svelte';
  import CopyButton from './CopyButton.svelte';
  import SafeText from './SafeText.svelte';
  import { coinbaseTag } from '../lib/blocks';
  import { outputKindText, voteEntryText, voteFormText, type BlockDetail } from '../lib/explorer';
  import { formatCount, shortHash } from '../lib/format';
  import { t } from '../lib/i18n.svelte';

  let { detail, ontx, onaddress }: { detail: BlockDetail; ontx: (txid: string) => void; onaddress: (address: string) => void } = $props();
  /** A bundle or a proposal, on the Sidechains page (its tab, the sidechain's slot and the hash chosen there). */
  const openBundle = (slot: number | undefined, hash: string) => goTo('sidechains', 'withdrawals', slot === undefined ? '' : `${slot}:${hash}`);
  const openProposal = (slot: number | undefined, hash: string) => goTo('sidechains', 'proposals', slot === undefined ? '' : `${slot}:${hash}`);
  const sidechainBlockTip = (title: string | undefined) => (title ? t('A block of %1: see it in its own wallet', title) : t('A block of a sidechain: see it in its own wallet'));

  const tag = $derived(coinbaseTag(detail.script, 60));
  // The outputs in two lists side by side: what the coinbase says (BIP300/301 messages, commitments, data) and what
  // it pays (the miner's payout and any other output with coins).
  const hasBundles = $derived(!!(detail.votes.form || detail.proposed.length || detail.closed.length || detail.pending.length));
  const payouts = $derived(detail.outputs.filter((o) => o.kind === 'payout' || o.value > 0));
  const messages = $derived(detail.outputs.filter((o) => !(o.kind === 'payout' || o.value > 0)));
  const hidden = $derived(detail.outputCount - detail.outputs.length);
  const castVotes = $derived(detail.votes.entries.filter((v) => v.vote !== 'abstain').length);
  // One row per bundle of a sidechain, in the order of the slots: this block's vote on it, what became of it (proposed,
  // paid out, failed, still pending with its score). A vote that names no bundle (downvote all, abstain) goes on each
  // pending bundle of its sidechain, or on a row of its own when there is none.
  type Entry = BlockDetail['votes']['entries'][number];
  type BundleRow = { key: string; sc: { slot?: number; title?: string; position?: number }; vote?: Entry; state?: 'proposed' | 'paid' | 'failed' | 'pending'; hash?: string; score?: number };
  const bundleRows = $derived.by(() => {
    const rows = new Map<string, BundleRow>();
    const side = (x: { slot?: number; position?: number }) => (x.slot === undefined ? `p${x.position}` : `s${x.slot}`);
    const row = (sc: BundleRow['sc'], hash?: string) => {
      const key = `${side(sc)}|${hash ?? ''}`;
      let r = rows.get(key);
      if (!r) rows.set(key, (r = { key, sc, hash }));
      if (sc.title && !r.sc.title) r.sc = sc;
      return r;
    };
    for (const b of detail.proposed) row(b, b.hash).state = 'proposed';
    for (const b of detail.closed) row(b, b.hash).state = b.paid ? 'paid' : 'failed';
    for (const p of detail.pending)
      for (const b of p.bundles) {
        const r = row(p, b.hash);
        r.state ??= 'pending';
        r.score = b.score;
      }
    for (const v of detail.votes.entries) {
      if (v.bundle) row(v, v.bundle).vote = v;
      else {
        const same = [...rows.values()].filter((r) => r.hash && side(r.sc) === side(v) && r.state === 'pending');
        if (same.length) for (const r of same) r.vote ??= v;
        else row(v).vote = v;
      }
    }
    const order = (r: BundleRow) => (r.sc.slot ?? 1000 + (r.sc.position ?? 0));
    return [...rows.values()].sort((x, y) => order(x) - order(y) || (x.hash ?? '').localeCompare(y.hash ?? ''));
  });
  const bundleStateText = (st: BundleRow['state']) =>
    st === 'proposed' ? t('Bundle proposed') : st === 'paid' ? t('Bundle paid out') : st === 'failed' ? t('Bundle failed') : st === 'pending' ? t('Pending') : '';
  const nothing = $derived(
    !detail.bmm.length && !detail.proposals.length && !detail.acks.length && !detail.activations.length && !detail.votes.form && !detail.proposed.length && !detail.closed.length && !detail.pending.length && !detail.escrow.length,
  );
</script>

{#snippet sidechain(s: { slot?: number; title?: string; position?: number })}
  {#if s.slot === undefined}<span class="faint">#{s.position}</span>
  {:else}<span class="sc">{#if s.title}<SafeText text={s.title} /> {/if}<span class="faint">slot {s.slot}</span></span>{/if}
{/snippet}
{#snippet hash(h: string, label: string, tip?: string)}
  <span class="hashc"><span class="mono hash" title={tip ?? h}>{shortHash(h, 8)}</span><CopyButton text={h} {label} /></span>
{/snippet}
{#snippet linked(h: string, label: string, tip: string, go: () => void)}
  <span class="hashc"><button type="button" class="mono linkish hash" title={tip} onclick={go}>{shortHash(h, 8)}</button><CopyButton text={h} {label} /></span>
{/snippet}
{#snippet bundleHash(slot: number | undefined, h: string)}
  {@render linked(h, 'Copy the bundle hash', t('See the bundle on the Sidechains page'), () => openBundle(slot, h))}
{/snippet}
{#snippet proposalHash(slot: number | undefined, h: string)}
  {@render linked(h, 'Copy the proposal hash', t('See the proposal on the Sidechains page'), () => openProposal(slot, h))}
{/snippet}
{#snippet outputHash(o: BlockDetail['outputs'][number])}
  {#if !o.hash}<span class="faint">–</span>
  {:else if o.kind === 'bundle'}{@render bundleHash(o.slot, o.hash)}
  {:else if o.kind === 'proposal' || o.kind === 'ack'}{@render proposalHash(o.slot, o.hash)}
  {:else if o.kind === 'bmm'}{@render hash(o.hash, 'Copy the sidechain block hash', sidechainBlockTip(o.title))}
  {:else}{@render hash(o.hash, 'Copy the hash')}{/if}
{/snippet}
{#snippet tx(txid: string)}
  <button type="button" class="mono linkish" title={txid} onclick={() => ontx(txid)}>{shortHash(txid, 8)}</button>
{/snippet}

<div class="activity-grid">
<section class="panel activity coinbase" aria-label="Coinbase">
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
    <div class="split">
      <div class="split-side">
        <h4 class="faint">{t('Messages and data')} ({formatCount(messages.length)})</h4>
        <div class="rows outputs" role="table" aria-label="Messages and data">
            {#each messages as o (o.n)}
            <div class="row-line" role="row" data-kind={o.kind}>
              <span class="faint n" role="cell">{o.n}</span>
              <span class="kind" role="cell"><span class="badge" class:accent={o.kind !== 'payout' && o.kind !== 'witness' && o.kind !== 'data'}>{outputKindText(o)}</span></span>
              <span class="amt" role="cell">{#if o.value}<Amount sats={o.value} masked={false} />{/if}</span>
              <span class="what" role="cell">
                {#if o.kind === 'payout'}<span class="mono break">{#if o.address}<AddressLink address={o.address} onopen={onaddress} />{:else}{o.type}{/if}</span>
                {:else if o.kind === 'vote'}{voteFormText(o.form ?? '')} <span class="faint">· {formatCount(o.count ?? 0)} entries</span>
                {:else if o.kind === 'witness'}{@render hash(o.hash ?? '', 'Copy the witness commitment')}
                {:else if o.kind === 'data'}{#if o.text}<SafeText text={o.text} class="mono" />{:else}<span class="mono break faint">{o.hex}</span>{/if}
                {:else}{@render sidechain(o)}{#if o.hash} {@render outputHash(o)}{/if}{/if}
              </span>
            </div>
          {:else}
            <p class="faint none">None</p>
          {/each}
        </div>
      </div>
      <div class="split-side">
        <h4 class="faint">{t('Payouts')} ({formatCount(payouts.length)})</h4>
        <div class="rows outputs" role="table" aria-label="Payouts">
            {#each payouts as o (o.n)}
            <div class="row-line" role="row" data-kind={o.kind}>
              <span class="faint n" role="cell">{o.n}</span>
              <span class="kind" role="cell"><span class="badge" class:accent={o.kind !== 'payout' && o.kind !== 'witness' && o.kind !== 'data'}>{outputKindText(o)}</span></span>
              <span class="amt" role="cell">{#if o.value}<Amount sats={o.value} masked={false} />{/if}</span>
              <span class="what" role="cell">
                {#if o.kind === 'payout'}<span class="mono break">{#if o.address}<AddressLink address={o.address} onopen={onaddress} />{:else}{o.type}{/if}</span>
                {:else if o.kind === 'vote'}{voteFormText(o.form ?? '')} <span class="faint">· {formatCount(o.count ?? 0)} entries</span>
                {:else if o.kind === 'witness'}{@render hash(o.hash ?? '', 'Copy the witness commitment')}
                {:else if o.kind === 'data'}{#if o.text}<SafeText text={o.text} class="mono" />{:else}<span class="mono break faint">{o.hex}</span>{/if}
                {:else}{@render sidechain(o)}{#if o.hash} {@render outputHash(o)}{/if}{/if}
              </span>
            </div>
          {:else}
            <p class="faint none">None</p>
          {/each}
        </div>
      </div>
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
    {#if detail.bmm.length || hasBundles}
      <div class="split" class:one={!(detail.bmm.length && hasBundles)}>
        <div class="split-side">
        {#if detail.bmm.length}
          <h3>Blind merged mining ({detail.bmm.length})</h3>
          <div class="rows" role="table" aria-label="BMM accepts">
            {#each detail.bmm as b (b.slot)}
              <div class="row-line" role="row">
                <span role="cell">{@render sidechain(b)}</span>
                <span role="cell"><span class="faint">sidechain block</span> {@render hash(b.hash, 'Copy the sidechain block hash', sidechainBlockTip(b.title))}</span>
                <span role="cell">{#if b.request}<span class="faint">request</span> {@render tx(b.request)}{/if}</span>
                <span class="amt" role="cell">{#if b.bid !== undefined}<span class="faint">bid</span> <Amount sats={b.bid} masked={false} />{/if}</span>
              </div>
            {/each}
          </div>
        {/if}
        </div>
        <div class="split-side">
        {#if detail.votes.form || detail.proposed.length || detail.closed.length || detail.pending.length}
          <h3>Withdrawal bundles</h3>
          <p class="faint votes-form">Votes: {voteFormText(detail.votes.form)}{#if detail.votes.form && castVotes}&nbsp;· {formatCount(castVotes)} cast{/if}{#if !detail.votes.known}&nbsp;· the sidechains they are for cannot be told{/if}</p>
          <div class="rows" role="table" aria-label="Bundle votes and bundles">
            {#each bundleRows as r (r.key)}
              <div class="row-line" role="row" data-vote={r.vote?.vote} data-state={r.state}>
                <span role="cell" class="badges">
                  {#if r.vote}<span class="badge" class:accent={r.vote.vote === 'upvote'} class:danger={r.vote.vote === 'downvote'}>{voteEntryText(r.vote)}</span>{/if}
                  {#if r.state}<span class="badge" class:accent={r.state === 'proposed' || r.state === 'paid'} class:danger={r.state === 'failed'}>{bundleStateText(r.state)}</span>{/if}
                </span>
                <span role="cell">{@render sidechain(r.sc)}</span>
                <span role="cell">{#if r.hash}{@render bundleHash(r.sc.slot, r.hash)}{/if}</span>
                <span class="amt" role="cell">{#if r.score !== undefined}<span class="faint">score</span> {formatCount(r.score)} / {formatCount(detail.minScore)}{/if}</span>
              </div>
            {/each}
          </div>
        {/if}
        </div>
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
            <span role="cell">{@render proposalHash(p.slot, p.hash)}</span>
          </div>
        {/each}
        {#each detail.acks as a (a.slot + a.hash)}
          <div class="row-line" role="row"><span role="cell"><span class="badge">Ack</span></span><span role="cell">{@render sidechain(a)}</span><span role="cell">{@render proposalHash(a.slot, a.hash)}</span></div>
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
              {#if e.kind === 'deposit'}<span class="faint">to</span> {#if e.mainchain && e.destination}<AddressLink address={e.destination} onopen={onaddress} />{:else}<SafeText text={e.destination} class="mono" />{/if}
              {:else}{#if e.bundle}<span class="faint">bundle</span> {@render bundleHash(e.slot, e.bundle)} {/if}<span class="faint">{formatCount(e.payouts ?? 0)} payouts of</span> <Amount sats={e.paid ?? 0} masked={false} />{#if e.fee !== undefined} <span class="faint">· fee</span> <Amount sats={e.fee} masked={false} />{/if}{/if}
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
</div>

<style>
  .activity-grid {
    display: grid;
    grid-template-columns: minmax(26rem, 1fr) minmax(24rem, 1.1fr);
    gap: 1rem;
    align-items: start;
    margin-bottom: 1rem;
  }
  .activity {
    min-width: 0;
    margin: 0;
  }
  .activity h3 {
    margin: 0.75rem 0 0.3rem;
    font-size: 0.95rem;
  }
  .activity h3:first-child {
    margin-top: 0;
  }
  .money {
    margin-bottom: 0.4rem;
    font-size: 0.9rem;
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
    grid-template-columns: auto minmax(min-content, 1fr) minmax(min-content, 1.2fr) auto;
    gap: 0.2rem 0.7rem;
    align-items: baseline;
    padding: 0.25rem 0.5rem;
    font-size: 0.85rem;
    border-top: 1px solid var(--line);
  }
  .row-line:first-child {
    border-top: 0;
  }
  .outputs .row-line {
    grid-template-columns: 1.4rem auto minmax(0, 1fr);
  }
  /* An output on two lines: its number, kind and amount, then what it is across the box. */
  .outputs .n {
    grid-column: 1;
    grid-row: 1;
  }
  .outputs .kind {
    grid-column: 2;
    grid-row: 1;
  }
  .outputs .amt {
    grid-column: 3;
    grid-row: 1;
  }
  .outputs .what {
    grid-column: 2 / -1;
    grid-row: 2;
    font-size: 0.8rem;
  }
  .split {
    display: grid;
    grid-template-columns: minmax(0, 1.2fr) minmax(0, 1fr);
    gap: 0.6rem;
    align-items: start;
  }
  .split.one {
    grid-template-columns: minmax(0, 1fr);
  }
  .split + h3 {
    margin-top: 0.75rem;
  }
  /* Half a box is too narrow for four columns: two, the facts flowing left then right. */
  .split-side > .rows:not(.outputs) > .row-line {
    grid-template-columns: minmax(0, 1fr) auto;
  }
  .split-side > h3:first-child {
    margin-top: 0;
  }
  .split h4 {
    margin: 0 0 0.25rem;
    font-size: 0.8rem;
    font-weight: 500;
  }
  .none {
    margin: 0;
    padding: 0.3rem 0.5rem;
    font-size: 0.85rem;
  }
  .hashc {
    display: inline-flex;
    align-items: center;
    gap: 0.25em;
    white-space: nowrap;
  }
  .amt {
    text-align: end;
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
    white-space: nowrap;
  }
  .badges {
    display: inline-flex;
    flex-wrap: wrap;
    gap: 0.25rem;
  }
  .votes-form {
    margin: 0 0 0.4rem;
  }
  @media (max-width: 1250px) {
    .activity-grid {
      grid-template-columns: minmax(0, 1fr);
    }
  }
  @media (max-width: 700px) {
    .split {
      grid-template-columns: minmax(0, 1fr);
    }
  }
  @media (max-width: 900px) {
    .row-line {
      grid-template-columns: minmax(0, 1fr) minmax(0, 1fr);
    }
    .amt {
      text-align: start;
    }
  }
</style>
