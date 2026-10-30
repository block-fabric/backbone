<script lang="ts">
  import ChainsOverview from '../components/ChainsOverview.svelte';
  import ManyChainRecent from '../components/ManyChainRecent.svelte';
  import ManyChainBlocks from '../components/ManyChainBlocks.svelte';
  import { ALL, chainEntries, chainState, formatUnits, HOST, parseUnits, refreshSummary, selectedChain } from '../lib/chains.svelte';
  import ChainBalanceChart from '../components/ChainBalanceChart.svelte';
  import ModuleAmount from '../components/ModuleAmount.svelte';
  import SafeText from '../components/SafeText.svelte';
  import { chainColor } from '../lib/chaincolors';
  import { chainBalanceHistory, many as manyState, type ChartSeries } from '../lib/manychains.svelte';
  import { t } from '../lib/i18n.svelte';
  // Overview: the balances of the wallet, and the chain as it grows (overviewpage.cpp, chainactivity.cpp).
  import Amount from '../components/Amount.svelte';
  import Icon from '../components/Icon.svelte';
  import { formatCount } from '../lib/format';
  import { go } from '../lib/nav.svelte';
  import { rpc } from '../lib/rpc';
  import { node, prefs, savePrefs, wallets, walletsEnabled } from '../lib/state.svelte';
  import { coinsToSats } from '../lib/units';
  import BalanceChart from '../components/BalanceChart.svelte';
  import { nav } from '../lib/nav.svelte';
  import { walletApi } from '../lib/wallet';

  /** The number of transactions shown (NUM_ITEMS of the overview of chains-qt). */
  const NUM_ITEMS = 5;

  // The balance chart of the wallet, asked of the host: the overview never loads the whole history (a large wallet
  // would stall it, and every block would send it again).
  let history: { time: number; balance: bigint }[] = $state.raw([]);
  let historyOf: string | null = null;
  $effect(() => {
    void wallets.txVersion;
    const name = wallets.current;
    if (name === null) {
      history = [];
      return;
    }
    const t = setTimeout(
      () => {
        walletApi(name)
          .balanceHistory()
          .then((h) => {
            if (wallets.current === name) {
              history = h;
              historyOf = name;
            }
          })
          .catch(() => undefined);
      },
      historyOf === name ? 300 : 0,
    );
    return () => clearTimeout(t);
  });

  // The chains shown (the top bar's selection, doc/unified-app.md §19): this app's own sections only while it is shown;
  // the lists of transactions, blocks and the mempool of every chain shown (a sidechain's only while it runs).
  const entries = $derived(chainEntries());
  const sel = $derived(selectedChain(entries));
  const shownIds = $derived(entries.filter((e) => (sel === ALL || sel === e.id) && (e.host || e.running)).map((e) => e.id));
  const withHost = $derived(sel === ALL || sel === HOST);
  const many = $derived(shownIds.some((id) => id !== HOST));
  let now = $state(Math.floor(Date.now() / 1000));
  /** One sidechain selected: its entry, its summary, its balance over time. */
  const sideEntry = $derived(sel !== ALL && sel !== HOST ? (entries.find((e) => e.id === sel) ?? null) : null);
  const sideSummary = $derived(sideEntry ? chainState.summaries[sideEntry.id] : undefined);
  const sideTotal = $derived.by(() => {
    const b = sideSummary?.balances;
    if (!b) return null;
    const sum = (parseUnits(b.trusted || '0', 8) ?? 0n) + (parseUnits(b.pending || '0', 8) ?? 0n) + (parseUnits(b.immature || '0', 8) ?? 0n);
    return formatUnits(sum, 8);
  });
  const sideSeries = $derived<ChartSeries[]>(sideEntry ? [{ id: sideEntry.id, name: sideEntry.name, unit: sideEntry.unit, color: chainColor(sideEntry.id), points: chainBalanceHistory(manyState.txs[sideEntry.id] ?? [], sideSummary, now) }] : []);
  $effect(() => {
    const stem = sideEntry?.id;
    if (!stem) return;
    const load = () => void refreshSummary(stem, 4000);
    load();
    const timer = setInterval(() => {
      if (!document.hidden) load();
    }, 8000);
    return () => clearInterval(timer);
  });

  const balances = $derived(wallets.current !== null ? wallets.balances[wallets.current] : undefined);
  const total = $derived(balances ? balances.trusted + balances.untrustedPending + balances.immature : null);

  // The balances of a wallet chosen before any balance event came.
  $effect(() => {
    const name = wallets.current;
    if (name === null || wallets.balances[name]) return;
    rpc
      .call('getbalances', [], { wallet: name })
      .then((b) => {
        if (!wallets.balances[name]) {
          wallets.balances[name] = {
            trusted: coinsToSats(b.mine.trusted),
            untrustedPending: coinsToSats(b.mine.untrusted_pending),
            immature: coinsToSats(b.mine.immature),
          };
        }
      })
      .catch(() => undefined);
  });

  interface BlockRow {
    height: number;
    hash: string;
    time: number;
    txs: number;
  }
  let blocks: BlockRow[] = $state([]);

  $effect(() => {
    const tip = node.tip;
    if (!tip) return;
    const controller = new AbortController();
    (async () => {
      const rows: BlockRow[] = [];
      let hash: string | undefined = tip.hash;
      for (let i = 0; i < 8 && hash; i++) {
        const header: { height: number; time: number; nTx: number; previousblockhash?: string } = await rpc.raw('getblockheader', [hash, true], { signal: controller.signal });
        rows.push({ height: header.height, hash, time: header.time, txs: header.nTx });
        hash = header.previousblockhash;
      }
      blocks = rows;
      now = Math.floor(Date.now() / 1000);
    })().catch(() => undefined);
    return () => controller.abort();
  });

  // The latest transactions waiting in the mempool (ChainActivity): what is known of one is kept until
  // it leaves, and only new ones are looked up.
  interface MempoolRow {
    txid: string;
    time: number;
    fee: number;
    vsize: number;
  }
  const MAX_LOOKUPS = 100;
  let mempoolRows = $state<MempoolRow[]>([]);
  const known = new Map<string, MempoolRow>();
  $effect(() => {
    void node.mempool.size;
    void node.tip?.height;
    const controller = new AbortController();
    (async () => {
      const ids = await rpc.raw<string[]>('getrawmempool', [], { signal: controller.signal });
      const present = new Set(ids);
      for (const id of [...known.keys()]) if (!present.has(id)) known.delete(id);
      const unknown = ids.filter((id) => !known.has(id));
      if (unknown.length > MAX_LOOKUPS) {
        const all = await rpc.raw<Record<string, { time: number; fees: { base: number }; vsize: number }>>('getrawmempool', [true], { signal: controller.signal });
        known.clear();
        for (const [txid, e] of Object.entries(all)) known.set(txid, { txid, time: e.time, fee: e.fees.base, vsize: e.vsize });
      } else {
        for (const txid of unknown) {
          try {
            const e = await rpc.raw<{ time: number; fees: { base: number }; vsize: number }>('getmempoolentry', [txid], { signal: controller.signal });
            known.set(txid, { txid, time: e.time, fee: e.fees.base, vsize: e.vsize });
          } catch {
            // Gone from the mempool meanwhile.
          }
        }
      }
      mempoolRows = [...known.values()].sort((a, b) => b.time - a.time).slice(0, 8);
    })().catch(() => undefined);
    return () => controller.abort();
  });
</script>

<div class="page overview" class:many>
  <header class="page-head">
    <h1>Overview</h1>
    <button type="button" class="btn ghost small" aria-pressed={prefs.mask} title="Mask values (Ctrl+Shift+M)" onclick={() => savePrefs({ mask: !prefs.mask })}>
      <Icon name={prefs.mask ? 'eyeoff' : 'eye'} size={16} />
      {prefs.mask ? 'Values masked' : 'Mask values'}
    </button>
  </header>

  {#if node.tip?.ibd}
    <button type="button" class="out-of-sync" title="The displayed information may be out of date. Your wallet automatically synchronizes with the network after a connection is established, but this process has not completed yet." onclick={() => (nav.dialog = 'sync')}>
      <Icon name="alert" size={16} /> Out of sync: the balances and transactions shown may be out of date until the node has caught up with the network.
    </button>
  {/if}

  {#if sel === ALL && many}
    <!-- Every chain: a row each, in its own coin (doc/unified-app.md §20.2). -->
    <ChainsOverview page="overview" />
  {:else if withHost}
  <section class="balances panel" aria-labelledby="balances-title">
    <div class="panel-head">
      <h2 id="balances-title">Balances</h2>
      {#if wallets.current !== null}<span class="muted">{wallets.current === '' ? 'default wallet' : wallets.current}</span>{/if}
    </div>
    {#if !walletsEnabled()}
      <div class="panel-body empty">
        <p>This node runs without wallets (the option -disablewallet is set).</p>
      </div>
    {:else if wallets.current === null}
      <div class="panel-body empty">
        <p>No wallet is loaded. Create one, or open one of the wallets of this node.</p>
        <button type="button" class="btn primary" onclick={() => go('wallets', 'create')}>Create or open a wallet</button>
      </div>
    {:else}
      <div class="panel-body figures">
        <div class="total">
          <span class="caption">Total</span>
          <span class="big"><Amount sats={total} /></span>
        </div>
        <dl>
          <div>
            <dt title="Your current spendable balance">Available</dt>
            <dd><Amount sats={balances?.trusted} /></dd>
          </div>
          <div>
            <dt title="Total of transactions that have yet to be confirmed, and do not yet count toward the spendable balance">Pending</dt>
            <dd><Amount sats={balances?.untrustedPending} /></dd>
          </div>
          <div>
            <dt title="Mined balance that has not yet matured">Immature</dt>
            <dd><Amount sats={balances?.immature} /></dd>
          </div>
        </dl>
        {#if !prefs.mask}<BalanceChart points={history} />{/if}
      </div>
    {/if}
  </section>
  {:else if sideEntry}
  <!-- One sidechain: its wallet, as the app's own (its balances in its coin, its chart). -->
  <section class="balances panel" aria-labelledby="balances-title" data-side-balances={sideEntry.id}>
    <div class="panel-head">
      <h2 id="balances-title">{t('Balances on %1', sideEntry.name)}</h2>
      {#if sideSummary?.wallet}<span class="muted" translate="no"><SafeText text={sideSummary.wallet} /></span>{/if}
    </div>
    <div class="panel-body figures">
      <div class="total">
        <span class="caption">{t('Total')}</span>
        <span class="big"><ModuleAmount text={sideTotal} unit={sideEntry.unit} coins /></span>
      </div>
      <dl>
        <div><dt>{t('Available')}</dt><dd><ModuleAmount text={sideSummary?.balances?.trusted} unit={sideEntry.unit} coins /></dd></div>
        <div><dt>{t('Pending')}</dt><dd><ModuleAmount text={sideSummary?.balances?.pending} unit={sideEntry.unit} coins /></dd></div>
        <div><dt>{t('Immature')}</dt><dd><ModuleAmount text={sideSummary?.balances?.immature} unit={sideEntry.unit} coins /></dd></div>
      </dl>
      {#if !prefs.mask}<ChainBalanceChart series={sideSeries} height={150} />{/if}
    </div>
  </section>
  {/if}

  <section class="recent panel" aria-labelledby="recent-title">
    <div class="panel-head">
      <h2 id="recent-title">{t('Recent transactions')}</h2>
      <button type="button" class="btn ghost small" disabled={prefs.mask} onclick={() => go('transactions')}>{t('All transactions')}</button>
    </div>
    <div class="panel-body">
      {#if withHost && !many && wallets.current === null}
        <p class="muted">{t('No wallet is loaded.')}</p>
      {:else}
        <ManyChainRecent chains={shownIds} count={NUM_ITEMS + 3} />
      {/if}
    </div>
  </section>

  <section class="chain panel blocks" aria-labelledby="chain-title">
    <div class="panel-head">
      <h2 id="chain-title">{t('Latest blocks')}</h2>
      <button type="button" class="btn ghost small" onclick={() => go('explorer')}>{t('Block explorer')}</button>
    </div>
    <div class="panel-body"><ManyChainBlocks chains={shownIds} hostBlocks={blocks} {now} count={many ? 8 : 6} /></div>
  </section>

  <section class="chain panel waiting" aria-labelledby="mempool-title">
    <div class="panel-head">
      <h2 id="mempool-title">{t('Waiting to be mined')}</h2>
      {#if !many}<span class="muted">{t('%1 waiting', formatCount(node.mempool.size))}</span>{/if}
    </div>
    <div class="panel-body"><ManyChainBlocks chains={shownIds} mode="mempool" hostMempool={mempoolRows.map((m) => ({ ...m, fee: m.fee.toFixed(8) }))} hostWaiting={node.mempool.size} {now} count={5} /></div>
  </section>
</div>

<style>
  /* The balances across the page; the recent transactions at the left, the latest blocks and what waits to be mined at
     the right (doc/unified-app.md §20.4). */
  .overview {
    display: grid;
    grid-template-columns: minmax(0, 1.15fr) minmax(0, 1fr);
    gap: 1rem;
    align-content: start;
    align-items: start;
  }
  .overview :global(.chains-overview),
  .page-head,
  .out-of-sync,
  .balances {
    grid-column: 1 / -1;
    margin: 0;
  }
  .recent {
    grid-row: span 2;
  }
  @media (max-width: 1000px) {
    .overview {
      grid-template-columns: minmax(0, 1fr);
    }
    .recent {
      grid-row: auto;
    }
  }
  .out-of-sync {
    display: flex;
    gap: 0.5rem;
    align-items: center;
    padding: 0.55rem 0.8rem;
    border: 1px solid var(--warning);
    border-radius: var(--radius-m);
    background: var(--warning-soft);
    color: var(--text);
    font: inherit;
    text-align: left;
    cursor: pointer;
  }
  /* Across the page: the total and its parts at the left, the chart at the right. */
  .figures {
    display: grid;
    grid-template-columns: minmax(17rem, 0.9fr) minmax(0, 1.3fr);
    gap: 0.6rem 2rem;
    align-items: center;
  }
  .figures > :global(:last-child:not(dl):not(.total)) {
    grid-column: 2;
    grid-row: 1 / span 2;
  }
  .figures .total,
  .figures dl {
    grid-column: 1;
  }
  .caption {
    display: block;
    color: var(--text-muted);
    font-size: 0.9rem;
  }
  .big {
    font-size: 2.3rem;
    font-weight: 650;
    letter-spacing: -0.02em;
    line-height: 1.15;
  }
  dl {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(10rem, 1fr));
    gap: 0.8rem;
    margin: 0;
    padding-top: 1rem;
    border-top: 1px solid var(--line);
  }
  dt {
    color: var(--text-muted);
    font-size: 0.88rem;
  }
  dd {
    margin: 0.15rem 0 0;
    font-weight: 600;
  }
  .empty p {
    color: var(--text-muted);
  }
  @media (max-width: 980px) {
    .figures {
      grid-template-columns: minmax(0, 1fr);
    }
    .figures > :global(:last-child:not(dl):not(.total)) {
      grid-column: 1;
      grid-row: auto;
    }
  }
</style>
