<script lang="ts">
  // Transactions of another chain than the app's, or of every chain together (doc/unified-app.md §14: the filter of
  // chains on Transactions). A module's are read from its node (its wallet's listtransactions, a read the host allows);
  // a row of a module opens that module's own Transactions, where its details and actions are.
  import { t } from '../lib/i18n.svelte';
  import SafeText from './SafeText.svelte';
  import ModuleAmount from './ModuleAmount.svelte';
  import { formatTime } from '../lib/format';
  import { hub, sidechainHost } from '../lib/sidechains.svelte';
  import { chainTxs, mergeChainTxs, modules, moduleName, openModule, productName, type ChainTx } from '../lib/modules.svelte';
  import { wallets } from '../lib/state.svelte';
  import { current } from '../lib/walletdata.svelte';
  import { go } from '../lib/nav.svelte';
  import { satsToCoins } from '../lib/units';

  let { chain }: { chain: string } = $props();

  let lists = $state<Record<string, ChainTx[]>>({});
  let errors = $state<Record<string, string>>({});
  let loading = $state(true);

  const stems = $derived(chain === 'all' ? (hub.list?.chains ?? []).filter((c) => c.enabled && c.state === 'running').map((c) => c.stem) : [chain]);

  async function load(stem: string) {
    try {
      let wallet = modules.overview[stem]?.wallet?.name;
      if (!wallet) {
        const o = await sidechainHost.overview(stem);
        modules.overview[stem] = o;
        wallet = o.wallet?.name;
      }
      const listed = await sidechainHost.rpc<unknown>(stem, 'listtransactions', ['*', 200], wallet);
      lists[stem] = chainTxs(stem, moduleName(stem), listed);
      errors[stem] = '';
    } catch (e) {
      errors[stem] = e instanceof Error ? e.message : String(e);
    }
  }

  $effect(() => {
    const list = stems;
    loading = true;
    void Promise.all(list.map(load)).finally(() => (loading = false));
  });

  // The app's own, in the same rows (only for every chain together).
  const own = $derived<ChainTx[]>(
    chain === 'all'
      ? current.records.map((r) => ({ chain: 'chains', name: productName(), txid: r.txid, time: r.time, category: r.type, amount: Number(satsToCoins(r.amount)), address: r.address, label: r.label, confirmations: r.depth }))
      : [],
  );
  const rows = $derived(mergeChainTxs([own, ...stems.map((s) => lists[s] ?? [])]).slice(0, 500));

  function open(r: ChainTx) {
    if (r.chain === 'chains') go('transactions');
    else openModule(r.chain, 'transactions');
  }
</script>

<div class="chain-activity" data-chain-activity={chain}>
  {#each stems as s (s)}
    {#if errors[s]}<p class="error" role="alert"><SafeText text={moduleName(s)} />: <SafeText text={errors[s]} /></p>{/if}
  {/each}
  <table class="table">
    <thead>
      <tr><th>{t('Chain')}</th><th>{t('Date')}</th><th>{t('Type')}</th><th>{t('Label')}</th><th class="num">{t('Amount')}</th></tr>
    </thead>
    <tbody>
      {#each rows as r (r.chain + r.txid + r.category + r.address + r.amount)}
        <tr data-chain-tx={r.chain} ondblclick={() => open(r)}>
          <td><button type="button" class="chip" onclick={() => open(r)} title={t('Open in %1', r.name)}><SafeText text={r.name} /></button></td>
          <td>{formatTime(r.time)}</td>
          <td><SafeText text={r.category} /></td>
          <td class="addr" translate="no"><SafeText text={r.label || r.address || r.txid.slice(0, 16) + '…'} /></td>
          <td class="num amt" class:neg={r.amount < 0} translate="no"><ModuleAmount text={r.amount.toFixed(8)} plus /></td>
        </tr>
      {:else}
        <tr><td colspan="5" class="muted">{loading ? t('Loading…') : t('No transactions yet')}</td></tr>
      {/each}
    </tbody>
  </table>
  {#if wallets.current === null && chain === 'all'}<p class="faint">{t('No wallet of %1 is loaded: its transactions are not in the list.', productName())}</p>{/if}
</div>

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
  .chip {
    padding: 0.1rem 0.55rem;
    border: 1px solid var(--line);
    border-radius: 999px;
    background: var(--surface-2);
    color: var(--text);
    font: inherit;
    font-size: 0.82rem;
    cursor: pointer;
  }
  .error {
    color: var(--negative);
  }
</style>
