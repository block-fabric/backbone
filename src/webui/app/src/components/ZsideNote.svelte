<script lang="ts">
  // zSide and the backup phrase (doc/unified-app.md §23.13): its wallet is always its own (its shielded keys are not made
  // from the phrase), so its private funds are not brought back by the phrase. Said wherever the phrase is said to cover the
  // sidechains, with its own backup (or, in a restore, its own restore) offered right there when its node runs.
  import Icon from './Icon.svelte';
  import SafeText from './SafeText.svelte';
  import { errorText } from '../lib/dialogs.svelte';
  import { t } from '../lib/i18n.svelte';
  import { toast } from '../lib/state.svelte';
  import { chainHost, chainWallet, setChainWallet } from '../lib/chains.svelte';
  import { hub } from '../lib/sidechains.svelte';
  import { ZSIDE_NOTE } from '../lib/seed.svelte';

  let { actions = false, restore = false, compact = false }: { actions?: boolean; restore?: boolean; compact?: boolean } = $props();

  const entry = $derived((hub.list?.chains ?? []).find((e) => e.phraseKeys === false));
  const running = $derived(!!entry && entry.enabled && entry.ready);
  let busy = $state('');
  let error = $state('');

  async function backup() {
    if (!entry) return;
    busy = 'backup';
    error = '';
    try {
      const file = await chainHost.walletBackup(entry.stem, chainWallet(entry.stem) ?? '');
      if (file) toast('success', t('Backed up'), file);
    } catch (e) {
      error = errorText(e);
    } finally {
      busy = '';
    }
  }

  async function restoreOwn() {
    if (!entry) return;
    busy = 'restore';
    error = '';
    try {
      const done = await chainHost.walletRestore(entry.stem, 'restored');
      if (done?.name) {
        // The app's pages use the wallet brought back on that chain.
        setChainWallet(entry.stem, done.name);
        toast('success', t('Restored'), t('Its wallet "%1" is back', done.name));
      }
    } catch (e) {
      error = errorText(e);
    } finally {
      busy = '';
    }
  }
</script>

{#if entry}
  <div class="zside-note" class:compact data-zside-note={restore ? 'restore' : 'backup'}>
    <Icon name="alert" size={16} />
    <div class="body">
      <p>
        {t(ZSIDE_NOTE)}
        {#if restore}{t('If you used %1, bring its private funds back from its own backup file once it runs.', entry.name)}{:else if !actions}{t('Back up %1’s wallet on its own once it runs (Settings → Wallet).', entry.name)}{/if}
      </p>
      {#if actions}
        {#if running}
          <div class="row">
            {#if restore}
              <button type="button" class="btn small" disabled={busy !== ''} onclick={restoreOwn} data-action="zside-restore"><Icon name="upload" size={14} /> {busy === 'restore' ? t('Working…') : t('Restore %1’s wallet from a backup…', entry.name)}</button>
            {/if}
            <button type="button" class="btn small" disabled={busy !== ''} onclick={backup} data-action="zside-backup"><Icon name="download" size={14} /> {busy === 'backup' ? t('Working…') : t('Back up %1’s wallet…', entry.name)}</button>
          </div>
        {:else if entry.enabled}
          <p class="muted small">{t('Its node is starting: its backup is offered here, and in Settings → Wallet, once it runs.')}</p>
        {/if}
      {/if}
      {#if error}<p class="error small" role="alert"><SafeText text={error} /></p>{/if}
    </div>
  </div>
{/if}

<style>
  .zside-note {
    display: flex;
    gap: 0.55rem;
    align-items: flex-start;
    padding: 0.6rem 0.8rem;
    border: 1px solid var(--warning);
    border-radius: var(--radius-l);
    color: var(--warning);
  }
  .zside-note.compact {
    padding: 0.4rem 0.6rem;
    font-size: 0.88rem;
  }
  .body {
    display: flex;
    flex-direction: column;
    gap: 0.45rem;
    min-width: 0;
    color: var(--text);
  }
  p {
    margin: 0;
  }
  .row {
    display: flex;
    flex-wrap: wrap;
    gap: 0.4rem;
  }
  .small {
    font-size: 0.85rem;
  }
  .error {
    color: var(--negative);
  }
</style>
