<script lang="ts">
  // The home of the dashboard layout: live cards of the wallet, the node, the sidechains, mining
  // and the chain, each opening its full page, with the quick actions of each. A row of a card
  // (a transaction, a peer, a sidechain, a block) opens the detail panel.
  import { untrack } from 'svelte';
  import Amount from '../../components/Amount.svelte';
  import BalanceChart from '../../components/BalanceChart.svelte';
  import Icon, { type IconName } from '../../components/Icon.svelte';
  import TxStatusIcon from '../../components/TxStatusIcon.svelte';
  import Card from './Card.svelte';
  import { css } from '../../lib/actions';
  import { openPalette, pageDisabled } from '../../lib/commands.svelte';
  import { detail, showDetail } from '../../lib/detail.svelte';
  import { formatCount, formatTime, niceTimeOffset, shortHash } from '../../lib/format';
  import { go, type PageId } from '../../lib/nav.svelte';
  import { host, rpc } from '../../lib/rpc';
  import { app, node, prefs, toast, wallets } from '../../lib/state.svelte';
  import { syncStatus } from '../../lib/sync';
  import { coinsToSats } from '../../lib/units';
  import { addressText, balanceHistory, typeText } from '../../lib/wallet';
  import { current, refreshRecords } from '../../lib/walletdata.svelte';

  interface Peer {
    id: number;
    addr: string;
    inbound: boolean;
    pingtime?: number;
    synced_blocks: number;
  }
  interface Sidechain {
    slot: number;
    title: string;
    escrow?: { amount: number };
    pendingbundles: number;
  }
  interface DrivechainInfo {
    activesidechains: number;
    proposals: number;
    pendingbundles: number;
    escrowtotal: number;
    withdrawalperiod: number;
  }
  interface Bundle {
    slot: number;
    hash: string;
    score: number;
    blocksleft: number;
    payable: boolean;
  }
  interface MiningInfo {
    blocks: number;
    difficulty: number;
    networkhashps: number;
    pooledtx: number;
  }
  interface BlockRow {
    height: number;
    hash: string;
    time: number;
    txs: number;
  }

  /** Blocks looked at for blind merged mining. */
  const BMM_WINDOW = 24;

  let now = $state(Math.floor(Date.now() / 1000));
  let peers: Peer[] = $state([]);
  let blocks: BlockRow[] = $state([]);
  let drivechain: DrivechainInfo | null = $state(null);
  let sidechains: Sidechain[] = $state([]);
  let bundles: Bundle[] = $state([]);
  let bmm: Record<number, number> = $state({});
  let mining: MiningInfo | null = $state(null);

  $effect(() => {
    const timer = setInterval(() => (now = Math.floor(Date.now() / 1000)), 5000);
    return () => clearInterval(timer);
  });

  // --- The wallet ---
  const balances = $derived(wallets.current !== null ? wallets.balances[wallets.current] : undefined);
  const total = $derived(balances ? balances.trusted + balances.untrustedPending + balances.immature : null);
  $effect(() => {
    void wallets.txVersion;
    void node.tip?.hash;
    if (wallets.current !== null) refreshRecords();
  });
  $effect(() => {
    const name = wallets.current;
    if (name === null || wallets.balances[name]) return;
    rpc
      .call('getbalances', [], { wallet: name })
      .then((b) => {
        if (!wallets.balances[name]) {
          wallets.balances[name] = { trusted: coinsToSats(b.mine.trusted), untrustedPending: coinsToSats(b.mine.untrusted_pending), immature: coinsToSats(b.mine.immature) };
        }
      })
      .catch(() => undefined);
  });
  const recent = $derived([...current.records].sort((a, b) => b.time - a.time || b.sortKey.localeCompare(a.sortKey)).slice(0, 5));
  const history = $derived(balanceHistory(current.records));

  // --- The node ---
  const status = $derived(
    node.tip
      ? syncStatus({
          tipHeight: node.tip.height,
          tipTime: node.tip.time,
          progress: node.tip.progress,
          loadingBlocks: node.tip.loadingBlocks,
          headerHeight: node.header?.height ?? node.tip.height,
          headerTime: node.header?.time ?? node.tip.time,
          presyncHeight: node.presync?.height,
          presyncTime: node.presync?.time,
          connections: node.connections,
          networkActive: node.networkActive,
          spacing: app.info?.blockSpacing ?? 60,
          now,
        })
      : null,
  );

  $effect(() => {
    void node.connections;
    const load = () =>
      rpc
        .raw<Peer[]>('getpeerinfo')
        .then((list) => (peers = list))
        .catch(() => undefined);
    load();
    const timer = setInterval(load, 15000);
    return () => clearInterval(timer);
  });

  async function toggleNetwork() {
    try {
      await host.setNetworkActive(!node.networkActive);
    } catch (e) {
      toast('error', 'Could not change network activity', e instanceof Error ? e.message : String(e));
    }
  }

  // --- The chain, the sidechains, mining: after each new block (once the blocks stop coming fast) ---
  async function loadChain(tipHash: string, signal: AbortSignal) {
    const rows: BlockRow[] = [];
    let hash: string | undefined = tipHash;
    for (let i = 0; i < 6 && hash; i++) {
      const h: { height: number; time: number; nTx: number; previousblockhash?: string } = await rpc.raw('getblockheader', [hash, true], { signal });
      rows.push({ height: h.height, hash, time: h.time, txs: h.nTx });
      hash = h.previousblockhash;
    }
    blocks = rows;
    now = Math.floor(Date.now() / 1000);
    mining = await rpc.raw<MiningInfo>('getmininginfo', [], { signal });
    drivechain = await rpc.raw<DrivechainInfo>('getdrivechaininfo', [], { signal });
    sidechains = await rpc.raw<Sidechain[]>('listactivesidechains', [], { signal });
    bundles = await rpc.raw<Bundle[]>('listwithdrawalbundles', [], { signal });
    const height = rows[0]?.height ?? 0;
    const from = Math.max(1, height - BMM_WINDOW + 1);
    const counts: Record<number, number> = {};
    for (const s of sidechains) {
      const events: { bmm?: string }[] = await rpc.raw('getsidechainevents', [s.slot, from, height - from + 1], { signal });
      counts[s.slot] = events.filter((e) => e.bmm).length;
    }
    bmm = counts;
  }

  $effect(() => {
    const tip = node.tip?.hash;
    if (!tip) return;
    const controller = new AbortController();
    const timer = setTimeout(() => loadChain(tip, controller.signal).catch(() => undefined), untrack(() => blocks.length) ? 700 : 0);
    return () => {
      clearTimeout(timer);
      controller.abort();
    };
  });

  const bmmWindow = $derived(Math.min(BMM_WINDOW, blocks[0]?.height ?? 0));

  function hashRate(h: number): string {
    const units = ['H/s', 'kH/s', 'MH/s', 'GH/s', 'TH/s', 'PH/s', 'EH/s'];
    let i = 0;
    while (h >= 1000 && i < units.length - 1) {
      h /= 1000;
      i++;
    }
    return `${h.toFixed(h < 10 ? 2 : 1)} ${units[i]}`;
  }

  const quick: { label: string; icon: IconName; page: PageId; tab?: string }[] = [
    { label: 'Send', icon: 'send', page: 'send' },
    { label: 'Receive', icon: 'receive', page: 'receive' },
    { label: 'Sign message', icon: 'edit', page: 'messages', tab: 'sign' },
    { label: 'Load PSBT', icon: 'paste', page: 'psbt' },
    { label: 'Deposit to a sidechain', icon: 'sidechains', page: 'sidechains' },
    { label: 'Block explorer', icon: 'explorer', page: 'explorer' },
    { label: 'Mine', icon: 'mine', page: 'mine' },
  ];

  const selected = $derived(detail.item);
  function isSelected(kind: string, key: string | number): boolean {
    if (!selected || selected.kind !== kind) return false;
    if (selected.kind === 'tx') return selected.txid === key;
    if (selected.kind === 'block') return selected.hash === key;
    if (selected.kind === 'peer') return selected.id === key;
    return selected.slot === key;
  }
</script>

<div class="page home">
  <header class="page-head">
    <div>
      <h1>Home</h1>
      <p class="muted sub">
        {#if status}{status.upToDate ? 'Up to date' : status.label} · block {formatCount(node.tip?.height ?? 0)} · {node.networkActive ? `${node.connections} peer${node.connections === 1 ? '' : 's'}` : 'network off'}{:else}Starting…{/if}
      </p>
    </div>
  </header>

  <nav class="quick" aria-label="Quick actions">
    {#each quick as q (q.label)}
      <button type="button" class="btn small" disabled={pageDisabled(q.page)} onclick={() => go(q.page, q.tab ?? '')}><Icon name={q.icon} size={16} /> {q.label}</button>
    {/each}
    <button type="button" class="btn ghost small" onclick={openPalette}><Icon name="search" size={16} /> All actions <kbd>Ctrl K</kbd></button>
  </nav>

  {#if node.tip?.ibd}
    <button type="button" class="out-of-sync" onclick={() => go('node', 'info')}>
      <Icon name="alert" size={16} /> Out of sync: balances and activity may be out of date until the node has caught up with the network.
    </button>
  {/if}

  <div class="cards">
    <Card title="Balance" icon="wallet" open={() => go('overview')} openLabel="Open the overview" wide>
      {#if wallets.current === null}
        <p class="muted">No wallet is loaded. Create one, or open one of the wallets of this node.</p>
        <button type="button" class="btn primary" onclick={() => go('wallets', 'create')}>Create or open a wallet</button>
      {:else}
        <div class="balance">
          <div>
            <span class="caption">Total{wallets.list.length > 1 ? ` · ${wallets.current || 'default wallet'}` : ''}</span>
            <span class="big"><Amount sats={total} /></span>
            <dl class="trio">
              <div><dt>Available</dt><dd><Amount sats={balances?.trusted} /></dd></div>
              <div><dt>Pending</dt><dd><Amount sats={balances?.untrustedPending} /></dd></div>
              <div><dt>Immature</dt><dd><Amount sats={balances?.immature} /></dd></div>
            </dl>
          </div>
          {#if !prefs.mask && history.length > 1}<div class="chart"><BalanceChart points={history} /></div>{/if}
        </div>
      {/if}
      {#snippet actions()}
        <button type="button" class="btn primary" disabled={pageDisabled('send')} onclick={() => go('send')}><Icon name="send" size={16} /> Send</button>
        <button type="button" class="btn" disabled={pageDisabled('receive')} onclick={() => go('receive')}><Icon name="receive" size={16} /> Receive</button>
        <button type="button" class="btn ghost" disabled={pageDisabled('transactions')} onclick={() => go('transactions')}>History</button>
        <button type="button" class="btn ghost" onclick={() => go('wallets')}>Wallets</button>
      {/snippet}
    </Card>

    <Card title="Sync and peers" icon="peers" open={() => go('node', 'info')} openLabel="Open the node window">
      {#if status}
        <div class="sync" class:ok={status.upToDate}>
          <Icon name={status.upToDate ? 'check' : 'history'} size={16} />
          <span>{status.upToDate ? 'Up to date' : status.label}</span>
          {#if !status.upToDate}<span class="muted num">{status.behind}</span>{/if}
        </div>
        <progress max="1" value={status.upToDate ? 1 : status.progress} aria-label="Verification progress"></progress>
      {/if}
      <dl class="pairs">
        <dt>Blocks</dt><dd class="num">{formatCount(node.tip?.height ?? 0)}</dd>
        <dt>Headers</dt><dd class="num">{formatCount(node.header?.height ?? node.tip?.height ?? 0)}</dd>
        <dt>Peers</dt><dd class="num">{node.networkActive ? node.connections : 'network off'}</dd>
      </dl>
      {#if peers.length}
        <ul class="rows" aria-label="Peers">
          {#each peers.slice(0, 4) as p (p.id)}
            <li>
              <button type="button" class="row-btn" aria-pressed={isSelected('peer', p.id)} onclick={() => showDetail({ kind: 'peer', id: p.id })}>
                <span class="mono grow ellipsis">{p.addr}</span>
                <span class="badge">{p.inbound ? 'in' : 'out'}</span>
                <span class="num muted">{p.pingtime !== undefined ? `${Math.round(p.pingtime * 1000)} ms` : '–'}</span>
              </button>
            </li>
          {/each}
        </ul>
      {/if}
      {#snippet actions()}
        <button type="button" class="btn" onclick={() => go('node', 'peers')}>Peers</button>
        <button type="button" class="btn ghost" onclick={toggleNetwork}>{node.networkActive ? 'Disable network' : 'Enable network'}</button>
        <button type="button" class="btn ghost" onclick={() => go('node', 'console')}>Console</button>
      {/snippet}
    </Card>

    <Card title="Sidechains" icon="sidechains" open={() => go('sidechains')} openLabel="Open the sidechains">
      <dl class="pairs">
        <dt>Active</dt><dd class="num">{drivechain?.activesidechains ?? '–'}</dd>
        <dt>In escrow</dt><dd>{#if drivechain}<Amount sats={coinsToSats(drivechain.escrowtotal)} />{:else}–{/if}</dd>
        <dt>Pending bundles</dt><dd class="num">{drivechain?.pendingbundles ?? '–'}</dd>
        <dt>Proposals</dt><dd class="num">{drivechain?.proposals ?? '–'}</dd>
      </dl>
      {#if sidechains.length === 0}
        <p class="muted">{drivechain ? 'No sidechain is active yet.' : 'Loading…'}</p>
      {:else}
        <ul class="rows" aria-label="Active sidechains">
          {#each sidechains as s (s.slot)}
            {@const pending = bundles.filter((b) => b.slot === s.slot)}
            <li>
              <button type="button" class="row-btn" aria-pressed={isSelected('sidechain', s.slot)} onclick={() => showDetail({ kind: 'sidechain', slot: s.slot })}>
                <span class="slot num">{s.slot}</span>
                <span class="grow ellipsis">{s.title}</span>
                {#if pending.length}<span class="badge warning" title="Withdrawal bundles being voted on">{pending.length} bundle{pending.length === 1 ? '' : 's'}</span>{/if}
                <span class="muted"><Amount sats={coinsToSats(s.escrow?.amount ?? 0)} showUnit={false} /></span>
              </button>
            </li>
          {/each}
        </ul>
      {/if}
      {#snippet actions()}
        <button type="button" class="btn" disabled={pageDisabled('sidechains')} onclick={() => go('sidechains')}>Deposit</button>
        <button type="button" class="btn ghost" disabled={pageDisabled('sidechains')} onclick={() => go('sidechains', 'proposals')}>Proposals and votes</button>
      {/snippet}
    </Card>

    <Card title="Blind merged mining" icon="nodes" open={() => go('sidechain-nodes')} openLabel="Open the sidechain nodes">
      <p class="muted small">Blocks of the last {bmmWindow} that committed to a block of each sidechain.</p>
      {#if sidechains.length === 0}
        <p class="muted">No sidechain to merge mine yet.</p>
      {:else}
        <ul class="meters">
          {#each sidechains as s (s.slot)}
            {@const n = bmm[s.slot] ?? 0}
            <li>
              <span class="grow ellipsis">{s.title}</span>
              <span class="meter" aria-hidden="true"><span use:css={{ width: `${bmmWindow ? (100 * n) / bmmWindow : 0}%` }}></span></span>
              <span class="num">{n}/{bmmWindow}</span>
            </li>
          {/each}
        </ul>
      {/if}
      {#snippet actions()}
        <button type="button" class="btn" onclick={() => go('sidechain-nodes')}>Sidechain nodes</button>
      {/snippet}
    </Card>

    <Card title="Mining" icon="mine" open={() => go('mine')} openLabel="Open mining">
      <dl class="pairs">
        <dt>Difficulty</dt><dd class="num">{mining ? mining.difficulty.toPrecision(5) : '–'}</dd>
        <dt>Network hash rate</dt><dd class="num">{mining ? hashRate(mining.networkhashps) : '–'}</dd>
        <dt>Waiting transactions</dt><dd class="num">{mining ? formatCount(mining.pooledtx) : '–'}</dd>
        <dt>Last block</dt><dd>{blocks[0] ? `${niceTimeOffset(Math.max(0, now - blocks[0].time))} ago` : '–'}</dd>
      </dl>
      {#snippet actions()}
        <button type="button" class="btn" onclick={() => go('mine')}>Solo mine</button>
        <button type="button" class="btn ghost" onclick={() => go('timestamp')}>Timestamp a file</button>
      {/snippet}
    </Card>

    <Card title="Recent activity" icon="history" open={() => go('transactions')} openLabel="Open the transactions">
      {#if wallets.current === null}
        <p class="muted">No wallet loaded.</p>
      {:else if recent.length === 0}
        <p class="muted">{current.recordsLoaded ? 'No transactions yet.' : 'Loading…'}</p>
      {:else}
        <ul class="rows" aria-label="Recent transactions">
          {#each recent as r (r.key)}
            <li>
              <button
                type="button"
                class="row-btn tx"
                aria-pressed={isSelected('tx', r.txid)}
                disabled={prefs.mask}
                title={prefs.mask ? 'Hidden: privacy mode' : 'Show the details'}
                onclick={() => wallets.current !== null && showDetail({ kind: 'tx', wallet: wallets.current, txid: r.txid, idx: r.idx, address: r.address })}
              >
                <TxStatusIcon record={r} />
                <span class="grow what">
                  <span class="ellipsis">{addressText(r)}</span>
                  <span class="faint small">{typeText(r.type) || 'Other'} · {formatTime(r.time)}</span>
                </span>
                <Amount sats={r.amount} colored plus />
              </button>
            </li>
          {/each}
        </ul>
      {/if}
    </Card>

    <Card title="Latest blocks" icon="block" open={() => go('explorer')} openLabel="Open the block explorer">
      {#if blocks.length === 0}
        <p class="muted">Waiting for the node…</p>
      {:else}
        <ul class="rows" aria-label="Latest blocks">
          {#each blocks as b (b.hash)}
            <li>
              <button type="button" class="row-btn" aria-pressed={isSelected('block', b.hash)} onclick={() => showDetail({ kind: 'block', hash: b.hash })}>
                <span class="num height">{formatCount(b.height)}</span>
                <span class="mono muted grow ellipsis">{shortHash(b.hash, 6)}</span>
                <span class="num small">{b.txs} tx{b.txs === 1 ? '' : 's'}</span>
                <span class="muted small age">{niceTimeOffset(Math.max(0, now - b.time))} ago</span>
              </button>
            </li>
          {/each}
        </ul>
      {/if}
    </Card>
  </div>
</div>

<style>
  .home {
    display: flex;
    flex-direction: column;
    gap: 1rem;
  }
  .sub {
    margin: 0.2rem 0 0;
  }
  .quick {
    display: flex;
    flex-wrap: wrap;
    gap: 0.45rem;
  }
  .quick kbd {
    margin-left: 0.2rem;
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
  .cards {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(21rem, 1fr));
    gap: 1rem;
    align-items: stretch;
  }
  .balance {
    display: flex;
    flex-wrap: wrap;
    gap: 1rem 2rem;
    align-items: flex-end;
  }
  .balance > div:first-child {
    flex: 1 1 18rem;
  }
  .chart {
    flex: 1 1 16rem;
    min-width: 0;
  }
  .caption {
    display: block;
    color: var(--text-muted);
    font-size: 0.9rem;
  }
  .big {
    display: block;
    font-size: 2.2rem;
    font-weight: 650;
    letter-spacing: -0.02em;
    line-height: 1.2;
  }
  .trio {
    display: flex;
    flex-wrap: wrap;
    gap: 0.6rem 1.6rem;
    margin: 0.7rem 0 0;
  }
  .trio dt,
  .pairs dt {
    color: var(--text-muted);
    font-size: 0.86rem;
  }
  .trio dd {
    margin: 0;
    font-weight: 600;
  }
  .pairs {
    display: grid;
    grid-template-columns: minmax(0, 1fr) auto;
    gap: 0.3rem 1rem;
    margin: 0.4rem 0 0.6rem;
  }
  .pairs dd {
    margin: 0;
    text-align: right;
    font-weight: 600;
  }
  .sync {
    display: flex;
    align-items: center;
    gap: 0.45rem;
    font-weight: 600;
  }
  .sync.ok {
    color: var(--positive);
  }
  progress {
    width: 100%;
    height: 0.4rem;
    margin: 0.5rem 0 0.2rem;
    appearance: none;
    border: 0;
    border-radius: 999px;
    background: var(--raised);
    overflow: hidden;
  }
  progress::-webkit-progress-bar {
    background: var(--raised);
  }
  progress::-webkit-progress-value {
    background: var(--accent);
  }
  .rows,
  .meters {
    margin: 0;
    padding: 0;
    list-style: none;
  }
  .row-btn {
    display: flex;
    align-items: center;
    gap: 0.6rem;
    width: 100%;
    min-width: 0;
    padding: 0.42rem 0.4rem;
    border: 0;
    border-top: 1px solid var(--line);
    background: none;
    color: var(--text);
    font: inherit;
    text-align: left;
    cursor: pointer;
  }
  .row-btn:hover:not(:disabled) {
    background: var(--raised);
  }
  .row-btn[aria-pressed='true'] {
    background: var(--accent-soft);
    box-shadow: inset 3px 0 0 var(--accent);
  }
  .row-btn:disabled {
    cursor: default;
  }
  .what {
    display: flex;
    flex-direction: column;
  }
  .ellipsis {
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .slot {
    min-width: 1.6rem;
    padding: 0.05rem 0.35rem;
    border-radius: var(--radius-s);
    background: var(--raised);
    text-align: center;
    font-weight: 650;
  }
  .height {
    min-width: 4.2rem;
    font-weight: 650;
  }
  .age {
    min-width: 5.5rem;
    text-align: right;
  }
  .small {
    font-size: 0.84rem;
  }
  .meters li {
    display: flex;
    align-items: center;
    gap: 0.7rem;
    padding: 0.4rem 0;
  }
  .meter {
    width: 7rem;
    height: 0.5rem;
    border-radius: 999px;
    background: var(--raised);
    overflow: hidden;
  }
  .meter span {
    display: block;
    height: 100%;
    background: var(--accent);
  }
  @media (max-width: 560px) {
    .cards {
      grid-template-columns: minmax(0, 1fr);
    }
    .quick .btn {
      flex: 1 1 45%;
    }
  }
</style>
