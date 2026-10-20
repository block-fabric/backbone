<script lang="ts">
  // The latest transactions of every chain shown (doc/unified-app.md §19: Home, Overview), newest first, each with its
  // chain's mark; this app's own from its wallet, a sidechain's from its node (chain.transactions). A row opens its
  // details here, in the app (the same view as for the app's own), "Open in <chain>" from there. Amounts as the node
  // wrote them, hidden in privacy mode.
  import { t } from '../lib/i18n.svelte';
  import SafeText from './SafeText.svelte';
  import ModuleAmount from './ModuleAmount.svelte';
  import ChainBadge from './ChainBadge.svelte';
  import Dialog from './Dialog.svelte';
  import TxDetailsView from './TxDetailsView.svelte';
  import { formatTime } from '../lib/format';
  import { chainHost, chainLabel, chainWallet, formatUnits, HOST, mergeTxs, sidechainTxs, type ManyChainTx } from '../lib/chains.svelte';
  import { many, poll, refreshTxs } from '../lib/manychains.svelte';
  import { openModule, productName } from '../lib/modules.svelte';
  import { node, prefs, wallets } from '../lib/state.svelte';
  import { walletApi, type TxDetails, type TxRecord } from '../lib/wallet';

  let { chains, count = 8 }: { chains: string[]; count?: number } = $props();

  let own: TxRecord[] = $state.raw([]);
  const withHost = $derived(chains.includes(HOST));
  const sides = $derived(chains.filter((c) => c !== HOST));

  $effect(() => {
    void wallets.txVersion;
    void node.tip?.hash;
    const name = wallets.current;
    if (!withHost || name === null) {
      own = [];
      return;
    }
    walletApi(name)
      .latest(count)
      .then((r) => {
        if (wallets.current === name) own = r;
      })
      .catch(() => undefined);
  });
  $effect(() => {
    const list = sides;
    return poll(() => list.forEach((s) => void refreshTxs(s)), 8000);
  });

  const rows = $derived(
    mergeTxs([
      own.map((r) => ({ chain: HOST, name: productName(), txid: r.txid, time: r.time, category: r.type, amount: formatUnits(r.amount, 8), address: r.address, label: r.label, confirmations: r.depth, vout: r.idx })),
      ...sides.map((s) => sidechainTxs(s, chainLabel(s), many.txs[s] ?? [])),
    ]).slice(0, count),
  );

  let open = $state<ManyChainTx | null>(null);
  let dialogOpen = $state(false);
  function show(r: ManyChainTx) {
    if (prefs.mask) return;
    open = r;
    dialogOpen = true;
  }
  const details = (r: ManyChainTx) => () => chainHost.transaction(r.chain, r.txid, chainWallet(r.chain), r.vout, r.address) as Promise<TxDetails>;
  const kind = (r: ManyChainTx) => (r.amount.startsWith('-') ? t('Sent') : r.category === 'generate' || r.category === 'immature' || r.category === 'mined' ? t('Mined') : t('Received'));
</script>

<ul class="many-recent" data-many-recent={chains.join(',')}>
  {#each rows as r (r.chain + r.txid + r.vout + r.category)}
    <li>
      <button type="button" class="tx" disabled={prefs.mask} title={prefs.mask ? t('Hidden: privacy mode') : t('Show the details')} onclick={() => show(r)} data-recent-tx={r.chain}>
        <ChainBadge id={r.chain} name={r.name} small markOnly />
        <span class="what">
          <span class="addr" translate="no"><SafeText text={r.label || r.address || `${r.txid.slice(0, 16)}…`} /></span>
          <span class="faint small"><SafeText text={r.name} /> · {kind(r)} · {formatTime(r.time)}{#if r.confirmations === 0} · {t('pending')}{/if}</span>
        </span>
        <span class="amt" class:neg={r.amount.startsWith('-')}><ModuleAmount text={r.amount} plus coins /></span>
      </button>
    </li>
  {:else}
    <li class="muted empty">{t('No transactions yet.')}</li>
  {/each}
</ul>

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
  .many-recent {
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
    margin: 0;
    padding: 0;
    list-style: none;
  }
  .tx {
    display: grid;
    grid-template-columns: auto minmax(0, 1fr) auto;
    gap: 0.7rem;
    align-items: center;
    width: 100%;
    padding: 0.5rem 0.8rem;
    border: 1px solid var(--line);
    border-radius: var(--radius-l);
    background: var(--surface-2);
    color: var(--text);
    font: inherit;
    text-align: left;
    cursor: pointer;
  }
  .tx:hover:not(:disabled) {
    background: var(--raised);
  }
  .tx:disabled {
    cursor: default;
  }
  .what {
    display: flex;
    flex-direction: column;
    min-width: 0;
  }
  .addr {
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .small {
    font-size: 0.8rem;
  }
  .amt {
    font-weight: 600;
    color: var(--positive);
  }
  .amt.neg {
    color: var(--text);
  }
  .empty {
    padding: 0.5rem 0.2rem;
  }
  .on {
    display: flex;
    gap: 0.5rem;
    align-items: center;
    margin: 0 0 0.6rem;
  }
</style>
