<script lang="ts">
  // The block explorer with every chain (doc/unified-app.md §20.5): one search for a height, a hash, a transaction id or
  // an address on every chain at once, each result named by its chain and opening that chain's own explorer at it; then
  // the newest blocks of every chain in one table, and what waits to be mined on each. This app's own chain is searched
  // by its own node, a sidechain by its node (chain.search: reads only, no index).
  import { untrack } from 'svelte';
  import { t } from '../lib/i18n.svelte';
  import Icon from './Icon.svelte';
  import ChainBadge from './ChainBadge.svelte';
  import SafeText from './SafeText.svelte';
  import ManyChainBlocks from './ManyChainBlocks.svelte';
  import { formatCount, shortHash } from '../lib/format';
  import { request } from '../lib/bridge';
  import { rpc } from '../lib/rpc';
  import { node } from '../lib/state.svelte';
  import { every } from '../lib/poll';
  import { go, nav } from '../lib/nav.svelte';
  import { productName } from '../lib/modules.svelte';
  import { mayBeAddress } from '../lib/explorer';
  import { chainEntries, HOST, setSelectedChain } from '../lib/chains.svelte';

  interface Found {
    chain: string;
    name: string;
    kind: 'block' | 'tx' | 'address';
    id: string;
    height?: number;
    mine?: boolean;
    confirmations?: number;
  }

  const entries = $derived(chainEntries());
  const shownIds = $derived(entries.filter((e) => e.host || e.running).map((e) => e.id));
  let query = $state('');
  let searching = $state(false);
  let searched = $state('');
  let found = $state<Found[]>([]);
  let now = $state(Math.floor(Date.now() / 1000));
  $effect(() => every(15000, () => (now = Math.floor(Date.now() / 1000))));

  // This app's own chain: its node.
  interface Header {
    height: number;
    hash: string;
    time: number;
    nTx: number;
    previousblockhash?: string;
  }
  let blocks = $state<{ height: number; hash: string; time: number; txs: number }[]>([]);
  $effect(() => {
    const tip = node.tip?.hash;
    if (!tip) return;
    const controller = new AbortController();
    (async () => {
      const rows: { height: number; hash: string; time: number; txs: number }[] = [];
      let hash: string | undefined = tip;
      for (let i = 0; i < 6 && hash; i++) {
        const h: Header = await rpc.raw('getblockheader', [hash, true], { signal: controller.signal });
        rows.push({ height: h.height, hash, time: h.time, txs: h.nTx });
        hash = h.previousblockhash;
      }
      blocks = rows;
    })().catch(() => undefined);
    return () => controller.abort();
  });
  let mempool = $state<{ txid: string; time: number; fee: string; vsize: number }[]>([]);
  $effect(() => {
    void node.mempool.size;
    void node.tip?.height;
    rpc
      .raw<Record<string, { time: number; fees: { base: number }; vsize: number }>>('getrawmempool', [true])
      .then((all) => {
        mempool = Object.entries(all)
          .map(([txid, e]) => ({ txid, time: e.time, fee: e.fees.base.toFixed(8), vsize: e.vsize }))
          .sort((a, b) => b.time - a.time)
          .slice(0, 8);
      })
      .catch(() => undefined);
  });

  /** What this app's own node knows of a search. */
  async function searchHost(q: string): Promise<Found[]> {
    const name = productName();
    const out: Found[] = [];
    if (/^\d{1,9}$/.test(q)) {
      const hash = await rpc.call('getblockhash', [Number(q)]).catch(() => null);
      if (typeof hash === 'string') out.push({ chain: HOST, name, kind: 'block', id: hash, height: Number(q) });
    } else if (/^[0-9a-fA-F]{64}$/.test(q)) {
      const h = await rpc.raw<{ height: number }>('getblockheader', [q.toLowerCase(), true]).catch(() => null);
      if (h) out.push({ chain: HOST, name, kind: 'block', id: q.toLowerCase(), height: h.height });
      else {
        const tx = await rpc.raw<{ txid: string; confirmations?: number }>('getrawtransaction', [q.toLowerCase(), 1]).catch(() => null);
        if (tx?.txid) out.push({ chain: HOST, name, kind: 'tx', id: tx.txid, confirmations: tx.confirmations ?? 0 });
      }
    } else if (mayBeAddress(q)) {
      const v = await rpc.call('validateaddress', [q]).catch(() => null);
      if (v?.isvalid) out.push({ chain: HOST, name, kind: 'address', id: q });
    }
    return out;
  }

  async function search() {
    const q = query.trim();
    if (!q) return;
    searching = true;
    searched = q;
    found = [];
    const asks = shownIds.map((id) =>
      id === HOST
        ? searchHost(q)
        : request<{ results: { kind: 'block' | 'tx' | 'address'; hash?: string; txid?: string; address?: string; height?: number; mine?: boolean; confirmations?: number }[] }>('host', 'chain.search', { stem: id, q }).then(
            (r) =>
              r.results.map((x) => ({ chain: id, name: entries.find((e) => e.id === id)?.name ?? id, kind: x.kind, id: x.hash ?? x.txid ?? x.address ?? '', height: x.height, mine: x.mine, confirmations: x.confirmations })),
            () => [] as Found[],
          ),
    );
    const lists = await Promise.all(asks);
    found = lists.flat();
    searching = false;
  }
  // A search given with the page ("search:<q>", the palette): over every chain; taken, as the other explorers take theirs.
  $effect(() => {
    const asked = nav.tab;
    if (!asked.startsWith('search:')) return;
    nav.tab = '';
    query = asked.slice('search:'.length);
    untrack(() => void search());
  });

  function open(f: Found) {
    setSelectedChain(f.chain);
    if (f.chain === HOST) go('explorer', `${f.kind === 'block' ? 'block' : f.kind}:${f.id}`);
    else go('explorer', `search:${f.id}`);
  }
  const kindText = (f: Found) => (f.kind === 'block' ? t('Block %1', formatCount(f.height ?? 0)) : f.kind === 'tx' ? t('Transaction') : t('Address'));
</script>

<div class="page all-explorer">
  <header class="page-head"><h1>{t('Block explorer')}</h1></header>
  <form class="search" onsubmit={(e) => { e.preventDefault(); void search(); }}>
    <div class="box grow">
      <Icon name="search" size={17} />
      <input type="search" class="mono" placeholder={t('Height, block hash, transaction id or address, on every chain')} spellcheck="false" autocomplete="off" bind:value={query} aria-label={t('Search every chain')} data-all-search />
    </div>
    <button type="submit" class="btn primary" disabled={searching || !query.trim()}>{searching ? t('Searching…') : t('Search')}</button>
  </form>

  {#if searched}
    <section class="panel results" aria-label={t('Found')} data-all-results={found.length}>
      <div class="panel-body">
        {#if searching}
          <p class="muted">{t('Searching every chain…')}</p>
        {:else if found.length === 0}
          <p class="muted">{t('Nothing on any chain matches “%1”.', searched)}</p>
          <p class="faint small">{t('A transaction that is not in a block, nor in your wallet, is only found by a node that keeps an index of transactions.')}</p>
        {:else}
          <table class="rows">
            <tbody>
              {#each found as f (f.chain + f.kind + f.id)}
                <tr class="clickable" onclick={() => open(f)} data-found={`${f.chain}:${f.kind}`}>
                  <td><ChainBadge id={f.chain} name={f.name} /></td>
                  <td class="what">{kindText(f)}{#if f.mine}<span class="badge accent">{t('yours')}</span>{/if}{#if f.kind === 'tx' && f.confirmations === 0}<span class="badge">{t('waiting')}</span>{/if}</td>
                  <td class="mono faint" translate="no"><SafeText text={shortHash(f.id, 10)} /></td>
                  <td class="end"><span class="link">{t('Open')} <Icon name="arrow" size={14} /></span></td>
                </tr>
              {/each}
            </tbody>
          </table>
        {/if}
      </div>
    </section>
  {/if}

  <div class="lists">
    <section class="panel" aria-labelledby="all-blocks-title">
      <div class="panel-head"><h2 id="all-blocks-title">{t('Latest blocks')}</h2><span class="muted small">{t('Each chain\'s newest first')}</span></div>
      <div class="panel-body"><ManyChainBlocks chains={shownIds} hostBlocks={blocks} {now} count={12} perChain={3} /></div>
    </section>
    <section class="panel" aria-labelledby="all-waiting-title">
      <div class="panel-head"><h2 id="all-waiting-title">{t('Waiting to be mined')}</h2></div>
      <div class="panel-body"><ManyChainBlocks chains={shownIds} mode="mempool" hostMempool={mempool} hostWaiting={node.mempool.size} {now} count={8} /></div>
    </section>
  </div>
</div>

<style>
  .all-explorer {
    display: flex;
    flex-direction: column;
    gap: 1rem;
  }
  .search {
    display: flex;
    gap: 0.6rem;
    align-items: center;
  }
  .box {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    padding: 0 0.6rem;
    border: 1px solid var(--line-strong);
    border-radius: var(--radius-s);
    background: var(--surface);
    color: var(--text-muted);
  }
  .box input {
    border: 0;
    padding-left: 0;
    background: transparent;
  }
  .box input:focus-visible {
    outline: none;
  }
  .box:has(input:focus-visible) {
    outline: 2px solid var(--focus);
    outline-offset: 2px;
  }
  .results .panel-body {
    padding-top: 1rem;
  }
  .results p {
    margin: 0;
  }
  .what {
    font-weight: 600;
  }
  .what .badge {
    margin-left: 0.5rem;
  }
  .link {
    display: inline-flex;
    align-items: center;
    gap: 0.25rem;
    color: var(--link);
  }
  .lists {
    display: grid;
    grid-template-columns: minmax(0, 1.2fr) minmax(0, 1fr);
    gap: 1rem;
    align-items: start;
  }
  .small {
    font-size: 0.82rem;
  }
  @media (max-width: 1100px) {
    .lists {
      grid-template-columns: minmax(0, 1fr);
    }
  }
</style>
