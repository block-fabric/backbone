<script lang="ts">
  // Settings → Wallet → Merged mining (doc/unified-app.md §23.12): which wallet pays the bids of merged mining. The main
  // wallet by default (as before: it works while it has no password or is unlocked); as an option, a separate mining wallet
  // (from the backup phrase, its account 1, without a password: it pays the bids by itself so the main wallet can stay
  // locked), its balance, Top up (a payment from the main wallet, asked in the app's window), Move back, Stop using it.
  // When bids cannot be paid (the main wallet locked, the mining wallet empty) it is said plainly here.
  import Icon from './Icon.svelte';
  import SafeText from './SafeText.svelte';
  import { errorText } from '../lib/dialogs.svelte';
  import { t } from '../lib/i18n.svelte';
  import { productName, productUnit } from '../lib/modules.svelte';
  import { toast } from '../lib/state.svelte';
  import { walletApi } from '../lib/wallet';
  import { coinsToSats, satsToCoins, parseAmount, UNITS } from '../lib/units';
  import { ErrorCode } from '../lib/bridge';
  import { miningHost, miningProblem, refreshSeed, seed, type MiningStatus } from '../lib/seed.svelte';

  const product = $derived(productName());
  let status = $state<MiningStatus | null>(null);
  let busy = $state('');
  let error = $state('');
  let amount = $state('');
  let topping = $state(false);

  async function load() {
    try {
      status = await miningHost.status();
      error = '';
    } catch (e) {
      error = errorText(e);
    }
  }
  $effect(() => {
    void seed.status;
    void load();
    const timer = setInterval(() => void load(), 10000);
    return () => clearInterval(timer);
  });

  const problem = $derived(miningProblem(status));
  const balance = $derived(status?.mining?.trusted !== undefined ? String(status.mining.trusted) : '');
  const parsed = $derived(parseAmount(amount, UNITS[0].id));
  const declined = (e: unknown) => !!e && typeof e === 'object' && 'code' in e && (e as { code: number }).code === ErrorCode.CANCELLED;

  async function run(what: string, fn: () => Promise<unknown>) {
    busy = what;
    error = '';
    try {
      await fn();
    } catch (e) {
      if (!declined(e)) error = errorText(e);
    } finally {
      busy = '';
      await load();
      void refreshSeed();
    }
  }

  /** A payment between two wallets of this chain (asked in the app's own window, its password there if it has one). */
  async function pay(from: string, to: string, sats: bigint, all: boolean, label: string) {
    const address = await walletApi(to).newAddress(label, '');
    const api = walletApi(from);
    const prepared = await api.prepare([{ address, amount: sats, subtractFee: all, label }], {}, { confTarget: 6 });
    try {
      await api.commit(prepared.token);
    } catch (e) {
      await api.discard(prepared.token).catch(() => undefined);
      throw e;
    }
  }

  function topUp() {
    if (!status?.mining || !parsed.ok || parsed.sats <= 0n) return;
    const sats = parsed.sats;
    void run('topup', async () => {
      await pay(status!.main, status!.mining!.wallet, sats, false, t('Mining wallet'));
      toast('success', t('Mining wallet'), t('%1 %2 on their way to the mining wallet', satsToCoins(sats), productUnit()));
      amount = '';
      topping = false;
    });
  }

  function moveBack() {
    if (!status?.mining || !balance) return;
    const sats = coinsToSats(balance);
    if (sats <= 0n) return;
    void run('back', async () => {
      await pay(status!.mining!.wallet, status!.main, sats, true, t('From the mining wallet'));
      toast('success', t('Mining wallet'), t('Its coins are on their way back to your wallet'));
    });
  }
</script>

<section class="panel mining" aria-labelledby="mining-title" data-mining-settings data-mining-on={status?.on ? 'true' : 'false'}>
  <div class="panel-head">
    <h2 id="mining-title"><Icon name="mine" size={18} /> {t('Merged mining')}</h2>
  </div>
  <div class="panel-body">
    {#if !status}
      <p class="muted">{error ? '' : t('Loading…')}</p>
    {:else}
      {#if problem}<p class="problem" role="alert" data-mining-problem={status.problem}><Icon name="alert" size={16} /> <span>{t(problem)}</span></p>{/if}
      {#if status.on && status.mining}
        <p data-mining-paying="mining">
          {t('The bids of merged mining are paid from your mining wallet')} <strong class="mono"><SafeText text={status.mining.wallet} /></strong>{status.mining.from === 'phrase' ? t(', made from your backup phrase.') : t(', with keys of its own: back it up on its own.')}
        </p>
        <p class="balance"><span class="muted">{t('It holds')}</span> <strong class="num" data-mining-balance>{balance || '—'} {productUnit()}</strong>{#if status.mining.pending && Number(status.mining.pending) > 0}<span class="muted">{' · '}{t('%1 on its way', String(status.mining.pending))}</span>{/if}</p>
        <p class="hint">{t('It has no password, so it can pay by itself while your wallet stays locked: keep only a little in it.')}</p>
        {#if topping}
          <form class="topup" onsubmit={(e) => { e.preventDefault(); topUp(); }}>
            <label>
              <span>{t('From your wallet %1', status.main)}</span>
              <input type="text" inputmode="decimal" bind:value={amount} placeholder="0.01" aria-label={t('Amount')} data-mining-amount autocomplete="off" />
            </label>
            <span class="unit">{productUnit()}</span>
            <button type="submit" class="btn primary small" disabled={busy !== '' || !parsed.ok || parsed.sats <= 0n} data-action="mining-topup-send">{busy === 'topup' ? t('Working…') : t('Top up…')}</button>
            <button type="button" class="btn ghost small" onclick={() => (topping = false)}>{t('Cancel')}</button>
          </form>
          {#if amount && !parsed.ok}<p class="error small">{t(parsed.error)}</p>{/if}
        {/if}
        <div class="actions">
          {#if !topping}<button type="button" class="btn" disabled={busy !== '' || !status.main} onclick={() => (topping = true)} data-action="mining-topup"><Icon name="plus" size={15} /> {t('Top up…')}</button>{/if}
          <button type="button" class="btn ghost" disabled={busy !== '' || !balance || Number(balance) <= 0} onclick={moveBack} data-action="mining-back"><Icon name="arrow" size={15} /> {t('Move its coins back')}</button>
          <button type="button" class="btn ghost" disabled={busy !== ''} onclick={() => run('stop', miningHost.stop)} data-action="mining-stop">{t('Pay from my wallet again')}</button>
        </div>
      {:else}
        <p data-mining-paying="main">
          {#if status.main}{t('The bids of merged mining are paid from your wallet')} <strong class="mono"><SafeText text={status.main} /></strong>.{:else}{t('The bids of merged mining are paid from your %1 wallet.', product)}{/if}
          {#if status.payingEncrypted}{t('It has a password: while it is locked, merged mining cannot pay its bids.')}{/if}
        </p>
        <div class="option">
          <p><strong>{t('Use a separate mining wallet')}</strong></p>
          <p class="muted small">{status.phrase ? t('A small wallet of its own, made from your backup phrase (so the phrase brings it back too), without a password: it pays the bids by itself, so your main wallet can stay locked. You top it up from your wallet.') : t('A small wallet with keys of its own, without a password: it pays the bids by itself, so your main wallet can stay locked. Back it up on its own, or keep very little in it.')}</p>
          <div class="actions">
            <button type="button" class="btn" disabled={busy !== ''} onclick={() => run('create', miningHost.create)} data-action="mining-create"><Icon name="wallet" size={15} /> {busy === 'create' ? t('Working…') : t('Use a separate mining wallet…')}</button>
          </div>
        </div>
      {/if}
      {#if status.bidding.length}<p class="muted small">{t('Merged mining is on for: %1', status.bidding.map((b) => b.name).join(', '))}</p>{/if}
    {/if}
    {#if error}<p class="error" role="alert"><SafeText text={error} /></p>{/if}
  </div>
</section>

<style>
  .mining {
    margin-bottom: 1.1rem;
  }
  h2 {
    display: inline-flex;
    align-items: center;
    gap: 0.45rem;
  }
  .panel-body {
    display: flex;
    flex-direction: column;
    gap: 0.7rem;
  }
  p {
    margin: 0;
  }
  .problem {
    display: flex;
    gap: 0.45rem;
    align-items: flex-start;
    padding: 0.55rem 0.75rem;
    border: 1px solid var(--warning);
    border-radius: var(--radius-l);
    color: var(--warning);
  }
  .problem span {
    color: var(--text);
  }
  .balance {
    display: flex;
    gap: 0.4rem;
    align-items: baseline;
    flex-wrap: wrap;
  }
  .option {
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
    padding: 0.7rem 0.85rem;
    border: 1px solid var(--line);
    border-radius: var(--radius-l);
  }
  .actions {
    display: flex;
    flex-wrap: wrap;
    gap: 0.5rem;
  }
  .topup {
    display: flex;
    flex-wrap: wrap;
    align-items: flex-end;
    gap: 0.5rem;
  }
  .topup label {
    display: flex;
    flex-direction: column;
    gap: 0.2rem;
    font-size: 0.85rem;
  }
  .topup input {
    width: 10rem;
  }
  .unit {
    padding-bottom: 0.4rem;
  }
  .hint,
  .small {
    font-size: 0.85rem;
  }
  .hint {
    color: var(--text-muted);
  }
  .error {
    color: var(--negative);
  }
</style>
