<script lang="ts">
  // Receive on a sidechain from the app's own Receive page (doc/unified-app.md §18): a new address of that chain's
  // wallet (made by its node, never typed), with its QR code; on zSide, an address of the shielded account; and the
  // way in from this app's own chain: the deposit address of that wallet (s<slot>_…), and the deposit itself on the
  // Sidechains page.
  import { t } from '../lib/i18n.svelte';
  import Icon from './Icon.svelte';
  import SafeText from './SafeText.svelte';
  import ChainBadge from './ChainBadge.svelte';
  import CopyButton from './CopyButton.svelte';
  import QrCode from './QrCode.svelte';
  import { errorText } from '../lib/dialogs.svelte';
  import { chainHost, chainLink, chainState, chainWallet, refreshSummary, setChainWallet } from '../lib/chains.svelte';
  import { productName } from '../lib/modules.svelte';
  import { go } from '../lib/nav.svelte';

  let { stem, name }: { stem: string; name: string } = $props();

  const summary = $derived(chainState.summaries[stem]);
  const caps = $derived(chainState.caps[stem] ?? summary?.capabilities ?? null);
  let wallet = $state('');
  let label = $state('');
  let shielded = $state(false);
  let address = $state('');
  let deposit = $state('');
  let busy = $state(false);
  let error = $state('');

  $effect(() => {
    void stem;
    void refreshSummary(stem, 0).then((s) => {
      const chosen = chainWallet(stem);
      if (s && !wallet) wallet = chosen && s.wallets?.includes(chosen) ? chosen : (s.wallet ?? '');
    });
  });

  function pickWallet(ev: Event) {
    wallet = (ev.currentTarget as HTMLSelectElement).value;
    setChainWallet(stem, wallet);
    address = '';
    deposit = '';
  }

  async function newAddress() {
    busy = true;
    error = '';
    try {
      address = (await chainHost.receive(stem, wallet || undefined, label.trim(), shielded)).address;
    } catch (e) {
      error = errorText(e);
    } finally {
      busy = false;
    }
  }

  async function depositAddress() {
    error = '';
    try {
      deposit = (await chainHost.depositAddress(stem, wallet || undefined)).depositaddress;
    } catch (e) {
      error = errorText(e);
    }
  }

  function depositFromHost() {
    chainLink.peg = stem;
    go('sidechains');
  }
</script>

<section class="panel sidechain-receive" aria-label={t('Receive on %1', name)} data-sidechain-receive={stem}>
  <header class="head">
    <h2 class="title"><Icon name="receive" size={18} /> {t('Receive on')} <ChainBadge id={stem} {name} /></h2>
    {#if summary?.wallets && summary.wallets.length > 1}
      <label class="wallet-pick"><span class="muted small">{t('Wallet')}</span>
        <select value={wallet} onchange={pickWallet}>
          {#each summary.wallets as w (w)}<option value={w} translate="no">{w}</option>{/each}
        </select>
      </label>
    {:else if wallet}
      <span class="muted small">{t('Wallet')} <SafeText text={wallet} /></span>
    {/if}
  </header>

  <div class="row">
    <div class="field grow">
      <label for="recv-label-{stem}">{t('Label')}</label>
      <input id="recv-label-{stem}" type="text" bind:value={label} autocomplete="off" maxlength="100" />
    </div>
    {#if caps?.shielded}
      <label class="check"><input type="checkbox" bind:checked={shielded} /> <span>{t('Shielded (private)')}</span></label>
    {/if}
    <button type="button" class="btn primary" disabled={busy} onclick={newAddress} data-receive-new><Icon name="plus" size={16} /> {t('New address')}</button>
  </div>

  {#if error}<p class="error" role="alert"><SafeText text={error} /></p>{/if}

  {#if address}
    <div class="address" data-receive-address={address}>
      <QrCode text={address} size={180} label={t('QR code of the address')} />
      <div class="stack">
        <span class="muted small">{t('An address of %1', name)} · <SafeText text={wallet} /></span>
        <span class="mono break big"><SafeText text={address} /></span>
        <CopyButton text={address} label={t('Copy address')} iconOnly={false} />
        <span class="muted small">{t('Only for coins on %1: paid on another chain, they would not arrive.', name)}</span>
      </div>
    </div>
  {/if}

  <div class="from-host">
    <h3>{t('From %1', productName())}</h3>
    <p class="muted small">{t('Coins of %1 come into %2 by a deposit to its deposit address: the deposit names the sidechain and is refused for any other.', productName(), name)}</p>
    <div class="row">
      <button type="button" class="btn" onclick={depositAddress} data-receive-deposit-address>{t('Show the deposit address')}</button>
      <button type="button" class="btn" onclick={depositFromHost} data-receive-deposit-go><Icon name="sidechains" size={16} /> {t('Deposit from %1…', productName())}</button>
    </div>
    {#if deposit}
      <div class="deposit"><span class="mono break"><SafeText text={deposit} /></span> <CopyButton text={deposit} /></div>
    {/if}
  </div>
</section>

<style>
  .sidechain-receive {
    display: flex;
    flex-direction: column;
    gap: 1rem;
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
  .row {
    display: flex;
    gap: 0.8rem;
    align-items: end;
    flex-wrap: wrap;
  }
  .field {
    display: flex;
    flex-direction: column;
    gap: 0.3rem;
  }
  .grow {
    flex: 1;
    min-width: 12rem;
  }
  .check {
    display: inline-flex;
    gap: 0.4rem;
    align-items: center;
    padding-bottom: 0.4rem;
  }
  .address {
    display: flex;
    gap: 1.2rem;
    align-items: center;
    flex-wrap: wrap;
    padding: 0.8rem;
    border: 1px solid var(--line);
    border-radius: var(--radius-m, 0.7rem);
    background: var(--surface-2);
  }
  .stack {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
    min-width: 0;
    flex: 1;
  }
  .big {
    font-size: 1.02rem;
  }
  .break {
    word-break: break-all;
  }
  .from-host h3 {
    margin: 0 0 0.3rem;
    font-size: 0.95rem;
  }
  .deposit {
    margin-top: 0.6rem;
    display: flex;
    gap: 0.5rem;
    align-items: center;
  }
  .small {
    font-size: 0.85rem;
  }
</style>
