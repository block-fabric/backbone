<script lang="ts">
  // Transactions of every chain this app runs in one list, with a Chain column (doc/unified-app.md §18), or of one
  // sidechain. A sidechain's are read from its node through the host (chain.transactions: its wallet's history, amounts
  // as its node wrote them); a row opens its details here, in the app (chain.transaction: the same details the app shows
  // for its own), and from there that chain's own page if wanted.
  import { t } from '../lib/i18n.svelte';
  import SafeText from './SafeText.svelte';
  import ModuleAmount from './ModuleAmount.svelte';
  import ChainBadge from './ChainBadge.svelte';
  import Dialog from './Dialog.svelte';
  import TxDetailsView from './TxDetailsView.svelte';
  import { formatTime, shortHash } from '../lib/format';
  import { isEmptyRow, whatHappened, whatWords } from '../lib/activity';
  import { hub } from '../lib/sidechains.svelte';
  import { moduleName, openModule, productName } from '../lib/modules.svelte';
  import { ALL, chainHost, chainWallet, formatUnits, HOST, mergeTxs, sidechainTxs, slotLabel, type ManyChainTx } from '../lib/chains.svelte';
  import { wallets, node } from '../lib/state.svelte';
  import { current } from '../lib/walletdata.svelte';
  import type { TxDetails } from '../lib/wallet';

  let { chain }: { chain: string } = $props();

  let lists = $state<Record<string, ManyChainTx[]>>({});
  let errors = $state<Record<string, string>>({});
  let loading = $state(true);
  let open = $state<ManyChainTx | null>(null);
  let dialogOpen = $state(false);

  const stems = $derived(chain === ALL ? (hub.list?.chains ?? []).filter((c) => c.enabled && c.state === 'running' && c.ready).map((c) => c.stem) : chain === HOST ? [] : [chain]);

  async function load(stem: string) {
    try {
      const got = await chainHost.transactions(stem, chainWallet(stem), 200);
      lists[stem] = sidechainTxs(stem, moduleName(stem), got.rows);
      errors[stem] = '';
    } catch (e) {
      errors[stem] = e instanceof Error ? e.message : String(e);
    }
  }

  $effect(() => {
    const list = stems;
    void node.tip?.hash;
    loading = true;
    void Promise.all(list.map(load)).finally(() => (loading = false));
  });
  // A sidechain's new blocks come without an event of the app: asked again every few seconds while shown.
  $effect(() => {
    const list = stems;
    const timer = setInterval(() => {
      if (!document.hidden) for (const s of list) void load(s);
    }, 8000);
    return () => clearInterval(timer);
  });

  // The app's own, in the same rows (every chain together).
  const own = $derived<ManyChainTx[]>(
    chain === ALL
      ? current.records.map((r) => ({ chain: HOST, name: productName(), txid: r.txid, time: r.time, category: r.type, amount: formatUnits(r.amount, 8), address: r.address, label: r.label, confirmations: r.depth, vout: r.idx, drivechain: r.drivechain ?? null }))
      : [],
  );
  const rows = $derived(
    mergeTxs([own, ...stems.map((s) => lists[s] ?? [])])
      .filter((r) => !isEmptyRow(r, HOST))
      .slice(0, 500),
  );

  function show(r: ManyChainTx) {
    open = r;
    dialogOpen = true;
  }
  function details(r: ManyChainTx): () => Promise<TxDetails> {
    return () => chainHost.transaction(r.chain, r.txid, chainWallet(r.chain), r.vout, r.address) as Promise<TxDetails>;
  }
</script>

<div class="chain-activity" data-chain-activity={chain}>
  {#each stems as s (s)}
    {#if errors[s]}<p class="error" role="alert"><SafeText text={moduleName(s)} />: <SafeText text={errors[s]} /></p>{/if}
  {/each}
  <table class="table">
    <thead>
      <tr><th>{t('Chain')}</th><th>{t('Date')}</th><th>{t('Type')}</th><th>{t('Label')}</th><th class="num">{t('Amount')}</th><th class="num">{t('Confirmations')}</th></tr>
    </thead>
    <tbody>
      {#each rows as r, i (i)}
        <tr data-chain-tx={r.chain} data-txid={r.txid} onclick={() => show(r)} class="tx-row">
          <td><ChainBadge id={r.chain} name={r.name} small /></td>
          <td>{formatTime(r.time)}</td>
          <td>{whatWords(whatHappened(r, HOST), productName(), r.drivechain?.kind === 'deposit' ? slotLabel(r.drivechain.slot) : '')}</td>
          <td class="addr" translate="no" title={r.address || r.txid}>{#if r.label}<SafeText text={r.label} />{:else}<span class="mono faint"><SafeText text={shortHash(r.address || r.txid, 8)} /></span>{/if}</td>
          <td class="num amt" class:neg={r.amount.startsWith('-')} translate="no"><ModuleAmount text={r.amount} plus coins /></td>
          <td class="num">{r.confirmations}</td>
        </tr>
      {:else}
        <tr><td colspan="6" class="muted">{loading ? t('Loading…') : t('No transactions yet')}</td></tr>
      {/each}
    </tbody>
  </table>
  {#if wallets.current === null && chain === ALL}<p class="faint">{t('No wallet of %1 is loaded: its transactions are not in the list.', productName())}</p>{/if}
</div>

<Dialog bind:open={dialogOpen} title={t('Transaction details')} wide onclose={() => (open = null)}>
  {#if open}
    <p class="on" data-tx-chain={open.chain}>{t('On')} <ChainBadge id={open.chain} name={open.name} /></p>
    {#if open.chain === HOST && wallets.current !== null}
      <TxDetailsView wallet={wallets.current} record={{ txid: open.txid, idx: open.vout, address: open.address }} />
    {:else}
      <TxDetailsView wallet="" record={{ txid: open.txid, idx: open.vout, address: open.address }} load={details(open)} />
    {/if}
  {/if}
  {#snippet actions()}
    {#if open && open.chain !== HOST}
      {@const stem = open.chain}
      <button type="button" class="btn" onclick={() => { dialogOpen = false; openModule(stem, 'transactions'); }}>{t('Open in %1', open.name)}</button>
    {/if}
    <button type="button" class="btn primary" onclick={() => (dialogOpen = false)}>{t('Close')}</button>
  {/snippet}
</Dialog>

<style>
  .chain-activity {
    overflow: auto;
    min-height: 0;
    flex: 1;
  }
  .table {
    width: 100%;
    border-collapse: collapse;
  }
  .table th,
  .table td {
    padding: 0.42rem 0.55rem;
    border-bottom: 1px solid var(--line);
    text-align: left;
    white-space: nowrap;
  }
  .table th {
    position: sticky;
    top: 0;
    background: var(--bg);
    color: var(--text-faint);
    font-size: 0.78rem;
    text-transform: uppercase;
    letter-spacing: 0.04em;
  }
  .num {
    text-align: right !important;
    font-variant-numeric: tabular-nums;
  }
  .amt {
    color: var(--positive);
  }
  .amt.neg {
    color: var(--text);
  }
  .addr {
    max-width: 22rem;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .tx-row {
    cursor: pointer;
  }
  .tx-row:hover td {
    background: var(--surface-2);
  }
  .on {
    margin: 0 0 0.8rem;
    display: flex;
    gap: 0.5rem;
    align-items: center;
  }
  .error {
    color: var(--negative);
  }
</style>
