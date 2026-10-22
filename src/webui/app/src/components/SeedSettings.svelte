<script lang="ts">
  // Settings → Wallet → Backup phrase (doc/unified-app.md §23): whether there is one, each chain's wallet and where its keys
  // come from, and what can be done: make one (or move to one, for wallets of before), restore one, show it again; its
  // password: add one, change it, remove it (§23.11: a password is recommended, never forced); lock it now. Every one of
  // these asks in the app's own window; this panel never sees a word. zSide's own wallet said apart (§23.13).
  import Icon from './Icon.svelte';
  import SafeText from './SafeText.svelte';
  import SeedChainList from './SeedChainList.svelte';
  import ZsideNote from './ZsideNote.svelte';
  import { errorText } from '../lib/dialogs.svelte';
  import { t } from '../lib/i18n.svelte';
  import { productName } from '../lib/modules.svelte';
  import { refreshSeed, seed, seedHost, watchSeed } from '../lib/seed.svelte';
  import { toast } from '../lib/state.svelte';

  const product = $derived(productName());
  const status = $derived(seed.status);
  const open = $derived(status?.exists && status.password === false);
  const zsideOn = $derived((status?.chains ?? []).some((c) => c.phraseKeys === false && c.state !== 'off'));
  let busy = $state('');
  let error = $state('');

  $effect(() => {
    watchSeed();
    void refreshSeed();
  });

  async function run(what: string, fn: () => Promise<{ cancelled?: boolean } & Record<string, unknown>>, done: string) {
    busy = what;
    error = '';
    try {
      const r = await fn();
      if (!r.cancelled && done) toast('success', t('Backup phrase'), t(done));
    } catch (e) {
      error = errorText(e);
    } finally {
      busy = '';
      void refreshSeed();
    }
  }
  const made = (when?: number) => (when ? new Date(when * 1000).toLocaleDateString() : '');
</script>

<section class="panel seed" aria-labelledby="seed-title" data-seed-settings>
  <div class="panel-head">
    <h2 id="seed-title"><Icon name="key" size={18} /> {t('Backup phrase')}</h2>
    {#if status?.unlocked && status.password !== false}<span class="badge" data-unlocked><Icon name="unlock" size={13} /> {t('Unlocked for %1 min', Math.max(1, Math.ceil((status.expiresIn ?? 0) / 60)))}</span>{/if}
  </div>
  <div class="panel-body">
    {#if !status}
      <p class="muted">{t('Loading…')}</p>
    {:else if status.damaged}
      <p class="error">{t('The file of your backup phrase cannot be read. Your words on paper still bring everything back: restore them into a new data folder.')}</p>
    {:else if status.exists}
      <p>{t('One phrase of %1 words keeps your wallets on %2 and on every sidechain that uses it.', status.words ?? 12, product)} <span class="muted">{status.restored ? t('Restored on %1.', made(status.created)) : t('Made on %1.', made(status.created))} {t('Fingerprint %1', status.fingerprint ?? '')}</span></p>
      {#if open}
        <p class="password open" data-password="none"><Icon name="unlock" size={16} /> <span><strong>{t('No password.')}</strong> {t('Anyone with access to this computer can spend your coins on %1 and on every sidechain, and read your backup phrase.', product)}</span></p>
      {:else}
        <p class="password" data-password="on"><Icon name="lock" size={16} /> <span><strong>{t('Protected by a password.')}</strong> {t('Asked to send coins, to show your phrase and to make a sidechain’s keys.')}</span></p>
      {/if}
      <SeedChainList chains={status.chains} />
      {#if zsideOn}<ZsideNote actions compact />{/if}
      <div class="actions">
        <button type="button" class="btn" disabled={busy !== ''} onclick={() => run('show', seedHost.show, '')} data-action="seed-show"><Icon name="eye" size={15} /> {t('Show my backup phrase…')}</button>
        {#if open}
          <button type="button" class="btn primary" disabled={busy !== ''} onclick={() => run('add', seedHost.addPassword, 'A password protects your wallet now.')} data-action="seed-add-password"><Icon name="lock" size={15} /> {t('Add a password…')}</button>
        {:else}
          <button type="button" class="btn" disabled={busy !== ''} onclick={() => run('change', seedHost.changePassphrase, 'The password is changed.')} data-action="seed-change"><Icon name="key" size={15} /> {t('Change the password…')}</button>
          <button type="button" class="btn ghost" disabled={busy !== ''} onclick={() => run('remove', seedHost.removePassword, 'The password is removed.')} data-action="seed-remove-password"><Icon name="unlock" size={15} /> {t('Remove the password…')}</button>
          {#if status.unlocked}<button type="button" class="btn ghost" disabled={busy !== ''} onclick={() => run('lock', async () => ({ locked: await seedHost.lock() }), '')} data-action="seed-lock"><Icon name="lock" size={15} /> {t('Lock it now')}</button>{/if}
        {/if}
      </div>
      <p class="hint">{open ? t('Each wallet made from the phrase is opened by the app as it needs it. Add a password and it is asked instead.') : t('Your password protects the phrase on this computer and your %1 wallet. Each sidechain’s wallet has a passphrase of its own made from the phrase: you always type yours, the app gives each chain its own.', product)}</p>
    {:else if status.hasWallets}
      <p>{t('Your wallets were made before the backup phrase: each keeps its own keys and needs its own backup. One phrase can keep all of them safe from now on.')}</p>
      <div class="actions">
        <button type="button" class="btn primary" disabled={busy !== ''} onclick={() => (seed.open = 'migrate')} data-action="seed-migrate"><Icon name="key" size={15} /> {t('Make my backup phrase…')}</button>
      </div>
    {:else}
      <p>{t('No wallet yet. Make a backup phrase, or bring back one you have.')}</p>
      <div class="actions">
        <button type="button" class="btn primary" onclick={() => (seed.open = 'firstrun')} data-action="seed-setup"><Icon name="key" size={15} /> {t('Set up my wallet…')}</button>
      </div>
    {/if}
    {#if error}<p class="error" role="alert"><SafeText text={error} /></p>{/if}
  </div>
</section>

<style>
  .seed {
    margin-bottom: 1.1rem;
  }
  .panel-head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 0.6rem;
  }
  h2 {
    display: inline-flex;
    align-items: center;
    gap: 0.45rem;
  }
  .panel-body {
    display: flex;
    flex-direction: column;
    gap: 0.8rem;
  }
  p {
    margin: 0;
  }
  .actions {
    display: flex;
    flex-wrap: wrap;
    gap: 0.5rem;
  }
  .hint {
    font-size: 0.85rem;
    color: var(--text-muted);
  }
  .password {
    display: flex;
    gap: 0.45rem;
    align-items: flex-start;
  }
  .password.open {
    color: var(--warning);
  }
  .password span {
    color: var(--text);
  }
  .error {
    color: var(--negative);
  }
</style>
