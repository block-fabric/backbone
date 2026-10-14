<script lang="ts">
  // Overview: the balances of the wallet, and the chain as it grows (overviewpage.cpp, chainactivity.cpp).
  import Amount from '../components/Amount.svelte';
  import Icon from '../components/Icon.svelte';
  import { formatCount, formatTime, niceTimeOffset, shortHash } from '../lib/format';
  import { go } from '../lib/nav.svelte';
  import { rpc } from '../lib/rpc';
  import { node, prefs, savePrefs, wallets } from '../lib/state.svelte';
  import { coinsToSats } from '../lib/units';

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
  let now = $state(Math.floor(Date.now() / 1000));

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
</script>

<div class="page overview">
  <header class="page-head">
    <h1>Overview</h1>
    <button type="button" class="btn ghost small" aria-pressed={prefs.mask} title="Mask values (Ctrl+Shift+M)" onclick={() => savePrefs({ mask: !prefs.mask })}>
      <Icon name={prefs.mask ? 'eyeoff' : 'eye'} size={16} />
      {prefs.mask ? 'Values masked' : 'Mask values'}
    </button>
  </header>

  <section class="balances panel" aria-labelledby="balances-title">
    <div class="panel-head">
      <h2 id="balances-title">Balances</h2>
      {#if wallets.current !== null}<span class="muted">{wallets.current === '' ? 'default wallet' : wallets.current}</span>{/if}
    </div>
    {#if wallets.current === null}
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
      </div>
    {/if}
  </section>

  <section class="chain panel" aria-labelledby="chain-title">
    <div class="panel-head">
      <h2 id="chain-title">Latest blocks</h2>
      <button type="button" class="btn ghost small" onclick={() => go('explorer')}>Block explorer</button>
    </div>
    <div class="panel-body">
      {#if blocks.length === 0}
        <p class="muted">Waiting for the node…</p>
      {:else}
        <ol class="blocks">
          {#each blocks as b (b.hash)}
            <li>
              <span class="height num">{formatCount(b.height)}</span>
              <span class="mono hash" title={b.hash}>{shortHash(b.hash, 6)}</span>
              <span class="num txs">{b.txs} tx{b.txs === 1 ? '' : 's'}</span>
              <time class="muted" datetime={new Date(b.time * 1000).toISOString()} title={formatTime(b.time)}>{niceTimeOffset(now - b.time)} ago</time>
            </li>
          {/each}
        </ol>
      {/if}
    </div>
  </section>
</div>

<style>
  .overview {
    display: grid;
    grid-template-columns: minmax(0, 1.2fr) minmax(0, 1fr);
    gap: 1rem;
    align-content: start;
    align-items: start;
  }
  .page-head {
    grid-column: 1 / -1;
  }
  .figures {
    display: flex;
    flex-direction: column;
    gap: 1.1rem;
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
    grid-template-columns: repeat(3, minmax(0, 1fr));
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
  .blocks {
    margin: 0;
    padding: 0;
    list-style: none;
  }
  .blocks li {
    display: grid;
    grid-template-columns: 5.5rem minmax(0, 1fr) 4.5rem 8.5rem;
    gap: 0.6rem;
    align-items: center;
    padding: 0.45rem 0;
    border-bottom: 1px solid var(--line);
  }
  .blocks li:last-child {
    border-bottom: 0;
  }
  .height {
    font-weight: 650;
  }
  .hash {
    color: var(--text-muted);
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .txs {
    text-align: right;
  }
  time {
    text-align: right;
    font-size: 0.88rem;
  }
  @media (max-width: 980px) {
    .overview {
      grid-template-columns: minmax(0, 1fr);
    }
    dl {
      grid-template-columns: minmax(0, 1fr);
    }
  }
</style>
