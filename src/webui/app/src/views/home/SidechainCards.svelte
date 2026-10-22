<script lang="ts">
  // Home with one sidechain selected (doc/unified-app.md §20.2): the first row of cards is that chain's, as its own app
  // has them: its Balance (its wallet, its assets), its Node (height, in step with this app's chain), its Peg (what is on
  // its way in and out, its slot, its escrow), and its Merged mining, or its Staking for a chain that stakes. Each card
  // opens the page that has more; amounts in the chain's coin, masked in privacy mode.
  import { t, tn } from '../../lib/i18n.svelte';
  import Card from './Card.svelte';
  import Icon from '../../components/Icon.svelte';
  import ModuleAmount from '../../components/ModuleAmount.svelte';
  import Amount from '../../components/Amount.svelte';
  import SafeText from '../../components/SafeText.svelte';
  import { formatCount } from '../../lib/format';
  import { go } from '../../lib/nav.svelte';
  import { coinsToSats } from '../../lib/units';
  import { NODE_PAGE, openModule, productName } from '../../lib/modules.svelte';
  import { simpleMode } from '../../lib/uimode.svelte';
  import { bmmErrorWords } from '../../lib/seed.svelte';
  import { balanceOf, chainHost, chainLink, chainState, chainWallet, fromBaseUnits, pendingDeposits, refreshSummary, type ChainEntry, type ChainPeg, type ChainStaking } from '../../lib/chains.svelte';

  let { entry, escrow = null }: { entry: ChainEntry; escrow?: number | null } = $props();

  const stem = $derived(entry.id);
  const summary = $derived(chainState.summaries[stem]);
  const b = $derived(balanceOf(summary));
  const stakes = $derived(!!entry.caps?.staking);
  let peg = $state<ChainPeg | null>(null);
  let staking = $state<ChainStaking | null>(null);

  // What the cards show, while shown: every few seconds, nothing while the window is hidden.
  $effect(() => {
    const s = stem;
    const withStaking = stakes;
    const load = () => {
      void refreshSummary(s, 4000);
      chainHost.peg(s, chainWallet(s)).then(
        (p) => (peg = p),
        () => undefined,
      );
      if (withStaking)
        chainHost.staking(s, chainWallet(s)).then(
          (x) => (staking = x),
          () => undefined,
        );
    };
    peg = null;
    staking = null;
    load();
    const timer = setInterval(() => {
      if (!document.hidden) load();
    }, 10000);
    return () => clearInterval(timer);
  });

  const mine = $derived((peg?.withdrawals ?? []).filter((w) => w.mine));
  const bmmProblem = $derived(bmmErrorWords(peg?.bmm?.error));
  /** Simple mode says what moves, not the slot and the escrow of the drivechain. */
  const simple = $derived(simpleMode());
  const onTheirWay = $derived(pendingDeposits(stem).length);
  const inStep = $derived(summary?.mainchain ? summary.mainchain.connected !== false && (summary.mainchain.behind_blocks ?? 0) === 0 : null);

  function send() {
    chainLink.send = { chain: stem };
    go('send');
  }
  function receive() {
    chainLink.receive = { chain: stem };
    go('receive');
  }
  function moveCoins() {
    chainLink.peg = stem;
    go('sidechains');
  }
</script>

<div class="stats" data-sidechain-cards={stem}>
  <Card title={t('Balance')} icon="wallet" open={() => go('overview')} openLabel={t('Open the overview')} stat>
    <span class="value"><ModuleAmount text={b.available} unit={entry.unit} coins /></span>
    <dl class="pairs">
      <dt>{t('Pending')}</dt><dd><ModuleAmount text={b.waiting} coins /></dd>
      {#if summary?.assets?.length}<dt>{t('Assets')}</dt><dd>{formatCount(summary.assets.length)}</dd>{/if}
      {#if summary?.shielded}<dt title={t('Shielded: amounts and addresses hidden')}>{t('Private')}</dt><dd data-card-private><ModuleAmount text={summary.shielded.available} coins /></dd>{/if}
      {#if summary?.names}<dt>{t('Names')}</dt><dd class="num" data-card-names>{formatCount(summary.names.count)}{#if summary.names.reservations}<span class="faint">{' + '}{tn('%n reservation(s)', summary.names.reservations)}</span>{/if}</dd>{/if}
      <dt>{t('Wallet')}</dt><dd class="wallet" translate="no"><SafeText text={summary?.wallet || '–'} /></dd>
    </dl>
    {#snippet actions()}
      <button type="button" class="btn primary grow" disabled={!entry.running} onclick={send}><Icon name="send" size={16} /> {t('Send')}</button>
      <button type="button" class="btn grow" disabled={!entry.running} onclick={receive}><Icon name="receive" size={16} /> {t('Receive')}</button>
    {/snippet}
  </Card>

  <Card title={t('Node')} icon="nodes" open={() => openModule(stem, NODE_PAGE)} openLabel={t('How its node is doing')} stat>
    <span class="value">
      {#if !entry.running}<span class="state">{t('Starting')}</span>
      {:else if inStep}<span class="state ok"><Icon name="check" size={16} /> {t('In step')}</span>
      {:else}<span class="state warn"><Icon name="history" size={16} /> {t('Catching up')}</span>{/if}
    </span>
    <dl class="pairs">
      <dt>{t('Height')}</dt><dd class="num">{summary?.height !== undefined ? formatCount(summary.height) : '–'}</dd>
      <dt>{t('%1 height', productName())}</dt><dd class="num">{summary?.mainchain?.height !== undefined ? formatCount(summary.mainchain.height) : '–'}</dd>
      <dt>{t('Behind')}</dt><dd class="num">{summary?.mainchain?.behind_blocks !== undefined ? tn('%n block(s)', summary.mainchain.behind_blocks) : '–'}</dd>
    </dl>
    {#snippet actions()}
      <button type="button" class="btn grow" onclick={() => openModule(stem, NODE_PAGE)}><Icon name="nodes" size={15} /> {t('Its node')}</button>
    {/snippet}
  </Card>

  <Card title={simple ? t('Moving coins') : t('Peg')} icon="sidechains" open={moveCoins} openLabel={t('Move coins in or out')} stat>
    {#if simple}
      <span class="value">{onTheirWay + mine.length === 0 ? t('Nothing on the way') : t('%1 on the way', formatCount(onTheirWay + mine.length))}</span>
    {:else}
      <span class="value num">{summary?.mainchain?.slot !== undefined ? t('Slot %1', String(summary.mainchain.slot)) : '–'}</span>
    {/if}
    <dl class="pairs">
      <dt>{t('Deposits on their way')}</dt><dd class="num">{formatCount(onTheirWay)}</dd>
      <dt>{t('Withdrawals waiting')}</dt><dd class="num">{peg ? formatCount(mine.length) : '–'}</dd>
      {#if !simple}<dt>{t('In escrow')}</dt><dd>{#if escrow !== null}<Amount sats={coinsToSats(escrow)} showUnit={false} />{:else}–{/if}</dd>{/if}
    </dl>
    {#snippet actions()}
      <button type="button" class="btn grow" onclick={moveCoins}><Icon name="sidechains" size={15} /> {t('Move coins')}</button>
    {/snippet}
  </Card>

  {#if stakes}
    <Card title={t('Staking')} icon="shield" open={() => go('staking')} openLabel={t('Open staking')} stat>
      <span class="value">{#if staking?.node?.validator}<span class="state ok">{t('Validating')}</span>{:else if staking}<span class="state">{t('Not a validator')}</span>{:else}–{/if}</span>
      <dl class="pairs">
        <dt>{t('Bonded')}</dt><dd><ModuleAmount text={staking?.chain?.staking?.bonded} coins /></dd>
        <dt>{t('Your stake')}</dt><dd><ModuleAmount text={fromBaseUnits(staking?.node?.stake)} coins /></dd>
        <dt>{t('Blocks made')}</dt><dd class="num">{staking?.node?.blocksmade !== undefined ? formatCount(staking.node.blocksmade) : '–'}</dd>
      </dl>
      {#snippet actions()}
        <button type="button" class="btn grow" onclick={() => go('staking')}><Icon name="shield" size={15} /> {t('Staking')}</button>
      {/snippet}
    </Card>
  {:else}
    <Card title={t('Merged mining')} icon="mine" open={() => openModule(stem, 'peg', 'bmm')} openLabel={t('Its merged mining')} stat>
      <span class="value">{#if peg?.bmm?.mining}<span class="state ok"><Icon name="check" size={16} /> {t('Mining')}</span>{:else if peg}<span class="state">{t('Off')}</span>{:else}–{/if}</span>
      <dl class="pairs">
        <dt>{t('Blocks mined')}</dt><dd class="num">{peg?.bmm?.blocks !== undefined ? formatCount(peg.bmm.blocks) : '–'}</dd>
        <dt>{t('Requests')}</dt><dd class="num">{peg?.bmm?.requests !== undefined ? formatCount(peg.bmm.requests) : '–'}</dd>
        <dt>{t('Outbid')}</dt><dd class="num">{peg?.bmm?.outbid !== undefined ? formatCount(peg.bmm.outbid) : '–'}</dd>
      </dl>
      <!-- Why it cannot bid, said plainly (doc/unified-app.md §23.12): the wallet that pays is locked, empty or not open. -->
      {#if peg?.bmm?.mining && bmmProblem}<p class="bmm-problem" role="alert" data-bmm-problem><Icon name="alert" size={14} /> <span><SafeText text={t(bmmProblem.text)} /></span></p>{/if}
      {#snippet actions()}
        <button type="button" class="btn grow" onclick={() => openModule(stem, 'peg', 'bmm')}><Icon name="mine" size={15} /> {t('Settings')}</button>
        {#if bmmProblem?.settings && peg?.bmm?.mining}<button type="button" class="btn grow" onclick={() => go('settings', 'wallet')} data-action="mining-wallet"><Icon name="wallet" size={15} /> {t('Mining wallet')}</button>{/if}
      {/snippet}
    </Card>
  {/if}
</div>

<style>
  .stats {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(14.5rem, 1fr));
    gap: 1rem;
  }
  .value {
    display: block;
    margin: 0.1rem 0 0.55rem;
    font-size: 1.45rem;
    font-weight: 650;
    letter-spacing: -0.02em;
    line-height: 1.25;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .state {
    display: inline-flex;
    align-items: center;
    gap: 0.4rem;
    font-size: 1.25rem;
  }
  .state.ok {
    color: var(--positive);
  }
  .state.warn {
    color: var(--warning);
  }
  .bmm-problem {
    display: flex;
    gap: 0.35rem;
    align-items: flex-start;
    margin: 0.4rem 0 0;
    font-size: 0.82rem;
    color: var(--warning);
  }
  .bmm-problem span {
    color: var(--text);
  }
  .pairs {
    display: grid;
    grid-template-columns: minmax(min-content, 1fr) minmax(0, auto);
    gap: 0.35rem 1rem;
    margin: 0;
    padding-top: 0.6rem;
    border-top: 1px solid var(--line);
  }
  .pairs dt {
    color: var(--text-muted);
    font-size: 0.86rem;
  }
  .pairs dd {
    min-width: 0;
    margin: 0;
    overflow: hidden;
    text-align: right;
    text-overflow: ellipsis;
    font-weight: 600;
    font-size: 0.92rem;
    white-space: nowrap;
  }
  @container (max-width: 520px) {
    .stats {
      grid-template-columns: minmax(0, 1fr);
    }
  }
</style>
