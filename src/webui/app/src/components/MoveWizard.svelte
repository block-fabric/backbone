<script lang="ts">
  // Move coins, guided (doc/unified-app.md §8 "Move coins into a sidechain", §23): one decision per screen for a new user,
  // the same calls as the Sidechains page's Move coins panel (chain.deposit: the deposit address from the chain's own node,
  // never typed, the app's own question; chain.withdraw: an address of this app's chain, the chain's own question). Then
  // what happens, followed: a deposit until the sidechain credits it, a withdrawal while the miners vote on it.
  import Dialog from './Dialog.svelte';
  import Icon from './Icon.svelte';
  import SafeText from './SafeText.svelte';
  import ChainBadge from './ChainBadge.svelte';
  import ModuleAmount from './ModuleAmount.svelte';
  import Amount from './Amount.svelte';
  import { ErrorCode } from '../lib/bridge';
  import { errorText } from '../lib/dialogs.svelte';
  import { t } from '../lib/i18n.svelte';
  import { go } from '../lib/nav.svelte';
  import { productName, productUnit } from '../lib/modules.svelte';
  import { wallets } from '../lib/state.svelte';
  import { walletApi } from '../lib/wallet';
  import { amountProblem, chainEntries, chainHost, chainState, chainWallet, creditDeposits, deposits, noteDeposit, ownWallet, refreshSummary, type ChainWithdrawal } from '../lib/chains.svelte';

  let { onclose }: { onclose: () => void } = $props();

  type Step = 'direction' | 'chain' | 'amount' | 'review' | 'sent';
  const STEPS: Step[] = ['direction', 'chain', 'amount', 'review', 'sent'];
  const TITLES: Record<Step, string> = { direction: 'Which way', chain: 'Which sidechain', amount: 'How much', review: 'Check', sent: 'On its way' };
  let open = $state(true);
  let step = $state<Step>('direction');
  let direction = $state<'in' | 'out'>('in');
  let target = $state('');
  let amount = $state('');
  let address = $state('');
  let fee = $state('0.0001');
  let busy = $state(false);
  let error = $state('');
  let sent = $state<null | { txid: string; address: string; stem: string; direction: 'in' | 'out'; amount: string }>(null);
  let withdrawal = $state<ChainWithdrawal | null>(null);

  const product = $derived(productName());
  const unit = $derived(productUnit());
  const side = $derived(chainEntries().filter((e) => !e.host && !ownWallet(e)));
  const entry = $derived(side.find((e) => e.id === target) ?? null);
  const summary = $derived(entry ? chainState.summaries[entry.id] : undefined);
  const own = $derived(wallets.current !== null ? wallets.balances[wallets.current] : undefined);
  $effect(() => {
    if (entry?.running) void refreshSummary(entry.id, 4000);
  });
  const available = $derived(direction === 'in' ? null : (summary?.balances?.trusted ?? null));
  const amountError = $derived(amount.trim() ? amountProblem(amount, 8, available) : null);
  const feeError = $derived.by(() => {
    if (direction !== 'out') return null;
    const p = amountProblem(fee, 8);
    return p === 'More than zero' ? null : p;
  });
  const index = $derived(STEPS.indexOf(step));
  const canGo = $derived(
    step === 'direction' ? true : step === 'chain' ? !!entry?.running : step === 'amount' ? !!amount.trim() && !amountError && (direction === 'in' ? wallets.current !== null : !!address.trim() && !feeError) : false,
  );

  // A withdrawal goes to an address of this app's wallet, filled in (it can be changed).
  $effect(() => {
    if (step === 'amount' && direction === 'out' && !address && wallets.current !== null) {
      walletApi(wallets.current)
        .newAddress(t('Back from %1', entry?.name ?? ''), '')
        .then((a) => (address = a), () => undefined);
    }
  });

  // After it was sent: a deposit until the sidechain's wallet shows it, a withdrawal while it waits for the miners.
  $effect(() => {
    const s = sent;
    if (!s) return;
    const poll = async () => {
      try {
        if (s.direction === 'in') {
          const r = await chainHost.transactions(s.stem, chainWallet(s.stem), 50);
          creditDeposits(s.stem, r.rows);
        } else {
          const peg = await chainHost.peg(s.stem, chainWallet(s.stem));
          withdrawal = peg.withdrawals.find((w) => w.txid === s.txid) ?? withdrawal;
        }
      } catch {
        // Read again at the next turn.
      }
    };
    void poll();
    const timer = setInterval(poll, 10_000);
    return () => clearInterval(timer);
  });
  const credited = $derived(sent?.direction === 'in' ? deposits.list.some((d) => d.txid === sent?.txid && d.credited) : false);

  async function send() {
    if (!entry) return;
    busy = true;
    error = '';
    try {
      if (direction === 'in') {
        const r = await chainHost.deposit(entry.id, amount.trim(), chainWallet(entry.id));
        if (r.txid) noteDeposit({ stem: entry.id, txid: r.txid, address: r.address, amount: amount.trim() });
        sent = { txid: r.txid ?? '', address: r.address, stem: entry.id, direction, amount: amount.trim() };
      } else {
        const r = await chainHost.withdraw(entry.id, address.trim(), amount.trim(), fee.trim(), chainWallet(entry.id));
        sent = { txid: r.txid ?? '', address: address.trim(), stem: entry.id, direction, amount: amount.trim() };
        void refreshSummary(entry.id, 0);
      }
      step = 'sent';
    } catch (e) {
      const code = e && typeof e === 'object' && 'code' in e ? (e as { code: number }).code : 0;
      error = code === ErrorCode.CANCELLED ? t('You declined: nothing was sent.') : errorText(e);
    } finally {
      busy = false;
    }
  }

  function close() {
    open = false;
    onclose();
  }
</script>

<Dialog bind:open title={t('Move coins')} wide onclose={close}>
  <div class="wizard" data-wizard="move" data-step={step}>
    <div class="bar" aria-hidden="true"><span style:width={`${((index + 1) / STEPS.length) * 100}%`}></span></div>
    <p class="caps where">{t('Step %1 of %2', index + 1, STEPS.length)} · {t(TITLES[step])}</p>

    {#if step === 'direction'}
      <h3>{t('Which way do your coins go?')}</h3>
      <div class="tiles" role="radiogroup" aria-label={t('Direction')}>
        <button type="button" role="radio" aria-checked={direction === 'in'} class="tile" class:on={direction === 'in'} onclick={() => (direction = 'in')} data-direction="in">
          <Icon name="down" size={20} />
          <strong>{t('Into a sidechain')}</strong>
          <span class="muted">{t('From your %1 wallet. It arrives after the next blocks of both chains: a few minutes.', product)}</span>
        </button>
        <button type="button" role="radio" aria-checked={direction === 'out'} class="tile" class:on={direction === 'out'} disabled={!side.length} onclick={() => (direction = 'out')} data-direction="out">
          <Icon name="up" size={20} />
          <strong>{t('Back to %1', product)}</strong>
          <span class="muted">{t('Slow on purpose: the miners of %1 vote on the way back, which takes days on the main network.', product)}</span>
        </button>
      </div>
    {:else if step === 'chain'}
      <h3>{direction === 'in' ? t('Into which sidechain?') : t('From which sidechain?')}</h3>
      {#if !side.length}
        <p>{t('No sidechain is on yet. Turn one on first: it takes a few minutes.')}</p>
        <div><button type="button" class="btn" onclick={() => { close(); go('settings', 'sidechains'); }}>{t('Settings → Sidechains')}</button></div>
      {:else}
        <div class="tiles" role="radiogroup" aria-label={t('Sidechain')}>
          {#each side as e (e.id)}
            <button type="button" role="radio" aria-checked={target === e.id} class="tile chain" class:on={target === e.id} disabled={!e.running} onclick={() => (target = e.id)} data-chain={e.id}>
              <ChainBadge id={e.id} name={e.name} />
              <span class="muted">{e.running ? t('Running') : t('Not running: it starts by itself, or turn it on in Settings')}</span>
            </button>
          {/each}
        </div>
      {/if}
    {:else if step === 'amount' && entry}
      <h3>{direction === 'in' ? t('How much goes into %1?', entry.name) : t('How much comes back to %1?', product)}</h3>
      <div class="field">
        <label for="mw-amount">{t('Amount')}</label>
        <div class="with-unit">
          <input id="mw-amount" type="text" class="num" inputmode="decimal" autocomplete="off" placeholder="0.00000000" bind:value={amount} aria-invalid={amountError ? 'true' : undefined} data-move-amount />
          <span class="unit">{direction === 'out' ? entry.unit : unit}</span>
        </div>
        {#if amountError}<span class="error small">{t(amountError, '8')}</span>
        {:else if direction === 'in'}<span class="hint">{t('Available on %1', product)} <Amount sats={own?.trusted} /></span>
        {:else}<span class="hint">{t('Available on %1', entry.name)} <ModuleAmount text={summary?.balances?.trusted ?? null} unit={entry.unit} coins /></span>{/if}
      </div>
      {#if direction === 'out'}
        <div class="field">
          <label for="mw-address">{t('To your %1 address', product)}</label>
          <input id="mw-address" type="text" class="mono" spellcheck="false" autocomplete="off" bind:value={address} data-move-address />
          <span class="hint">{t('A new address of your %1 wallet, filled in for you.', product)}</span>
        </div>
        <div class="field">
          <label for="mw-fee">{t('Fee for the miners')}</label>
          <div class="with-unit">
            <input id="mw-fee" type="text" class="num" inputmode="decimal" autocomplete="off" bind:value={fee} aria-invalid={feeError ? 'true' : undefined} />
            <span class="unit">{unit}</span>
          </div>
          {#if feeError}<span class="error small">{t(feeError, '8')}</span>{:else}<span class="hint">{t('Paid on %1 to the miners who include it; a higher one goes first.', product)}</span>{/if}
        </div>
      {:else}
        <p class="muted small">{t('It goes to your wallet on %1: its node gives the address, nothing to copy.', entry.name)}</p>
      {/if}
    {:else if step === 'review' && entry}
      <h3>{t('Check, then confirm')}</h3>
      <dl class="review">
        <dt>{t('From')}</dt><dd>{#if direction === 'in'}<ChainBadge id="chains" name={product} small /> <span class="muted">{t('your wallet')}</span>{:else}<ChainBadge id={entry.id} name={entry.name} small /> <span class="muted">{t('your wallet there')}</span>{/if}</dd>
        <dt>{t('To')}</dt><dd>{#if direction === 'in'}<ChainBadge id={entry.id} name={entry.name} small /> <span class="muted">{t('your wallet there')}</span>{:else}<span class="mono"><SafeText text={address} /></span>{/if}</dd>
        <dt>{t('Amount')}</dt><dd class="num">{amount.trim()} {direction === 'out' ? entry.unit : unit}</dd>
        {#if direction === 'out'}<dt>{t('Fee for the miners')}</dt><dd class="num">{fee.trim()} {unit}</dd>{/if}
        <dt>{t('Takes')}</dt><dd>{direction === 'in' ? t('A few minutes: the next blocks of both chains') : t('A while: the miners vote it through, then it is paid')}</dd>
      </dl>
      <p class="muted small">{t('You confirm it in the next window, the app’s own: what it says is what is sent.')}</p>
      {#if error}<p class="error" role="alert"><SafeText text={error} /></p>{/if}
    {:else if step === 'sent' && sent && entry}
      <h3>{sent.direction === 'in' ? t('%1 %2 sent into %3', sent.amount, unit, entry.name) : t('%1 %2 on their way back from %3', sent.amount, entry.unit, entry.name)}</h3>
      <ol class="track">
        {#if sent.direction === 'in'}
          <li class="done"><Icon name="check" size={15} /> {t('Sent on %1', product)}</li>
          <li class:done={credited} data-credited={credited}><Icon name={credited ? 'check' : 'clock'} size={15} /> {credited ? t('Credited on %1: it is in your wallet there', entry.name) : t('Waiting for %1 to see it (the next blocks of both chains)', entry.name)}</li>
        {:else}
          <li class="done"><Icon name="check" size={15} /> {t('Taken from your wallet on %1', entry.name)}</li>
          <li><Icon name="clock" size={15} /> {withdrawal ? t('Its state: %1', withdrawal.status) : t('Waiting to be bundled with others for the miners')}</li>
          <li><Icon name="clock" size={15} /> {t('Paid on %1 once the miners vote it through', product)}</li>
        {/if}
      </ol>
      <p class="muted small">{t('You can close this: it goes on, and Transactions shows it.')}</p>
    {/if}
  </div>

  {#snippet actions()}
    {#if step === 'sent'}
      <button type="button" class="btn primary" onclick={close} data-action="done">{t('Done')}</button>
    {:else}
      <button type="button" class="btn ghost" onclick={close} disabled={busy}>{t('Cancel')}</button>
      {#if index > 0}<button type="button" class="btn" disabled={busy} onclick={() => (step = STEPS[index - 1])}>{t('Back')}</button>{/if}
      {#if step === 'review'}
        <button type="button" class="btn primary" data-action="send" disabled={busy} onclick={send}>{busy ? t('Working…') : direction === 'in' ? t('Deposit…') : t('Withdraw…')}</button>
      {:else}
        <button type="button" class="btn primary" data-action="next" disabled={!canGo} onclick={() => (step = STEPS[index + 1])}>{t('Next')}</button>
      {/if}
    {/if}
  {/snippet}
</Dialog>

<style>
  .wizard {
    display: flex;
    flex-direction: column;
    gap: 0.8rem;
    min-height: 18rem;
  }
  .bar {
    height: 0.3rem;
    border-radius: 999px;
    background: var(--surface-2);
    overflow: hidden;
  }
  .bar span {
    display: block;
    height: 100%;
    background: var(--accent);
  }
  .where,
  h3,
  p {
    margin: 0;
  }
  .tiles {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(15rem, 1fr));
    gap: 0.6rem;
  }
  .tile {
    display: flex;
    flex-direction: column;
    align-items: flex-start;
    gap: 0.35rem;
    padding: 0.85rem 0.95rem;
    border: 1px solid var(--line);
    border-radius: var(--radius-l);
    background: transparent;
    color: var(--text);
    font: inherit;
    text-align: start;
    cursor: pointer;
  }
  .tile.on {
    border-color: var(--accent);
    background: var(--accent-soft);
  }
  .tile:disabled {
    opacity: 0.55;
    cursor: not-allowed;
  }
  .field {
    display: flex;
    flex-direction: column;
    gap: 0.3rem;
  }
  .with-unit {
    display: flex;
    align-items: center;
    gap: 0.5rem;
  }
  .with-unit input {
    flex: 1;
  }
  .hint,
  .small {
    font-size: 0.85rem;
    color: var(--text-muted);
  }
  .review {
    display: grid;
    grid-template-columns: max-content 1fr;
    gap: 0.5rem 1rem;
    margin: 0;
  }
  .review dt {
    color: var(--text-muted);
  }
  .review dd {
    margin: 0;
    overflow-wrap: anywhere;
  }
  .track {
    display: flex;
    flex-direction: column;
    gap: 0.45rem;
    margin: 0;
    padding: 0;
    list-style: none;
  }
  .track li {
    display: flex;
    align-items: center;
    gap: 0.45rem;
    color: var(--text-muted);
  }
  .track li.done {
    color: var(--text);
  }
  .error {
    color: var(--negative);
  }
</style>
