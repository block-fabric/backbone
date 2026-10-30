<script lang="ts">
  // Coins into and out of one sidechain, on the app's Sidechains page (doc/unified-app.md §18): a deposit (paid by this
  // app's wallet to the deposit address of the chain's wallet, from its node: never typed; asked in the app's own
  // dialog), a withdrawal back (asked by the chain's own question in the trusted dialog: it is slow, bundled and voted
  // on by the miners), the withdrawals not paid yet, the deposits on their way, and the chain's link to this app's node
  // and its blind merged mining.
  import { t } from '../lib/i18n.svelte';
  import Icon from './Icon.svelte';
  import SafeText from './SafeText.svelte';
  import ChainBadge from './ChainBadge.svelte';
  import ModuleAmount from './ModuleAmount.svelte';
  import Amount from './Amount.svelte';
  import { errorText } from '../lib/dialogs.svelte';
  import { toast, wallets } from '../lib/state.svelte';
  import { ErrorCode } from '../lib/bridge';
  import { walletApi } from '../lib/wallet';
  import { productName } from '../lib/modules.svelte';
  import { amountProblem, chainHost, chainState, chainWallet, noteDeposit, pendingDeposits, refreshSummary, type ChainPeg } from '../lib/chains.svelte';

  let { stem, name, running, focus = false }: { stem: string; name: string; running: boolean; focus?: boolean } = $props();

  let peg = $state<ChainPeg | null>(null);
  const stakes = $derived(!!chainState.caps[stem]?.staking);
  let error = $state('');
  let depositAmount = $state('');
  let withdrawAddress = $state('');
  let withdrawAmount = $state('');
  let withdrawFee = $state('0.0001');
  let busy = $state('');
  let root: HTMLElement | undefined = $state();

  async function load() {
    if (!running) return;
    try {
      peg = await chainHost.peg(stem, chainWallet(stem));
    } catch (e) {
      error = errorText(e);
    }
  }
  $effect(() => {
    void running;
    void load();
    void refreshSummary(stem);
    const timer = setInterval(() => {
      if (!document.hidden) void load();
    }, 10000);
    return () => clearInterval(timer);
  });
  $effect(() => {
    if (focus && root) root.scrollIntoView({ block: 'start' });
  });

  const summary = $derived(chainState.summaries[stem]);
  const own = $derived(wallets.current !== null ? wallets.balances[wallets.current] : undefined);
  const depositError = $derived(depositAmount.trim() ? amountProblem(depositAmount, 8) : null);
  const withdrawError = $derived(withdrawAmount.trim() ? amountProblem(withdrawAmount, 8, summary?.balances?.trusted ?? null) : null);

  function said(e: unknown): string {
    const code = e && typeof e === 'object' && 'code' in e ? (e as { code: number }).code : 0;
    return code === ErrorCode.CANCELLED ? t('Declined in the dialog: nothing was sent.') : errorText(e);
  }

  async function deposit() {
    busy = 'deposit';
    error = '';
    try {
      const r = await chainHost.deposit(stem, depositAmount.trim(), chainWallet(stem));
      if (r.txid) noteDeposit({ stem, txid: r.txid, address: r.address, amount: depositAmount.trim() });
      toast('success', t('Deposit to %1 sent', name), t('It is credited there after the next blocks of both chains.'));
      depositAmount = '';
    } catch (e) {
      error = said(e);
    } finally {
      busy = '';
    }
  }

  async function ownAddress() {
    if (wallets.current === null) return;
    try {
      withdrawAddress = await walletApi(wallets.current).newAddress(t('Withdrawal from %1', name), '');
    } catch (e) {
      error = errorText(e);
    }
  }

  async function withdraw() {
    busy = 'withdraw';
    error = '';
    try {
      await chainHost.withdraw(stem, withdrawAddress.trim(), withdrawAmount.trim(), withdrawFee.trim(), chainWallet(stem));
      toast('success', t('Withdrawal from %1 made', name), t('It is paid on %1 once a bundle with it is voted through: that takes long.', productName()));
      withdrawAmount = '';
      void load();
      void refreshSummary(stem, 0);
    } catch (e) {
      error = said(e);
    } finally {
      busy = '';
    }
  }
</script>

<section class="panel chain-peg" bind:this={root} aria-label={t('Coins into and out of %1', name)} data-chain-peg={stem}>
  <div class="panel-head">
    <h2><ChainBadge id={stem} {name} /></h2>
    {#if peg}
      <span class="badge" class:accent={peg.mainchain.state === 'ok'} data-peg-link>{peg.mainchain.state === 'ok' ? t('In step with %1', productName()) : t('Link to %1: %2', productName(), peg.mainchain.state ?? '?')}</span>
      <!-- A chain that stakes makes its blocks by stake: merged mining only anchors them in the mainchain (0 is not broken). -->
      <span class="badge" data-peg-bmm title={stakes ? t('Its blocks are made by its validators; merged mining anchors them in %1', productName()) : undefined}>{stakes ? (peg.bmm.mining ? t('Blocks by stake; anchors by merged mining: %1', String(peg.bmm.blocks ?? 0)) : t('Blocks by stake; merged mining off')) : peg.bmm.mining ? t('Merged mining on: %1 blocks', String(peg.bmm.blocks ?? 0)) : t('Merged mining off')}</span>
      {#if peg.mainchain.slot !== undefined}<span class="muted small">{t('Slot %1', String(peg.mainchain.slot))}</span>{/if}
    {/if}
    <span class="grow"></span>
    <span class="muted small">{t('On %1', name)} <ModuleAmount text={summary?.balances?.trusted ?? null} coins /></span>
  </div>
  {#if !running}
    <p class="panel-body muted">{t('Its node is not running: turn it on in Settings → Sidechains.')}</p>
  {:else}
    <div class="panel-body forms">
      <form class="form" onsubmit={(e) => { e.preventDefault(); void deposit(); }} data-peg-deposit={stem}>
        <h3><Icon name="sidechains" size={16} /> {t('Deposit into %1', name)}</h3>
        <p class="muted small">{t('From your %1 wallet, to your wallet on %2 (its deposit address, from its node).', productName(), name)}</p>
        <div class="row">
          <input type="text" class="num" inputmode="decimal" bind:value={depositAmount} aria-label={t('Amount')} placeholder={t('Amount')} autocomplete="off" data-peg-deposit-amount />
          <button type="submit" class="btn primary" disabled={!!busy || !depositAmount.trim() || !!depositError || wallets.current === null}>{busy === 'deposit' ? t('Working…') : t('Deposit…')}</button>
        </div>
        {#if depositError}<span class="error">{t(depositError, '8')}</span>{/if}
        {#if own}<span class="faint small">{t('%1 available', productName())}: <Amount sats={own.trusted} /></span>{/if}
      </form>
      <form class="form" onsubmit={(e) => { e.preventDefault(); void withdraw(); }} data-peg-withdraw={stem}>
        <h3><Icon name="upload" size={16} /> {t('Withdraw to %1', productName())}</h3>
        <p class="muted small">{t('Slow: paid once a bundle with it is voted through by the miners of %1.', productName())}</p>
        <div class="row">
          <input type="text" class="mono grow" bind:value={withdrawAddress} aria-label={t('An address of %1', productName())} placeholder={t('An address of %1', productName())} autocomplete="off" spellcheck="false" />
          <button type="button" class="btn small" onclick={ownAddress} disabled={wallets.current === null}>{t('Mine')}</button>
        </div>
        <div class="row">
          <input type="text" class="num" inputmode="decimal" bind:value={withdrawAmount} aria-label={t('Amount')} placeholder={t('Amount')} autocomplete="off" />
          <input type="text" class="num fee" inputmode="decimal" bind:value={withdrawFee} aria-label={t('Fee for the miners of %1', productName())} title={t('Fee for the miners of %1', productName())} autocomplete="off" />
          <button type="submit" class="btn" disabled={!!busy || !withdrawAddress.trim() || !withdrawAmount.trim() || !!withdrawError}>{busy === 'withdraw' ? t('Working…') : t('Withdraw…')}</button>
        </div>
        {#if withdrawError}<span class="error">{t(withdrawError, '8')}</span>{/if}
      </form>
    </div>
    {#if pendingDeposits(stem).length || peg?.withdrawals.length}
      <div class="panel-body lists">
        {#each pendingDeposits(stem) as d (d.txid)}
          <div class="item" data-peg-pending-deposit={d.txid}><Icon name="clock" size={14} /> {t('Deposit on its way')} <ModuleAmount text={d.amount} coins /> <span class="mono faint small"><SafeText text={d.txid.slice(0, 16)} />…</span></div>
        {/each}
        {#each (peg?.withdrawals ?? []).filter((w) => w.mine) as w (w.txid + w.vout)}
          <div class="item" data-peg-withdrawal={w.txid}><Icon name="upload" size={14} /> {t('Withdrawal')} <ModuleAmount text={w.amount} coins /> <span class="badge"><SafeText text={w.status} /></span></div>
        {/each}
      </div>
    {/if}
  {/if}
  {#if error}<p class="panel-body error" role="alert" data-peg-error><SafeText text={error} /></p>{/if}
</section>

<style>
  .chain-peg h2 {
    margin: 0;
    font-size: 1rem;
  }
  .panel-head {
    display: flex;
    align-items: center;
    gap: 0.6rem;
    flex-wrap: wrap;
  }
  .grow {
    flex: 1;
  }
  .forms {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(18rem, 1fr));
    gap: 1.2rem;
  }
  .form {
    display: flex;
    flex-direction: column;
    gap: 0.45rem;
  }
  .form h3 {
    margin: 0;
    font-size: 0.95rem;
    display: inline-flex;
    gap: 0.4rem;
    align-items: center;
  }
  .form p {
    margin: 0;
  }
  .row {
    display: flex;
    gap: 0.5rem;
    align-items: center;
  }
  .row input {
    flex: 1;
    min-width: 0;
  }
  .row input.fee {
    flex: 0 0 7rem;
  }
  .lists {
    display: flex;
    flex-direction: column;
    gap: 0.3rem;
  }
  .item {
    display: flex;
    gap: 0.5rem;
    align-items: center;
  }
  .small {
    font-size: 0.85rem;
  }
</style>
