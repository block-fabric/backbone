<script lang="ts">
  // The details of a transaction of the wallet (TransactionDescDialog, TransactionDesc::toHTML).
  import Amount from './Amount.svelte';
  import CopyButton from './CopyButton.svelte';
  import Dialog from './Dialog.svelte';
  import { formatTime } from '../lib/format';
  import { walletApi, type DetailRow, type TxDetails, type TxRecord } from '../lib/wallet';

  let { wallet, record = $bindable(null) }: { wallet: string; record?: Pick<TxRecord, 'txid' | 'idx' | 'address'> | null } = $props();

  let details: TxDetails | null = $state(null);
  let error = $state('');
  let open = $state(false);

  $effect(() => {
    const r = record;
    open = r !== null;
    details = null;
    error = '';
    if (!r) return;
    walletApi(wallet)
      .transaction(r.txid, r.idx, r.address)
      .then((d) => (details = d))
      .catch((e) => (error = e instanceof Error ? e.message : String(e)));
  });
</script>

{#snippet value(row: DetailRow)}
  {#if row.address !== undefined}
    {#if row.label}<span translate="no">{row.label}</span>{/if}
    <span class="mono break">{row.address}</span>
    {#if row.own}<span class="badge accent">own address</span>{/if}
  {:else if row.sats !== undefined}
    <Amount sats={row.sats} plus={row.plus} colored={row.k === 'Net amount'} />
    {#if row.note}<span class="muted">({row.note})</span>{/if}
  {:else if row.time !== undefined}
    {row.time ? formatTime(row.time) : ''}
  {:else}
    <span class="break">{row.text}</span>
  {/if}
{/snippet}

<Dialog bind:open title="Transaction details" wide onclose={() => (record = null)}>
  {#if error}
    <p class="error">{error}</p>
  {:else if !details}
    <p class="muted">Loading…</p>
  {:else}
    <div class="details" data-txid={details.txid}>
      <dl class="facts">
        {#each details.rows as row, i (i)}
          <dt>{row.k}</dt>
          <dd>{@render value(row)}</dd>
        {/each}
      </dl>
      {#each details.notes as note, i (i)}
        <h3>{note.k}</h3>
        <p class="pre break" translate="no">{note.text}</p>
      {/each}
      {#each details.messages as m, i (i)}
        <h3>Message</h3>
        <p class="pre break">{m}</p>
      {/each}
      <dl class="facts ids">
        {#each details.ids as row, i (i)}
          <dt>{row.k}</dt>
          <dd>
            <span class:mono={row.k === 'Transaction ID'} class="break">{row.text}</span>
            {#if row.k === 'Transaction ID'}<CopyButton text={row.text ?? ''} label="Copy transaction ID" />{/if}
          </dd>
        {/each}
      </dl>
      {#if details.coinbaseNote}<p class="muted note">{details.coinbaseNote}</p>{/if}
      {#if details.debug}
        <details class="debug">
          <summary>Debug information</summary>
          <dl class="facts">
            {#each details.debug.amounts as row, i (i)}
              <dt>{row.k}</dt>
              <dd>{@render value(row)}</dd>
            {/each}
          </dl>
          <h3>Transaction</h3>
          <pre class="mono">{details.debug.transaction}</pre>
          <h3>Inputs</h3>
          <ul>
            {#each details.debug.inputs as input, i (i)}
              <li>
                {#if input.label}{input.label}{/if} <span class="mono">{input.address ?? ''}</span> Amount=<Amount sats={input.sats} /> IsMine={input.mine ? 'true' : 'false'}
              </li>
            {/each}
          </ul>
        </details>
      {/if}
    </div>
  {/if}
  {#snippet actions()}
    <button type="button" class="btn primary" onclick={() => (record = null)}>Close</button>
  {/snippet}
</Dialog>

<style>
  .details {
    display: flex;
    flex-direction: column;
    gap: 0.8rem;
  }
  dd {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: 0.4rem;
  }
  .ids {
    padding-top: 0.7rem;
    border-top: 1px solid var(--line);
  }
  h3 {
    margin-top: 0.3rem;
    font-size: 0.95rem;
  }
  .pre {
    white-space: pre-wrap;
  }
  .note {
    font-size: 0.9rem;
  }
  .debug pre {
    white-space: pre-wrap;
    font-size: 0.78rem;
    background: var(--surface-2);
    padding: 0.6rem;
    border-radius: var(--radius-s);
  }
  summary {
    cursor: pointer;
    font-weight: 600;
  }
</style>
