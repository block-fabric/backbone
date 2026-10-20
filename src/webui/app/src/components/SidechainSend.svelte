<script lang="ts">
  // Send on a sidechain from the app's own Send page (doc/unified-app.md §18): the form follows the chain (its wallet,
  // its addresses, its assets, its shielded account). The address is checked by the host against that chain and every
  // other one this app runs before anything is prepared (an address of another chain is refused, with which one it is);
  // the amount is parsed exactly with the decimals of what is sent. The send itself is that chain's own question, asked
  // in the app's trusted dialog with the chain named and its mark, and runs only on a yes.
  import { t } from '../lib/i18n.svelte';
  import Icon from './Icon.svelte';
  import SafeText from './SafeText.svelte';
  import ChainBadge from './ChainBadge.svelte';
  import ModuleAmount from './ModuleAmount.svelte';
  import { errorText } from '../lib/dialogs.svelte';
  import { toast } from '../lib/state.svelte';
  import { ErrorCode } from '../lib/bridge';
  import { addressHint, amountProblem, chainHost, chainLink, chainState, chainUnit, chainWallet, refreshSummary, setChainWallet, type ChainAsset } from '../lib/chains.svelte';
  import { productName } from '../lib/modules.svelte';
  import { go } from '../lib/nav.svelte';

  let { stem, name }: { stem: string; name: string } = $props();

  const summary = $derived(chainState.summaries[stem]);
  const caps = $derived(chainState.caps[stem] ?? summary?.capabilities ?? null);
  let wallet = $state('');
  let address = $state('');
  let amount = $state('');
  /** '' for the chain's coin, else the hash of an asset. */
  let asset = $state('');
  let shielded = $state(false);
  let memo = $state('');
  let subtractFee = $state(false);
  let busy = $state(false);
  let error = $state('');
  let sent = $state<{ txid?: string } | null>(null);
  let check = $state<{ address: string; state: 'idle' | 'checking' | 'ok' | 'bad'; text: string; own?: boolean }>({ address: '', state: 'idle', text: '' });

  // A link (Send on Layers, an asset's Send): taken once.
  $effect(() => {
    const link = chainLink.send;
    if (link && link.chain === stem) {
      if (link.asset) asset = link.asset;
      chainLink.send = null;
    }
  });

  $effect(() => {
    void stem;
    void refreshSummary(stem, 0).then((s) => {
      if (s && !wallet) wallet = chainWallet(stem) && s.wallets?.includes(chainWallet(stem)!) ? chainWallet(stem)! : (s.wallet ?? '');
    });
  });

  const chosenAsset = $derived<ChainAsset | null>(asset ? (summary?.assets?.find((a) => a.id === asset) ?? null) : null);
  const decimals = $derived(chosenAsset ? chosenAsset.decimals : 8);
  const unit = $derived(chosenAsset ? chosenAsset.label : chainUnit(stem));
  const available = $derived(chosenAsset ? chosenAsset.balance : shielded ? (summary?.shielded?.spendable ?? null) : (summary?.balances?.trusted ?? null));
  const amountError = $derived(amount.trim() ? amountProblem(amount, decimals, available) : null);

  // The address, checked by the host a moment after the typing stops.
  $effect(() => {
    const a = address.trim();
    const sh = shielded;
    const hint = addressHint(a);
    if (hint === 'empty') {
      check = { address: a, state: 'idle', text: '' };
      return;
    }
    if (hint === 'deposit') {
      check = { address: a, state: 'bad', text: t('This is a deposit address: it moves coins from %1 into a sidechain (Sidechains → Deposit). Enter an address of %2.', productName(), name) };
      return;
    }
    check = { address: a, state: 'checking', text: '' };
    const timer = setTimeout(() => {
      chainHost.validate(stem, a, wallet || undefined, sh).then(
        (r) => {
          if (address.trim() === a) check = { address: a, state: 'ok', text: r.own ? t('An address of this wallet') : '', own: r.own };
        },
        (e) => {
          if (address.trim() === a) check = { address: a, state: 'bad', text: errorText(e) };
        },
      );
    }, 350);
    return () => clearTimeout(timer);
  });

  const canSend = $derived(!busy && check.state === 'ok' && check.address === address.trim() && !!amount.trim() && !amountError && !!wallet);

  function pickWallet(ev: Event) {
    wallet = (ev.currentTarget as HTMLSelectElement).value;
    setChainWallet(stem, wallet);
    void refreshSummary(stem, 0);
  }

  async function send() {
    if (!canSend) return;
    busy = true;
    error = '';
    sent = null;
    try {
      const r = await chainHost.send({ stem, wallet, address: address.trim(), amount: amount.trim(), ...(asset ? { asset } : {}), ...(shielded ? { shielded: true, memo } : {}), ...(asset || shielded ? {} : { subtractFee }) });
      sent = { txid: r.txid };
      toast('success', t('Sent on %1', name), r.txid ? t('Transaction %1…', r.txid.slice(0, 16)) : '');
      address = '';
      amount = '';
      memo = '';
      void refreshSummary(stem, 0);
    } catch (e) {
      const code = e && typeof e === 'object' && 'code' in e ? (e as { code: number }).code : 0;
      error = code === ErrorCode.CANCELLED ? t('Declined in the dialog: nothing was sent.') : errorText(e);
    } finally {
      busy = false;
    }
  }
</script>

<section class="panel sidechain-send" aria-label={t('Send on %1', name)} data-sidechain-send={stem}>
  <header class="head">
    <h2 class="title"><Icon name="send" size={18} /> {t('Send on')} <ChainBadge id={stem} {name} /></h2>
    {#if summary?.wallets && summary.wallets.length > 1}
      <label class="wallet-pick"><span class="muted small">{t('Wallet')}</span>
        <select value={wallet} onchange={pickWallet} data-send-wallet>
          {#each summary.wallets as w (w)}<option value={w} translate="no">{w}</option>{/each}
        </select>
      </label>
    {:else if wallet}
      <span class="muted small">{t('Wallet')} <SafeText text={wallet} /></span>
    {/if}
  </header>

  {#if !summary?.running && summary}
    <p class="warn-box">{t('Its node is not running: turn it on in Settings → Sidechains.')}</p>
  {/if}

  <div class="grid">
    {#if caps?.assets}
      <div class="field">
        <label for="send-asset-{stem}">{t('What')}</label>
        <select id="send-asset-{stem}" bind:value={asset} data-send-asset>
          <option value="" translate="no">{chainUnit(stem)}</option>
          {#each summary?.assets ?? [] as a (a.id)}<option value={a.id} translate="no">{a.label} ({a.balance})</option>{/each}
        </select>
      </div>
    {/if}
    {#if caps?.shielded && !asset}
      <label class="check shielded">
        <input type="checkbox" bind:checked={shielded} data-send-shielded />
        <span>{t('From the shielded account (private)')}</span>
      </label>
    {/if}
    <div class="field wide">
      <label for="send-to-{stem}">{t('Pay to')}</label>
      <input id="send-to-{stem}" class="mono" type="text" spellcheck="false" autocomplete="off" placeholder={t('An address of %1', name)} bind:value={address} aria-invalid={check.state === 'bad' ? 'true' : undefined} data-send-address />
      {#if check.state === 'bad'}
        <span class="error" role="alert" data-address-error><SafeText text={check.text} /></span>
      {:else if check.state === 'checking'}
        <span class="muted small">{t('Checking the address…')}</span>
      {:else if check.state === 'ok'}
        <span class="ok small" data-address-ok><Icon name="check" size={14} /> {t('An address of %1', name)}{#if check.own} · {t('An address of this wallet')}{/if}</span>
      {/if}
    </div>
    <div class="field">
      <label for="send-amount-{stem}">{t('Amount')}</label>
      <div class="amount-row">
        <input id="send-amount-{stem}" class="num" type="text" inputmode="decimal" autocomplete="off" bind:value={amount} aria-invalid={amountError ? 'true' : undefined} data-send-amount />
        <span class="unit"><SafeText text={unit} /></span>
      </div>
      {#if amountError}<span class="error">{t(amountError, String(decimals))}</span>{/if}
      <span class="muted small">{t('Available')} <ModuleAmount text={available} coins={!chosenAsset} /></span>
    </div>
    {#if shielded}
      <div class="field wide">
        <label for="send-memo-{stem}">{t('Memo (for a shielded address)')}</label>
        <input id="send-memo-{stem}" type="text" maxlength="512" bind:value={memo} autocomplete="off" />
      </div>
    {/if}
    {#if !asset && !shielded}
      <label class="check">
        <input type="checkbox" bind:checked={subtractFee} />
        <span>{t('Subtract fee from amount')}</span>
      </label>
    {/if}
  </div>

  {#if error}<p class="error" role="alert" data-send-error><SafeText text={error} /></p>{/if}
  {#if sent}
    <p class="ok" data-send-done>{t('Sent on %1.', name)} {#if sent.txid}<span class="mono"><SafeText text={sent.txid} /></span>{/if} <button type="button" class="btn ghost small" onclick={() => go('transactions')}>{t('Transactions')}</button></p>
  {/if}

  <footer class="actions">
    <button type="button" class="btn primary" disabled={!canSend} onclick={send} data-send-go><Icon name="send" size={17} /> {busy ? t('Working…') : t('Send…')}</button>
    <span class="muted small note">{t('%1 asks you first, in the app\'s own dialog: on %2, what is sent, where, and the fee.', name, name)}</span>
  </footer>
</section>

<style>
  .sidechain-send {
    display: flex;
    flex-direction: column;
    gap: 0.9rem;
    padding: 1.1rem 1.2rem;
  }
  .head {
    display: flex;
    align-items: center;
    gap: 1rem;
    flex-wrap: wrap;
  }
  .title {
    display: inline-flex;
    align-items: center;
    gap: 0.5rem;
    margin: 0;
    font-size: 1.05rem;
    flex: 1;
  }
  .wallet-pick {
    display: inline-flex;
    gap: 0.4rem;
    align-items: center;
  }
  .grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(16rem, 1fr));
    gap: 0.9rem 1.2rem;
    align-items: start;
  }
  .field {
    display: flex;
    flex-direction: column;
    gap: 0.3rem;
  }
  .field.wide {
    grid-column: 1 / -1;
  }
  .amount-row {
    display: flex;
    gap: 0.5rem;
    align-items: center;
  }
  .amount-row input {
    flex: 1;
    min-width: 0;
  }
  .unit {
    color: var(--text-muted);
    font-weight: 600;
  }
  .check {
    display: inline-flex;
    gap: 0.5rem;
    align-items: center;
    align-self: end;
  }
  .ok {
    color: var(--positive, var(--accent-strong));
    display: inline-flex;
    gap: 0.3rem;
    align-items: center;
    flex-wrap: wrap;
  }
  .small {
    font-size: 0.85rem;
  }
  .actions {
    display: flex;
    gap: 1rem;
    align-items: center;
    flex-wrap: wrap;
  }
  .note {
    flex: 1;
  }
</style>
