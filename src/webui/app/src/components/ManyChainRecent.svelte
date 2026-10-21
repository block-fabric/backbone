<script lang="ts">
  // The latest transactions of the chains shown (doc/unified-app.md §20.2: Home, Overview), newest first: each named in
  // words (the label you gave, else what happened), the chain and the address on the line below, the amount in the
  // chain's coin; a chain's mining rewards folded into one row ("Mining rewards: 9"), so that they do not bury the
  // payments. This app's own from its wallet, a sidechain's from its node (chain.transactions). A row opens its details
  // here, in the app; the rewards open that chain's transactions. Hidden in privacy mode.
  import { t } from '../lib/i18n.svelte';
  import SafeText from './SafeText.svelte';
  import ModuleAmount from './ModuleAmount.svelte';
  import ChainBadge from './ChainBadge.svelte';
  import Dialog from './Dialog.svelte';
  import Icon from './Icon.svelte';
  import TxDetailsView from './TxDetailsView.svelte';
  import { ageText, shortHash } from '../lib/format';
  import { foldActivity, whatWords, type ActivityItem } from '../lib/activity';
  import { chainHost, chainLabel, chainWallet, formatUnits, HOST, mergeTxs, setSelectedChain, sidechainTxs, slotLabel, type ManyChainTx } from '../lib/chains.svelte';
  import { many, poll, refreshTxs } from '../lib/manychains.svelte';
  import { openModule, productName } from '../lib/modules.svelte';
  import { go } from '../lib/nav.svelte';
  import { node, prefs, wallets } from '../lib/state.svelte';
  import { every } from '../lib/poll';
  import { walletApi, type TxDetails, type TxRecord } from '../lib/wallet';
  import { throttled } from '../lib/throttle.svelte';

  let { chains, count = 8 }: { chains: string[]; count?: number } = $props();

  let own: TxRecord[] = $state.raw([]);
  const withHost = $derived(chains.includes(HOST));
  const sides = $derived(chains.filter((c) => c !== HOST));
  /** The chain's name on each row only when several chains are shown. */
  const several = $derived(chains.length > 1);

  // Enough of the wallet's own to fill the list once its rewards are folded.
  $effect(() => {
    void wallets.txVersion;
    void node.tip?.hash;
    const name = wallets.current;
    if (!withHost || name === null) {
      own = [];
      return;
    }
    walletApi(name)
      .latest(Math.max(count * 4, 30))
      .then((r) => {
        if (wallets.current === name) own = r;
      })
      .catch(() => undefined);
  });
  $effect(() => {
    const list = sides;
    return poll(() => list.forEach((s) => void refreshTxs(s)), 10000);
  });
  let now = $state(Math.floor(Date.now() / 1000));
  $effect(() => every(30000, () => (now = Math.floor(Date.now() / 1000))));

  const fresh = $derived<ActivityItem[]>(
    foldActivity(
      mergeTxs([
        own.map((r) => ({ chain: HOST, name: productName(), txid: r.txid, time: r.time, category: r.type, amount: formatUnits(r.amount, 8), address: r.address, label: r.label, confirmations: r.depth, vout: r.idx, drivechain: r.drivechain ?? null })),
        ...sides.map((s) => sidechainTxs(s, chainLabel(s), many.txs[s] ?? [])),
      ]),
      count,
      HOST,
    ),
  );
  // Shown again at most every RECENT_MS (doc/unified-app.md §21.3): a busy network changes the list with each payment
  // of any chain, and the ages of its rows with it.
  const RECENT_MS = 5000;
  const shownItems = throttled(() => fresh, RECENT_MS);
  const items = $derived(shownItems.value);

  let open = $state<ManyChainTx | null>(null);
  let dialogOpen = $state(false);
  function show(r: ManyChainTx) {
    if (prefs.mask) return;
    open = r;
    dialogOpen = true;
  }
  function showRewards(chain: string) {
    setSelectedChain(chain);
    go('transactions');
  }
  const details = (r: ManyChainTx) => () => chainHost.transaction(r.chain, r.txid, chainWallet(r.chain), r.vout, r.address) as Promise<TxDetails>;
</script>

<ul class="activity" data-many-recent={chains.join(',')}>
  {#each items as item (item.key)}
    <li>
      {#if item.kind === 'rewards'}
        <button type="button" class="row" title={t('Show the transactions of %1', item.name)} onclick={() => showRewards(item.chain)} data-recent-rewards={item.chain} data-recent-folded={item.what}>
          <span class="icon" class:reward={item.what === 'rewards'} aria-hidden="true"><Icon name={item.what === 'rewards' ? 'mine' : 'sidechains'} size={15} /></span>
          <span class="what">
            <span class="title">{item.what === 'rewards' ? t('Mining rewards') : t('Merged mining bids')} <span class="count num">{item.count}</span></span>
            <span class="sub">{#if several}<ChainBadge id={item.chain} name={item.name} small /><span class="dot-sep">{' · '}</span>{/if}{t('latest %1', ageText(item.time, now))}</span>
          </span>
          <span class="amt"><ModuleAmount text={item.total} plus coins colored={item.what === 'rewards'} /></span>
        </button>
      {:else}
        {@const r = item.tx}
        <button type="button" class="row" disabled={prefs.mask} title={prefs.mask ? t('Hidden: privacy mode') : t('Show the details')} onclick={() => show(r)} data-recent-tx={r.chain}>
          <span class="icon" class:in={item.incoming} aria-hidden="true"><Icon name={item.incoming ? 'down' : 'up'} size={15} /></span>
          <span class="what">
            <span class="title" translate={item.label ? 'no' : undefined}>{#if item.label}<SafeText text={item.label} />{:else}{whatWords(item.what, productName(), item.side === null ? '' : slotLabel(item.side))}{/if}</span>
            <span class="sub">
              {#if several}<ChainBadge id={r.chain} name={r.name} small /><span class="dot-sep">{' · '}</span>{/if}
              {#if item.label}<span>{whatWords(item.what, productName(), item.side === null ? '' : slotLabel(item.side))}</span><span class="dot-sep">{' · '}</span>{/if}
              {#if item.ref && !item.label}<span class="mono ref" translate="no" title={item.ref}>{shortHash(item.ref, 6)}</span><span class="dot-sep">{' · '}</span>{/if}
              <span>{r.confirmations === 0 ? t('waiting') : ageText(r.time, now)}</span>
            </span>
          </span>
          <span class="amt"><ModuleAmount text={r.amount} plus coins colored={item.incoming} /></span>
        </button>
      {/if}
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
  .activity {
    display: flex;
    flex-direction: column;
    margin: 0;
    padding: 0;
    list-style: none;
  }
  .activity > li + li {
    border-top: 1px solid var(--line);
  }
  .row {
    display: grid;
    grid-template-columns: auto minmax(0, 1fr) auto;
    gap: 0.75rem;
    align-items: center;
    width: 100%;
    padding: 0.55rem 0.35rem;
    border: 0;
    border-radius: var(--radius-s);
    background: none;
    color: var(--text);
    font: inherit;
    text-align: left;
    cursor: pointer;
  }
  .row:hover:not(:disabled) {
    background: var(--raised);
  }
  .row:disabled {
    cursor: default;
  }
  .icon {
    display: inline-grid;
    place-items: center;
    width: 2rem;
    height: 2rem;
    border-radius: 50%;
    background: var(--raised);
    color: var(--text-muted);
  }
  .icon.in {
    background: color-mix(in srgb, var(--positive) 16%, transparent);
    color: var(--positive);
  }
  .icon.reward {
    background: color-mix(in srgb, var(--accent) 14%, transparent);
    color: var(--accent-strong, var(--accent));
  }
  .what {
    display: flex;
    flex-direction: column;
    gap: 0.1rem;
    min-width: 0;
  }
  .title {
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    font-weight: 600;
  }
  .count {
    margin-left: 0.25rem;
    padding: 0 0.4rem;
    border-radius: 999px;
    background: var(--raised);
    color: var(--text-muted);
    font-size: 0.8rem;
    font-weight: 600;
  }
  .sub {
    display: flex;
    align-items: center;
    min-width: 0;
    overflow: hidden;
    color: var(--text-muted);
    font-size: 0.82rem;
    white-space: nowrap;
  }
  .sub :global(.chain-badge) {
    font-weight: 550;
  }
  .ref {
    font-size: 0.8rem;
  }
  .amt {
    font-weight: 600;
    text-align: right;
  }
  .empty {
    padding: 0.6rem 0.35rem;
  }
  .on {
    display: flex;
    gap: 0.5rem;
    align-items: center;
    margin: 0 0 0.6rem;
  }
</style>
