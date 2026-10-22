<script lang="ts">
  // Whether the backup phrase and its wallets get a password (doc/unified-app.md §23.11): recommended, never forced. Without
  // one, said plainly what it means before anything is made; a password can be added later in Settings → Wallet.
  import Icon from './Icon.svelte';
  import { t } from '../lib/i18n.svelte';
  import { productName } from '../lib/modules.svelte';

  let { protect = $bindable(true), disabled = false }: { protect?: boolean; disabled?: boolean } = $props();
  const product = $derived(productName());
</script>

<fieldset class="choices" {disabled} data-password-choice={protect ? 'password' : 'none'}>
  <legend class="sr-only">{t('A password')}</legend>
  <label class="choice" class:chosen={protect}>
    <input type="radio" name="protect" checked={protect} onchange={() => (protect = true)} data-protect="password" />
    <span>
      <span class="title"><strong>{t('Protect it with a password')}</strong> <span class="badge accent">{t('recommended')}</span></span>
      <span class="muted">{t('Asked to send coins and to show your phrase: someone using this computer without it can do neither.')}</span>
    </span>
  </label>
  <label class="choice" class:chosen={!protect}>
    <input type="radio" name="protect" checked={!protect} onchange={() => (protect = false)} data-protect="none" />
    <span>
      <strong>{t('No password')}</strong>
      <span class="muted">{t('Nothing to type, ever.')}</span>
    </span>
  </label>
</fieldset>
{#if !protect}
  <p class="warn" role="note" data-no-password-warning>
    <Icon name="alert" size={16} />
    <span>{t('Without a password, anyone with access to this computer can spend your coins on %1 and on every sidechain, and read your backup phrase. You can add a password later in Settings → Wallet.', product)}</span>
  </p>
{/if}

<style>
  .choices {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
    margin: 0;
    padding: 0;
    border: 0;
  }
  .choice {
    display: flex;
    gap: 0.7rem;
    align-items: flex-start;
    padding: 0.65rem 0.9rem;
    border: 1px solid var(--line);
    border-radius: var(--radius-l);
    cursor: pointer;
  }
  .choice.chosen {
    border-color: var(--accent);
    background: var(--accent-soft);
  }
  .choice > span {
    display: flex;
    flex-direction: column;
    gap: 0.2rem;
    min-width: 0;
  }
  .title {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: 0.45rem;
  }
  .warn {
    display: flex;
    max-width: none;
    gap: 0.5rem;
    align-items: flex-start;
    margin: 0;
    padding: 0.6rem 0.8rem;
    border: 1px solid var(--warning);
    border-radius: var(--radius-l);
    color: var(--warning);
    font-size: 0.9rem;
  }
  .warn span {
    color: var(--text);
  }
</style>
