<script lang="ts">
  // The node window (rpcconsole.cpp): information, console, network traffic, peers.
  import { formatBytes, formatCount, formatTime } from '../lib/format';
  import { nav } from '../lib/nav.svelte';
  import { rpc, type BlockchainInfo, type MempoolInfo, type NetworkInfo } from '../lib/rpc';
  import { app, node, wallets } from '../lib/state.svelte';

  const TABS = [
    { id: 'info', label: 'Information' },
    { id: 'console', label: 'Console' },
    { id: 'traffic', label: 'Network traffic' },
    { id: 'peers', label: 'Peers' },
  ];
  const tab = $derived(TABS.some((t) => t.id === nav.tab) ? nav.tab : 'info');

  let chain: BlockchainInfo | null = $state(null);
  let net: NetworkInfo | null = $state(null);
  let mempool: MempoolInfo | null = $state(null);
  let startup: number | null = $state(null);

  $effect(() => {
    void node.tip?.height;
    void node.connections;
    if (tab !== 'info') return;
    Promise.all([rpc.call('getblockchaininfo', []), rpc.call('getnetworkinfo', []), rpc.call('getmempoolinfo', []), rpc.call('uptime', [])])
      .then(([c, n, m, up]) => {
        chain = c;
        net = n;
        mempool = m;
        startup = Math.floor(Date.now() / 1000) - up;
      })
      .catch(() => undefined);
  });
</script>

<div class="page">
  <header class="page-head"><h1>Node window</h1></header>
  <div class="tabs" role="tablist" aria-label="Node window">
    {#each TABS as t (t.id)}
      <button type="button" role="tab" aria-selected={tab === t.id} onclick={() => (nav.tab = t.id)}>{t.label}</button>
    {/each}
  </div>

  {#if tab === 'info'}
    <div class="grid">
      <section class="panel">
        <div class="panel-head"><h2>General</h2></div>
        <dl class="facts panel-body">
          <dt>Client version</dt><dd>{app.info?.version}</dd>
          <dt>User agent</dt><dd class="mono">{net?.subversion ?? '–'}</dd>
          <dt>Datadir</dt><dd class="mono">{app.info?.datadir}</dd>
          <dt>Startup time</dt><dd>{startup ? formatTime(startup) : '–'}</dd>
          <dt>Wallet</dt><dd>{wallets.current === null ? 'None' : wallets.current === '' ? 'default wallet' : wallets.current}</dd>
        </dl>
      </section>
      <section class="panel">
        <div class="panel-head"><h2>Network</h2></div>
        <dl class="facts panel-body">
          <dt>Name</dt><dd>{chain?.chain ?? app.info?.network}</dd>
          <dt>Number of connections</dt>
          <dd>{net ? `${net.connections} (In: ${net.connections_in} / Out: ${net.connections_out})` : node.connections}</dd>
          <dt>Network activity</dt><dd>{node.networkActive ? 'Enabled' : 'Disabled'}</dd>
          <dt>Local addresses</dt><dd class="mono">{net?.localaddresses.length ? net.localaddresses.map((a) => `${a.address}:${a.port}`).join(', ') : 'None'}</dd>
        </dl>
      </section>
      <section class="panel">
        <div class="panel-head"><h2>Block chain</h2></div>
        <dl class="facts panel-body">
          <dt>Current block height</dt><dd class="num">{formatCount(chain?.blocks ?? node.tip?.height ?? 0)}</dd>
          <dt>Last block time</dt><dd>{formatTime(chain?.time ?? node.tip?.time ?? 0)}</dd>
          <dt>Headers</dt><dd class="num">{formatCount(chain?.headers ?? 0)}</dd>
          <dt>Size on disk</dt><dd>{chain ? formatBytes(chain.size_on_disk) : '–'}{chain?.pruned ? ' (pruned)' : ''}</dd>
        </dl>
      </section>
      <section class="panel">
        <div class="panel-head"><h2>Memory pool</h2></div>
        <dl class="facts panel-body">
          <dt>Current number of transactions</dt><dd class="num">{mempool ? formatCount(mempool.size) : '–'}</dd>
          <dt>Memory usage</dt><dd>{mempool ? formatBytes(mempool.usage) : '–'}</dd>
        </dl>
      </section>
    </div>
  {:else}
    <div class="panel planned">
      <p>The {TABS.find((t) => t.id === tab)?.label.toLowerCase()} tab is being rebuilt for the new interface.</p>
    </div>
  {/if}
</div>

<style>
  .grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(24rem, 1fr));
    gap: 1rem;
  }
  .planned {
    padding: 1.5rem;
    color: var(--text-muted);
  }
</style>
