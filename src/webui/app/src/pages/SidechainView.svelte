<script lang="ts">
  // A sidechain this app runs, chosen in the chain switcher (doc/unified-app.md): its own pages in a frame
  // (§13: Home, Send, Receive, Transactions and its own, through its node; what they spend asked in the app's
  // trusted dialog) once its node runs and speaks this app's version; before (or without its pages built in, or
  // with a node too old or too new) how it is doing, read-only: its height, how it keeps up with Chains, its wallet.
  import Icon from '../components/Icon.svelte';
  import { modules, moduleRoute, NODE_PAGE, openModule } from '../lib/modules.svelte';
  import SafeText from '../components/SafeText.svelte';
  import { formatCount, formatTime } from '../lib/format';
  import { t } from '../lib/i18n.svelte';
  import { go } from '../lib/nav.svelte';
  import { hub, sidechainHost } from '../lib/sidechains.svelte';
  import { describeState, describeSync, type SidechainOverview } from '../lib/unified';

  const entry = $derived(hub.list?.chains.find((c) => c.stem === hub.selected) ?? null);
  let overview = $state<SidechainOverview | null>(null);
  let error = $state('');
  let log = $state<string | null>(null);

  async function load(stem: string) {
    try {
      const next = await sidechainHost.overview(stem);
      if (stem === hub.selected) {
        overview = next;
        error = '';
      }
    } catch (e) {
      error = e instanceof Error ? e.message : String(e);
    }
  }

  $effect(() => {
    const stem = hub.selected;
    if (stem === 'chains') return;
    overview = null;
    log = null;
    void load(stem);
    // Every few seconds while shown, not while the window is hidden.
    const timer = setInterval(() => {
      if (!document.hidden) void load(stem);
    }, 3000);
    return () => clearInterval(timer);
  });

  // Its pages (components/ModuleFrames.svelte): built in, and its node of this app's version (pack.info, kept there).
  const info = $derived(entry ? (modules.info[entry.stem] ?? null) : null);
  const running = $derived(!!entry && entry.state === 'running' && entry.ready);
  const asked = $derived(entry ? moduleRoute(entry.stem).page === NODE_PAGE : false);

  const chainState = $derived(entry ? describeState(entry) : null);
  const sync = $derived(describeSync(overview));
  const coins = (n: number | undefined) => (typeof n === 'number' ? n.toFixed(8) : '—');

  async function showLog() {
    if (!entry) return;
    try {
      const l = await sidechainHost.log(entry.stem);
      log = (l.supervisor ? l.supervisor.split('\n').slice(-8).join('\n') + '\n' : '') + l.node.split('\n').slice(-30).join('\n');
    } catch (e) {
      log = e instanceof Error ? e.message : String(e);
    }
  }
</script>

<div class="page sidechain-view" data-stem={entry?.stem ?? ''}>
  {#if !entry}
    <header class="page-head"><h1>Sidechain</h1></header>
    <p class="muted">This sidechain is not running from this app. Turn it on in Settings → Sidechains.</p>
  {:else}
    <header class="page-head head">
      <div class="title">
        <span class="mark" aria-hidden="true"><SafeText text={entry.name.slice(0, 1)} /></span>
        <div>
          <h1><SafeText text={entry.name} /></h1>
          <p class="muted sub">{t(entry.description)}</p>
        </div>
      </div>
      {#if chainState}<span class="badge state {chainState.tone}" data-state={entry.state}>{t(chainState.key, ...chainState.args)}</span>{/if}
    </header>

    {#if asked && info?.compatible && running}
      <p class="readonly" role="note"><Icon name="eye" size={15} /> Its node. <button type="button" class="btn small" onclick={() => entry && openModule(entry.stem, 'home')}>Its pages</button></p>
    {:else if info && !info.pack}
      <p class="readonly" role="note" data-reason="nopack"><Icon name="eye" size={15} /> Read-only: the pages of this sidechain are not in this version of the app.</p>
    {:else if info && info.reason === 'daemon'}
      <p class="readonly" role="note" data-reason="daemon"><Icon name="eye" size={15} /> Read-only, update needed: the program of this sidechain is not of this version of the app.</p>
    {:else}
      <p class="readonly" role="note"><Icon name="eye" size={15} /> Its pages open once its node runs.</p>
    {/if}

    {#if entry.error}<p class="error" role="alert"><SafeText text={entry.error} /></p>{/if}
    {#if error}<p class="error" role="alert"><SafeText text={error} /></p>{/if}

    <div class="stats">
      <section class="panel stat">
        <div class="caps">Height</div>
        <div class="value num" data-field="height">{overview?.height !== undefined ? formatCount(overview.height) : '—'}</div>
        <div class="muted small">blocks of <SafeText text={entry.name} /></div>
      </section>
      <section class="panel stat">
        <div class="caps">With Chains</div>
        <div class="value sync {sync.tone}" data-field="sync" data-synced={overview?.synced ? 'yes' : 'no'}>{t(sync.key, ...sync.args)}</div>
        <div class="muted small">Chains is at block {formatCount(Math.max(0, overview?.mainchainHeight ?? 0))}</div>
      </section>
      <section class="panel stat">
        <div class="caps">Balance</div>
        <div class="value num" data-field="balance">{coins(overview?.wallet?.trusted)}</div>
        <div class="muted small">
          {#if overview?.wallet}
            wallet “<SafeText text={overview.wallet.name} />”{#if overview.wallet.pending > 0}, {coins(overview.wallet.pending)} on its way{/if}{#if entry.wallet === 'shared'}; its own keys until the single seed comes{/if}
          {:else if overview?.walletError}
            <SafeText text={overview.walletError} />
          {:else}
            no wallet yet
          {/if}
        </div>
      </section>
    </div>

    <section class="panel blocks">
      <div class="panel-head"><h2>Recent blocks</h2></div>
      <div class="panel-body">
        {#if overview?.blocks?.length}
          <table class="table">
            <thead><tr><th>Height</th><th>Time</th><th>Transactions</th><th>Hash</th></tr></thead>
            <tbody>
              {#each overview.blocks as b (b.hash)}
                <tr data-height={b.height}>
                  <td class="num">{formatCount(b.height)}</td>
                  <td>{formatTime(b.time)}</td>
                  <td class="num">{b.txs}</td>
                  <td class="hash" translate="no"><SafeText text={b.hash.slice(0, 16) + '…'} /></td>
                </tr>
              {/each}
            </tbody>
          </table>
        {:else}
          <p class="muted">No blocks to show yet.</p>
        {/if}
      </div>
    </section>

    <section class="panel where">
      <div class="panel-body facts">
        <div><span class="caps">Data</span> <SafeText text={entry.dataDir} /></div>
        <div><span class="caps">Ports</span> {entry.rpcPort} (RPC, this computer only), {entry.port} (peers)</div>
        <div class="row">
          <button type="button" class="btn small" onclick={() => go('settings', 'sidechains')}><Icon name="settings" size={15} /> Settings of the sidechains</button>
          <button type="button" class="btn ghost small" onclick={showLog}><Icon name="text" size={15} /> Show its log</button>
        </div>
        {#if log !== null}<pre class="log"><SafeText text={log} /></pre>{/if}
      </div>
    </section>
  {/if}
</div>

<style>
  .head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 1rem;
  }
  .title {
    display: flex;
    align-items: center;
    gap: 0.8rem;
  }
  .title h1 {
    margin: 0;
  }
  .sub {
    margin: 0.15rem 0 0;
  }
  .mark {
    display: inline-grid;
    place-items: center;
    width: 2.6rem;
    height: 2.6rem;
    border-radius: 0.8rem;
    background: var(--accent);
    color: var(--on-accent);
    font-weight: 800;
    font-size: 1.2rem;
  }
  .state.ok {
    background: var(--accent-soft);
    color: var(--positive);
  }
  .state.warn,
  .state.busy {
    background: var(--warning-soft);
    color: var(--warning);
  }
  .state.bad {
    background: var(--danger-soft);
    color: var(--negative);
  }
  .readonly {
    display: flex;
    align-items: center;
    gap: 0.45rem;
    margin: 0.6rem 0 1rem;
    padding: 0.55rem 0.8rem;
    border: 1px dashed var(--line-strong);
    border-radius: var(--radius-m);
    color: var(--text-muted);
    font-size: 0.9rem;
  }
  .stats {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(13rem, 1fr));
    gap: 0.9rem;
    margin-bottom: 0.9rem;
  }
  .stat {
    padding: 0.9rem 1.1rem;
  }
  .value {
    margin: 0.35rem 0 0.2rem;
    font-size: 1.45rem;
    font-weight: 700;
  }
  .value.sync {
    font-size: 1.05rem;
    line-height: 1.35;
  }
  .sync.ok {
    color: var(--positive);
  }
  .sync.warn {
    color: var(--warning);
  }
  .small {
    font-size: 0.85rem;
  }
  .blocks,
  .where {
    margin-bottom: 0.9rem;
  }
  .table {
    width: 100%;
    border-collapse: collapse;
  }
  .table th,
  .table td {
    padding: 0.4rem 0.5rem;
    border-bottom: 1px solid var(--line);
    text-align: left;
  }
  .hash {
    font-family: var(--font-mono);
    font-size: 0.85rem;
    color: var(--text-muted);
  }
  .facts {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
    padding-top: 1rem;
    overflow-wrap: anywhere;
  }
  .log {
    max-height: 16rem;
    overflow: auto;
    padding: 0.6rem;
    border-radius: var(--radius-m);
    background: var(--surface-2);
    font-size: 0.78rem;
    white-space: pre-wrap;
  }
  .error {
    color: var(--negative);
  }
</style>
