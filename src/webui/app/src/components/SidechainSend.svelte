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
  import AssetLogo from './AssetLogo.svelte';
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

  function useAll() {
    amount = available ?? '';
    if (!chosenAsset) subtractFee = true;
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
    <h2 class="title"><ChainBadge id={stem} {name} /> <span class="muted">{t('Send')}</span></h2>
    {#if summary?.wallets && summary.wallets.length > 1}
      <label class="wallet-pick"><span class="muted small">{t('From the wallet')}</span>
        <select value={wallet} onchange={pickWallet} data-send-wallet>
          {#each summary.wallets as w (w)}<option value={w} translate="no">{w}</option>{/each}
        </select>
      </label>
    {:else if wallet}
      <span class="muted small">{t('From the wallet')} <strong translate="no"><SafeText text={wallet} /></strong></span>
    {/if}
  </header>

  {#if !summary?.running && summary}
    <p class="warn-box">{t('Its node is not running: turn it on in Settings → Sidechains.')}</p>
  {/if}

  {#if caps?.assets}
    <div class="field">
      <span class="label" id="send-what-{stem}">{t('What to send')}</span>
      <div class="what" role="radiogroup" aria-labelledby="send-what-{stem}" data-send-asset-list>
        <button type="button" role="radio" class="what-item" class:on={asset === ''} aria-checked={asset === ''} onclick={() => (asset = '')} data-send-asset-pick="">
          <ChainBadge id={stem} {name} small markOnly />
          <span class="what-name"><SafeText text={chainUnit(stem)} /></span>
          <span class="muted small"><ModuleAmount text={summary?.balances?.trusted ?? null} coins /></span>
        </button>
        {#each summary?.assets ?? [] as a (a.id)}
          <button type="button" role="radio" class="what-item" class:on={asset === a.id} aria-checked={asset === a.id} onclick={() => (asset = a.id)} data-send-asset-pick={a.id}>
            <AssetLogo {stem} id={a.id} label={a.label} size={1.3} />
            <span class="what-name"><SafeText text={a.label} /></span>
            <span class="muted small"><ModuleAmount text={a.balance} /></span>
          </button>
        {/each}
      </div>
      <!-- The same choice for the keyboard and the tests: one list. -->
      <select class="sr-only" tabindex="-1" aria-hidden="true" bind:value={asset} data-send-asset>
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

  <div class="field">
    <label for="send-to-{stem}">{t('Pay to')}</label>
    <input id="send-to-{stem}" class="mono" type="text" spellcheck="false" autocomplete="off" placeholder={shielded ? t('A shielded address of %1', name) : t('An address of %1', name)} bind:value={address} aria-invalid={check.state === 'bad' ? 'true' : undefined} aria-describedby="send-to-help-{stem}" data-send-address />
    <span id="send-to-help-{stem}" class="help">
      {#if check.state === 'bad'}
        <span class="refused" role="alert" data-address-error>
          <Icon name="alert" size={16} />
          <span><strong>{t('Not an address to pay on %1', name)}</strong><br /><SafeText text={check.text} /></span>
        </span>
      {:else if check.state === 'checking'}
        <span class="muted small">{t('Checking the address against %1 and the other chains…', name)}</span>
      {:else if check.state === 'ok'}
        <span class="ok small" data-address-ok><Icon name="check" size={14} /> {t('An address of %1', name)}{#if check.own} · {t('An address of this wallet')}{/if}</span>
      {:else}
        <span class="muted small">{t('Only an address of %1: one of another chain is refused before anything is sent.', name)}</span>
      {/if}
    </span>
  </div>

  <div class="field">
    <label for="send-amount-{stem}">{t('Amount')}</label>
    <div class="amount-row">
      <input id="send-amount-{stem}" class="num" type="text" inputmode="decimal" autocomplete="off" placeholder="0.00" bind:value={amount} aria-invalid={amountError ? 'true' : undefined} data-send-amount />
      <span class="unit"><SafeText text={unit} /></span>
      {#if available && !shielded}
        <!-- All of it: a coin's fee then comes out of the amount. -->
        <button type="button" class="btn ghost small" onclick={useAll} data-send-max>{t('All of it')}</button>
      {/if}
    </div>
    <span class="help">
      {#if amountError}<span class="error">{t(amountError, String(decimals))}</span>{:else}<span class="muted small">{t('Available')} <ModuleAmount text={available} unit={unit} coins={!chosenAsset} />{#if chosenAsset} · {t('%1 decimals', String(decimals))}{/if}</span>{/if}
    </span>
  </div>

  {#if shielded}
    <div class="field">
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
    gap: 1rem;
    padding: 1.1rem 1.3rem 1.2rem;
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
    font-size: 1rem;
    flex: 1;
  }
  .wallet-pick {
    display: inline-flex;
    gap: 0.4rem;
    align-items: center;
  }
  .label {
    font-weight: 600;
  }
  .what {
    display: flex;
    flex-wrap: wrap;
    gap: 0.4rem;
  }
  .what-item {
    display: inline-flex;
    align-items: center;
    gap: 0.45rem;
    padding: 0.35rem 0.7rem 0.35rem 0.4rem;
    border: 1px solid var(--line);
    border-radius: 999px;
    background: var(--surface-2);
    color: var(--text);
    font: inherit;
    cursor: pointer;
  }
  .what-item:hover {
    border-color: var(--line-strong);
  }
  .what-item.on {
    border-color: var(--accent);
    box-shadow: 0 0 0 1px var(--accent) inset;
    background: color-mix(in srgb, var(--accent) 10%, var(--surface-2));
  }
  .what-name {
    font-weight: 650;
  }
  .help {
    min-height: 1.2rem;
  }
  .refused {
    display: flex;
    gap: 0.55rem;
    align-items: flex-start;
    padding: 0.55rem 0.75rem;
    border: 1px solid var(--negative);
    border-radius: var(--radius-m);
    background: color-mix(in srgb, var(--negative) 10%, transparent);
    color: var(--text);
    font-size: 0.9rem;
  }
  .refused :global(svg) {
    flex: none;
    margin-top: 0.1rem;
    color: var(--negative);
  }
  .field {
    display: flex;
    flex-direction: column;
    gap: 0.3rem;
  }
  .field {
    max-width: 44rem;
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
    align-self: flex-start;
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
