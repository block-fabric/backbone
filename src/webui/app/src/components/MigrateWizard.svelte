<script lang="ts">
  // For those who had wallets before the backup phrase (decision 6, doc/unified-app.md §23): offered once at the first start
  // of this version, and from Settings → Wallet. A phrase is made (in the app's own window), a new wallet made from it on
  // this app's chain and on each sidechain it runs with it, then the coins moved, chain by chain, by an ordinary payment to
  // yourself that you confirm. The wallets of before are never touched: not their keys, not their files; never a sweep.
  import Dialog from './Dialog.svelte';
  import Icon from './Icon.svelte';
  import SafeText from './SafeText.svelte';
  import ChainBadge from './ChainBadge.svelte';
  import SeedChainList from './SeedChainList.svelte';
  import PasswordChoice from './PasswordChoice.svelte';
  import { ErrorCode } from '../lib/bridge';
  import { errorText } from '../lib/dialogs.svelte';
  import { t } from '../lib/i18n.svelte';
  import { productName, productUnit } from '../lib/modules.svelte';
  import { selectWallet, wallets } from '../lib/state.svelte';
  import { walletApi } from '../lib/wallet';
  import { formatUnits, chainHost } from '../lib/chains.svelte';
  import { allReady, refreshSeed, seed, seedHost } from '../lib/seed.svelte';

  let { onclose }: { onclose: () => void } = $props();

  type Step = 'why' | 'phrase' | 'move' | 'done';
  let open = $state(true);
  let step = $state<Step>('why');
  let making = $state(false);
  let error = $state('');
  // A password for the phrase and its wallets: recommended, not forced (doc §23.11).
  let protect = $state(true);
  const product = $derived(productName());
  const status = $derived(seed.status);
  const phraseChains = $derived((status?.chains ?? []).filter((c) => c.mode === 'phrase'));
  const phraseWallet = (id: string) => phraseChains.find((c) => c.id === id && c.state === 'ready')?.wallet ?? '';

  interface Old {
    chain: string;
    name: string;
    wallet: string;
    amount: string;
    sats?: bigint;
    state: 'to-move' | 'moving' | 'moved' | 'error';
    error?: string;
  }
  let olds = $state<Old[]>([]);
  let loading = $state(false);

  async function makePhrase() {
    making = true;
    error = '';
    try {
      const r = await seedHost.create(12, false, protect);
      if (r.cancelled) error = t('Nothing was made: make it when you are ready.');
    } catch (e) {
      error = errorText(e);
    } finally {
      making = false;
      await refreshSeed();
    }
  }

  /** The wallets of before that hold coins, on each chain that has a wallet of the phrase now. */
  async function findOld() {
    loading = true;
    const list: Old[] = [];
    const mine = phraseWallet('chains');
    if (mine) {
      // The app's pages use the wallet of the phrase from now on.
      selectWallet(mine);
      for (const w of wallets.list) {
        const b = wallets.balances[w.name];
        if (w.name !== mine && b && b.trusted > 0n) list.push({ chain: 'chains', name: product, wallet: w.name, amount: formatUnits(b.trusted, 8), sats: b.trusted, state: 'to-move' });
      }
    }
    for (const c of phraseChains) {
      if (c.id === 'chains' || !c.wallet || c.state !== 'ready') continue;
      try {
        const r = await chainHost.wallets(c.id);
        for (const w of r.wallets) {
          if (w.name !== c.wallet && w.loaded && w.trusted && Number(w.trusted) > 0) list.push({ chain: c.id, name: c.name, wallet: w.name, amount: String(w.trusted), state: 'to-move' });
        }
      } catch {
        // Its node does not answer now: nothing to move from it listed.
      }
    }
    olds = list;
    loading = false;
  }

  async function move(o: Old) {
    o.state = 'moving';
    o.error = '';
    const to = phraseWallet(o.chain);
    try {
      if (o.chain === 'chains') {
        const address = await walletApi(to).newAddress(t('Moved from %1', o.wallet), '');
        const api = walletApi(o.wallet);
        const prepared = await api.prepare([{ address, amount: o.sats ?? 0n, subtractFee: true, label: t('To your backup phrase wallet') }], {}, { confTarget: 6 });
        try {
          await api.commit(prepared.token);
        } catch (e) {
          await api.discard(prepared.token).catch(() => undefined);
          throw e;
        }
      } else {
        const { address } = await chainHost.receive(o.chain, to, t('Moved from %1', o.wallet));
        await chainHost.send({ stem: o.chain, wallet: o.wallet, address, amount: o.amount, subtractFee: true });
      }
      o.state = 'moved';
    } catch (e) {
      const code = e && typeof e === 'object' && 'code' in e ? (e as { code: number }).code : 0;
      o.state = code === ErrorCode.CANCELLED ? 'to-move' : 'error';
      o.error = code === ErrorCode.CANCELLED ? t('You declined: nothing was sent.') : errorText(e);
    }
  }

  async function later() {
    await seedHost.dismiss('offer').catch(() => undefined);
    close();
  }

  function close() {
    open = false;
    onclose();
  }
</script>

<Dialog bind:open title={t('One backup phrase for all your chains')} wide onclose={close}>
  <div class="wizard" data-wizard="migrate" data-step={step}>
    {#if step === 'why'}
      <h3>{t('Keep everything safe with 12 words')}</h3>
      <p>{t('This version of %1 can keep all your wallets safe with one backup phrase: 12 words on paper bring back your coins on %1 and on every sidechain, on any computer.', product)}</p>
      <ul class="plain">
        <li><Icon name="key" size={16} /> {t('A phrase is made, and a new wallet from it on %1 and on each sidechain you run.', product)}</li>
        <li><Icon name="send" size={16} /> {t('Then you move your coins into it, one chain at a time: an ordinary payment to yourself, which you confirm each time.')}</li>
        <li><Icon name="lock" size={16} /> {t('Your wallets of before stay exactly as they are, keys and files: nothing is taken from them without your yes.')}</li>
      </ul>
      <p class="muted small">{t('Not now? Settings → Wallet → Backup phrase keeps this for later.')}</p>
    {:else if step === 'phrase'}
      <h3>{t('Your backup phrase')}</h3>
      {#if status?.exists}
        <p class="made"><Icon name="check" size={18} /> {t('Your backup phrase is made. The new wallets:')}</p>
        <SeedChainList chains={phraseChains} />
        {#if !allReady(status)}<p class="muted small">{t('Being made… a sidechain that is off gets its wallet when it is turned on.')}</p>{/if}
      {:else}
        <p>{t('The app shows you 12 words in a window of its own. Write them on paper, in order; it then asks for three of them back.')}</p>
        <PasswordChoice bind:protect disabled={making} />
        <div><button type="button" class="btn primary" onclick={makePhrase} disabled={making} data-action="make-phrase"><Icon name="key" size={16} /> {making ? t('In the app’s window…') : t('Make my backup phrase…')}</button></div>
      {/if}
      {#if error}<p class="error" role="alert"><SafeText text={error} /></p>{/if}
    {:else if step === 'move'}
      <h3>{t('Move your coins')}</h3>
      {#if loading}
        <p class="muted">{t('Looking at your wallets…')}</p>
      {:else if !olds.length}
        <p data-nothing-to-move>{t('Nothing to move: your wallets of before hold no coins that can be spent now.')}</p>
      {:else}
        <p class="muted small">{t('Each one is a payment to your new wallet on the same chain, its fee taken from it. You confirm each in the app’s own window.')}</p>
        <ul class="olds">
          {#each olds as o (o.chain + '/' + o.wallet)}
            <li data-old={o.chain + '/' + o.wallet} data-state={o.state}>
              <ChainBadge id={o.chain} name={o.name} small />
              <span class="what"><span class="mono"><SafeText text={o.wallet} /></span> <Icon name="arrow" size={13} /> <span class="mono"><SafeText text={phraseWallet(o.chain)} /></span></span>
              <span class="num">{o.amount} {productUnit()}</span>
              {#if o.state === 'moved'}<span class="ok"><Icon name="check" size={14} /> {t('Sent')}</span>
              {:else}<button type="button" class="btn small" disabled={o.state === 'moving'} onclick={() => move(o)} data-action="move-old">{o.state === 'moving' ? t('Working…') : t('Move…')}</button>{/if}
              {#if o.error}<span class="error small"><SafeText text={o.error} /></span>{/if}
            </li>
          {/each}
        </ul>
      {/if}
    {:else}
      <h3>{t('Done')}</h3>
      <p>{t('Your coins arrive in your new wallets with the next blocks. Your wallets of before are still here, keys and all: once empty you may close them in Manage wallets; keep any backup you made of them.')}</p>
    {/if}
  </div>

  {#snippet actions()}
    {#if step === 'why'}
      <button type="button" class="btn ghost" onclick={later} data-action="later">{t('Not now')}</button>
      <button type="button" class="btn primary" onclick={() => (step = 'phrase')} data-action="next">{t('Make my backup phrase')}</button>
    {:else if step === 'phrase'}
      <button type="button" class="btn ghost" onclick={close}>{t('Close')}</button>
      <button type="button" class="btn primary" data-action="next" disabled={!status?.exists || !phraseWallet('chains')} onclick={() => { step = 'move'; void findOld(); }}>{t('Next')}</button>
    {:else if step === 'move'}
      <button type="button" class="btn ghost" onclick={() => void findOld()} disabled={loading}>{t('Look again')}</button>
      <button type="button" class="btn primary" data-action="next" onclick={() => (step = 'done')}>{t('Next')}</button>
    {:else}
      <button type="button" class="btn primary" onclick={close} data-action="done">{t('Done')}</button>
    {/if}
  {/snippet}
</Dialog>

<style>
  .wizard {
    display: flex;
    flex-direction: column;
    gap: 0.8rem;
    min-height: 16rem;
  }
  h3,
  p {
    margin: 0;
  }
  .plain {
    display: flex;
    flex-direction: column;
    gap: 0.55rem;
    margin: 0;
    padding: 0;
    list-style: none;
  }
  .plain li {
    display: flex;
    gap: 0.55rem;
    align-items: flex-start;
  }
  .made {
    display: flex;
    gap: 0.45rem;
    align-items: center;
    font-weight: 600;
    color: var(--positive);
  }
  .olds {
    display: flex;
    flex-direction: column;
    margin: 0;
    padding: 0;
    list-style: none;
  }
  .olds li {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: 0.35rem 0.8rem;
    padding: 0.55rem 0;
    border-bottom: 1px solid var(--line);
  }
  .what {
    display: inline-flex;
    align-items: center;
    gap: 0.35rem;
    flex: 1 1 12rem;
  }
  .ok {
    display: inline-flex;
    align-items: center;
    gap: 0.3rem;
    color: var(--positive);
    white-space: nowrap;
  }
  .small {
    font-size: 0.85rem;
  }
  .error {
    flex-basis: 100%;
    color: var(--negative);
  }
</style>
