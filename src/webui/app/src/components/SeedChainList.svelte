<script lang="ts">
  // Each chain's wallet and the backup phrase (doc/unified-app.md §23): made from it, being made, read again (with how far),
  // waiting for its node, its keys not made yet (a button makes them: the passphrase is asked in the app's own dialog),
  // or a wallet of its own. Shown by the guides and by Settings → Wallet.
  import Icon from './Icon.svelte';
  import SafeText from './SafeText.svelte';
  import ChainBadge from './ChainBadge.svelte';
  import { errorText } from '../lib/dialogs.svelte';
  import { t } from '../lib/i18n.svelte';
  import { chainWords, progressText, refreshSeed, seedHost, type SeedChain } from '../lib/seed.svelte';

  let { chains, only = null }: { chains: SeedChain[]; only?: string[] | null } = $props();

  let busy = $state('');
  let error = $state('');
  const shown = $derived(only ? chains.filter((c) => only.includes(c.id)) : chains);

  async function makeKeys(c: SeedChain) {
    busy = c.id;
    error = '';
    try {
      await seedHost.addChain(c.id);
    } catch (e) {
      error = errorText(e);
    } finally {
      busy = '';
      void refreshSeed();
    }
  }
</script>

<ul class="seed-chains" data-seed-chains>
  {#each shown as c (c.id)}
    {@const said = chainWords(c)}
    <li data-chain={c.id} data-state={c.state}>
      <ChainBadge id={c.id} name={c.name} small />
      <span class="what {said.tone}">
        {#if c.state === 'ready' || c.state === 'off'}<Icon name="check" size={14} />{:else if c.state === 'error' || c.state === 'needs-keys' || c.state === 'own'}<Icon name="alert" size={14} />{:else}<Icon name="clock" size={14} />{/if}
        {t(said.text)}{#if progressText(c)}<span class="dot-sep">{' · '}</span><span class="num">{t('%1', progressText(c))}</span>{/if}
      </span>
      {#if c.wallet}<span class="wallet muted" translate="no" title={t('Its wallet')}><Icon name="wallet" size={13} /> <SafeText text={c.wallet} /></span>{/if}
      {#if c.state === 'needs-keys' || c.state === 'error'}
        <button type="button" class="btn small" disabled={busy !== ''} onclick={() => makeKeys(c)} data-make-keys={c.id}><Icon name="key" size={14} /> {busy === c.id ? t('Working…') : t('Make its keys…')}</button>
      {/if}
      {#if c.error}<span class="error small"><SafeText text={c.error} /></span>{/if}
    </li>
  {/each}
</ul>
{#if error}<p class="error small" role="alert"><SafeText text={error} /></p>{/if}

<style>
  .seed-chains {
    display: flex;
    flex-direction: column;
    margin: 0;
    padding: 0;
    list-style: none;
  }
  li {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: 0.35rem 0.75rem;
    padding: 0.55rem 0;
    border-bottom: 1px solid var(--line);
  }
  li:last-child {
    border-bottom: 0;
  }
  .what {
    display: inline-flex;
    align-items: center;
    gap: 0.35rem;
    flex: 1 1 14rem;
    min-width: 0;
  }
  .what.ok {
    color: var(--positive, var(--text));
  }
  .what.warn {
    color: var(--warning);
  }
  .what.bad {
    color: var(--negative);
  }
  .what.busy,
  .what.off {
    color: var(--text-muted);
  }
  .wallet {
    display: inline-flex;
    align-items: center;
    gap: 0.25rem;
    font-size: 0.85rem;
    font-family: var(--font-mono);
  }
  .error {
    flex-basis: 100%;
    color: var(--negative);
  }
  .small {
    font-size: 0.85rem;
  }
</style>
