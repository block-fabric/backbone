<script lang="ts">
  // The wallets of one sidechain on the app's Manage wallets page (doc/unified-app.md §18): which ones its node has,
  // open or not, what each holds; open, close, create, back up (to a file chosen in the app's own chooser); and which
  // one the app's pages use on that chain. Whether the chain's wallet comes from the app's backup phrase or keeps its
  // own (then it needs its own backup) is said plainly.
  import { t } from '../lib/i18n.svelte';
  import Icon from './Icon.svelte';
  import SafeText from './SafeText.svelte';
  import ChainBadge from './ChainBadge.svelte';
  import ModuleAmount from './ModuleAmount.svelte';
  import { errorText } from '../lib/dialogs.svelte';
  import { toast } from '../lib/state.svelte';
  import { chainHost, chainState, chainWallet, refreshSummary, setChainWallet, WALLET_NAME, type ChainWallets } from '../lib/chains.svelte';

  let { stem, name, running }: { stem: string; name: string; running: boolean } = $props();

  let data = $state<ChainWallets | null>(null);
  let error = $state('');
  let busy = $state('');
  let newName = $state('');

  async function load() {
    if (!running) return;
    try {
      data = await chainHost.wallets(stem, chainWallet(stem));
      error = '';
    } catch (e) {
      error = errorText(e);
    }
  }
  $effect(() => {
    void running;
    void load();
  });

  async function run(what: string, fn: () => Promise<unknown>, done?: string) {
    busy = what;
    error = '';
    try {
      await fn();
      if (done) toast('success', done);
    } catch (e) {
      error = errorText(e);
    } finally {
      busy = '';
      chainState.asked[stem] = 0;
      void refreshSummary(stem, 0);
      await load();
    }
  }

  const nameOk = $derived(WALLET_NAME.test(newName.trim()));
  const selected = $derived(data?.selected ?? '');
</script>

<section class="panel chain-wallets" aria-label={t('Wallets of %1', name)} data-chain-wallets={stem}>
  <div class="panel-head">
    <h2><ChainBadge id={stem} {name} /></h2>
    {#if data}
      {#if data.mode === 'separate'}
        <span class="badge warning" title={t('Its keys are not made from the app\'s backup phrase: back this wallet up on its own.')}>{t('Separate backup')}</span>
      {:else}
        <span class="badge accent" title={t('Its keys come from the app\'s backup phrase.')}>{t('From your backup phrase')}</span>
      {/if}
    {/if}
  </div>
  {#if !running}
    <p class="panel-body muted">{t('Its node is not running: turn it on in Settings → Sidechains.')}</p>
  {:else}
    <ul class="panel-body list">
      {#each data?.wallets ?? [] as w (w.name)}
        <li data-chain-wallet={w.name}>
          <Icon name="wallet" />
          <span class="grow">
            <strong><SafeText text={w.name} /></strong>
            {#if w.loaded}<span class="badge accent">{t('open')}</span>{/if}
            {#if w.name === selected && w.loaded}<span class="badge" data-wallet-used>{t('used by the app')}</span>{/if}
            {#if w.encrypted}<span class="badge"><Icon name="lock" size={12} /> {t('encrypted')}</span>{/if}
            {#if w.loaded && w.trusted !== undefined}<span class="muted small"><ModuleAmount text={w.trusted} coins /></span>{/if}
          </span>
          {#if w.loaded}
            {#if w.name !== selected}<button type="button" class="btn small" onclick={() => { setChainWallet(stem, w.name); void load(); void refreshSummary(stem, 0); }}>{t('Use')}</button>{/if}
            <button type="button" class="btn small" disabled={!!busy} onclick={() => run('backup', () => chainHost.walletBackup(stem, w.name).then((f) => f && toast('success', t('Backed up'), f)))}><Icon name="download" size={14} /> {t('Back up…')}</button>
            <button type="button" class="btn small" disabled={!!busy} onclick={() => run('close', () => chainHost.walletUnload(stem, w.name))}>{t('Close')}</button>
          {:else}
            <button type="button" class="btn small primary" disabled={!!busy} onclick={() => run('open', () => chainHost.walletLoad(stem, w.name))}>{t('Open')}</button>
          {/if}
        </li>
      {:else}
        <li class="muted">{data ? t('No wallets available') : t('Loading…')}</li>
      {/each}
    </ul>
    <form class="panel-body create" onsubmit={(e) => { e.preventDefault(); if (nameOk) void run('create', () => chainHost.walletCreate(stem, newName.trim()), t('Wallet created')).then(() => (newName = '')); }}>
      <input type="text" bind:value={newName} placeholder={t('Name of a new wallet')} aria-label={t('Name of a new wallet')} maxlength="64" autocomplete="off" />
      <button type="submit" class="btn small" disabled={!nameOk || !!busy}><Icon name="plus" size={14} /> {t('Create')}</button>
    </form>
  {/if}
  {#if error}<p class="panel-body error" role="alert"><SafeText text={error} /></p>{/if}
</section>

<style>
  .chain-wallets h2 {
    margin: 0;
    font-size: 1rem;
  }
  .list {
    list-style: none;
    margin: 0;
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
  }
  .list li {
    display: flex;
    align-items: center;
    gap: 0.6rem;
  }
  .grow {
    flex: 1;
    display: inline-flex;
    gap: 0.5rem;
    align-items: center;
    flex-wrap: wrap;
  }
  .create {
    display: flex;
    gap: 0.5rem;
  }
  .create input {
    flex: 1;
    min-width: 0;
  }
  .small {
    font-size: 0.85rem;
  }
</style>
