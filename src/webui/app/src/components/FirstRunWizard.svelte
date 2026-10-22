<script lang="ts">
  // The first start (doc/unified-app.md §8 "First run", §23): your wallet and its backup phrase, the network, the sidechains
  // you want with a line of what each is for, where their data goes, what happens next. It opens by itself when the app has
  // no wallet yet (lib/seed.svelte.ts autoGuide), and from Settings. The phrase is made, shown, checked and typed in the
  // app's own dialog window, never here: this page only asks for it to be made and says what became of each chain's wallet.
  import Dialog from './Dialog.svelte';
  import Icon from './Icon.svelte';
  import SafeText from './SafeText.svelte';
  import SeedChainList from './SeedChainList.svelte';
  import { ErrorCode } from '../lib/bridge';
  import { errorText } from '../lib/dialogs.svelte';
  import { t } from '../lib/i18n.svelte';
  import { productName } from '../lib/modules.svelte';
  import { hub, refreshSidechains, sidechainHost } from '../lib/sidechains.svelte';
  import { allReady, refreshSeed, seed, seedHost } from '../lib/seed.svelte';
  import { firstRunSteps, resources, totalResources, type FirstRunChoices, type FirstRunStep } from '../lib/unified';

  let { onclose, onmove = () => {} }: { onclose: () => void; onmove?: () => void } = $props();

  let open = $state(true);
  let choices = $state<FirstRunChoices>({ wallet: 'create', picked: [], storage: 'unified' });
  let step = $state<FirstRunStep>('welcome');
  let words = $state<12 | 24>(12);
  let extraWord = $state(false);
  let advanced = $state(false);
  let making = $state(false);
  let made = $state<null | 'created' | 'restored'>(null);
  let error = $state('');
  const product = $derived(productName());
  let progress = $state<{ stem: string; name: string; status: 'waiting' | 'starting' | 'on' | 'error'; error?: string }[]>([]);
  let finishing = $state(false);
  let finished = $state(false);

  const status = $derived(seed.status);
  // A phrase made before the guide opened (Settings → Sidechains → Setup guide): nothing to make again.
  const hasPhrase = $derived(!!status?.exists);
  const steps = $derived(firstRunSteps(choices));
  const index = $derived(steps.indexOf(step));
  const list = $derived(hub.list?.chains ?? []);
  const total = $derived(totalResources(list, choices.picked));
  const network = $derived(hub.list?.network ?? 'main');
  const NETWORKS: Record<string, string> = { main: 'the main network: real coins', test: 'the test network: coins for trying things out, worth nothing', testnet: 'the test network: coins for trying things out, worth nothing', signet: 'signet: a test network', regtest: 'regtest: a private test chain on this computer' };
  const TITLES: Record<FirstRunStep, string> = { welcome: 'Welcome', wallet: 'Your wallet', backup: 'Backup phrase', restore: 'Restore', network: 'Network', sidechains: 'Sidechains', storage: 'Storage', sync: 'Getting ready' };
  // The chains whose wallets the guide made or turned on: the app's own, and those picked.
  const shownChains = $derived((status?.chains ?? []).filter((c) => c.id === 'chains' || choices.picked.includes(c.id)));
  const canGo = $derived(step === 'backup' || step === 'restore' ? made !== null || hasPhrase : true);

  function toggle(stem: string, on: boolean) {
    choices.picked = on ? [...choices.picked, stem] : choices.picked.filter((s) => s !== stem);
  }

  async function makePhrase() {
    making = true;
    error = '';
    try {
      const r = choices.wallet === 'create' ? await seedHost.create(words, extraWord) : await seedHost.restore();
      if (r.cancelled) error = t('Nothing was made: make it when you are ready.');
      else made = choices.wallet === 'create' ? 'created' : 'restored';
    } catch (e) {
      error = e && typeof e === 'object' && 'code' in e && (e as { code: number }).code === ErrorCode.CANCELLED ? t('Nothing was made: make it when you are ready.') : errorText(e);
    } finally {
      making = false;
      await refreshSeed();
    }
  }

  async function finish() {
    finishing = true;
    error = '';
    const picking = list.filter((e) => choices.picked.includes(e.stem) && !e.enabled);
    // Their keys come from the phrase: it stays unlocked a while after it was made; else the passphrase is asked once.
    if (picking.length && seed.status?.exists && !seed.status.unlocked) {
      try {
        const r = await seedHost.unlock(picking.map((e) => e.stem));
        if (r.cancelled) {
          error = t('Their keys are made from your backup phrase with your passphrase: turn them on when you are ready.');
          finishing = false;
          return;
        }
      } catch (e) {
        error = errorText(e);
        finishing = false;
        return;
      }
    }
    progress = picking.map((e) => ({ stem: e.stem, name: e.name, status: 'waiting' as const }));
    for (const p of progress) {
      p.status = 'starting';
      try {
        await sidechainHost.enable(p.stem, 'shared');
        p.status = 'on';
      } catch (e) {
        p.status = 'error';
        p.error = errorText(e);
      }
    }
    await refreshSidechains();
    await refreshSeed();
    finishing = false;
    finished = true;
  }

  async function skip() {
    // Not offered again by itself: Settings → Wallet keeps it.
    if (!seed.status?.exists) await seedHost.dismiss('firstRun').catch(() => undefined);
    close();
  }

  function close() {
    open = false;
    onclose();
  }
</script>

<Dialog bind:open title={t('Set up %1', product)} wide onclose={close}>
  <div class="wizard" data-wizard="firstrun" data-step={finished ? 'done' : step}>
    <div class="bar" aria-hidden="true"><span style:width={`${((index + 1) / steps.length) * 100}%`}></span></div>
    <p class="caps where">{t('Step %1 of %2', index + 1, steps.length)} · {t(TITLES[step])}</p>

    {#if step === 'welcome'}
      <h3>{t('Welcome to %1', product)}</h3>
      <p class="lead">{t('%1 is a coin with sidechains: other chains, each good at something, that your coins can move into and back out of.', product)}</p>
      <ul class="plain">
        <li><Icon name="key" size={16} /> {t('First, your wallet: one backup phrase keeps it safe, on every chain.')}</li>
        <li><Icon name="sidechains" size={16} /> {t('Then, if you like, a few sidechains. You can add or remove them any time later in Settings.')}</li>
        <li><Icon name="clock" size={16} /> {t('A few minutes to set up; the chains download in the background after.')}</li>
      </ul>
    {:else if step === 'wallet'}
      <h3>{t('Your wallet')}</h3>
      <fieldset class="choices" disabled={made !== null || hasPhrase}>
        <legend class="sr-only">{t('Your wallet')}</legend>
        <label class="choice" class:chosen={choices.wallet === 'create'}>
          <input type="radio" name="w" checked={choices.wallet === 'create'} onchange={() => (choices.wallet = 'create')} />
          <span>
            <span class="title"><strong>{t('Create a new wallet')}</strong> <span class="badge accent">{t('recommended')}</span></span>
            <span class="muted">{t('You get a backup phrase of 12 words: the key to your coins on %1 and on every sidechain.', product)}</span>
          </span>
        </label>
        <label class="choice" class:chosen={choices.wallet === 'restore'}>
          <input type="radio" name="w" checked={choices.wallet === 'restore'} onchange={() => (choices.wallet = 'restore')} />
          <span>
            <strong>{t('Restore from a backup phrase')}</strong>
            <span class="muted">{t('Bring back a wallet you had, with its coins on %1 and on the sidechains.', product)}</span>
          </span>
        </label>
      </fieldset>
      <p class="note" data-advice="fresh-seed"><Icon name="info" size={14} /> {t('Even if you have a Bitcoin wallet, give %1 a phrase of its own: the same phrase gives the same keys on both chains, and your addresses on one could be linked to the other.', product)}</p>
    {:else if step === 'backup'}
      <h3>{t('Your backup phrase')}</h3>
      {#if made || hasPhrase}
        <p class="made" data-made><Icon name="check" size={18} /> {made === 'created' ? t('Your backup phrase is made and checked. Keep the paper safe: it is the only copy.') : t('You have a backup phrase on this computer.')}</p>
        <SeedChainList chains={shownChains} />
      {:else}
        <p>{t('The app shows you 12 words in a window of its own. Write them on paper, in order; it then asks for three of them back, and for a passphrase that protects them on this computer.')}</p>
        <ul class="plain">
          <li><Icon name="eyeoff" size={16} /> {t('Make sure nobody can see your screen.')}</li>
          <li><Icon name="edit" size={16} /> {t('Paper and a pen: the words cannot be copied, on purpose.')}</li>
        </ul>
        <button type="button" class="btn ghost small more" onclick={() => (advanced = !advanced)} aria-expanded={advanced}><Icon name={advanced ? 'chevron' : 'chevron-right'} size={14} /> {advanced ? t('Fewer options') : t('More options')}</button>
        {#if advanced}
          <div class="advanced">
            <label class="check"><input type="radio" name="words" checked={words === 12} onchange={() => (words = 12)} /> {t('12 words')}</label>
            <label class="check"><input type="radio" name="words" checked={words === 24} onchange={() => (words = 24)} /> {t('24 words')}</label>
            <label class="check"><input type="checkbox" bind:checked={extraWord} /> {t('An extra word of my own (it becomes part of the backup: lose it and the words alone bring back another, empty wallet)')}</label>
          </div>
        {/if}
        <div><button type="button" class="btn primary" onclick={makePhrase} disabled={making} data-action="make-phrase"><Icon name="key" size={16} /> {making ? t('In the app’s window…') : t('Make my backup phrase…')}</button></div>
      {/if}
      {#if error}<p class="error" role="alert"><SafeText text={error} /></p>{/if}
    {:else if step === 'restore'}
      <h3>{t('Type your backup phrase')}</h3>
      {#if made || hasPhrase}
        <p class="made" data-made><Icon name="check" size={18} /> {made === 'restored' ? t('Your backup phrase is back. Each chain is read again for your coins, in the background: this can take a while.') : t('You have a backup phrase on this computer.')}</p>
        <SeedChainList chains={shownChains} />
      {:else}
        <p>{t('The app asks for your words in a window of its own, with your passphrase. %1 and the sidechains you pick next are brought back from them, and each chain is read again for your coins.', product)}</p>
        <div><button type="button" class="btn primary" onclick={makePhrase} disabled={making} data-action="make-phrase"><Icon name="key" size={16} /> {making ? t('In the app’s window…') : t('Type my backup phrase…')}</button></div>
      {/if}
      {#if error}<p class="error" role="alert"><SafeText text={error} /></p>{/if}
    {:else if step === 'network'}
      <h3>{t('Network')}</h3>
      <p>{t('This app is on')} <strong>{t(NETWORKS[network] ?? network)}</strong>.</p>
      <p class="muted">{t('The network is chosen when the app starts. Try things on the test network first if you like: its coins are free and worth nothing. Each network keeps its own wallet and data.')}</p>
    {:else if step === 'sidechains'}
      <h3>{t('Pick the sidechains you want')}</h3>
      <p class="muted">{t('None is needed: %1 works on its own. Each one you pick runs in the background, takes some room, and gets its keys from your backup phrase.', product)}</p>
      <div class="picks">
        {#each list as e (e.stem)}
          {@const res = resources(e)}
          <label class="choice pick" class:chosen={choices.picked.includes(e.stem) || e.enabled} data-stem={e.stem}>
            <input type="checkbox" checked={choices.picked.includes(e.stem) || e.enabled} disabled={e.enabled || !e.program} onchange={(ev) => toggle(e.stem, ev.currentTarget.checked)} />
            <span>
              <strong><SafeText text={e.name} /></strong>
              <span>{t(e.description)}</span>
              <span class="muted small">{t(res.key, ...res.args)}{e.enabled ? ' · ' + t('already on') : ''}{!e.program ? ' · ' + t('not on this computer') : ''}</span>
            </span>
          </label>
        {/each}
      </div>
      {#if choices.picked.length}<p class="total">{t('Together: about %1 GB of disk and %2 MB of memory', total.diskGB, total.ramMB)}</p>{/if}
    {:else if step === 'storage'}
      <h3>{t('Where their data goes')}</h3>
      <fieldset class="choices">
        <legend class="sr-only">{t('Storage')}</legend>
        <label class="choice" class:chosen={choices.storage === 'unified'}>
          <input type="radio" name="s" checked={choices.storage === 'unified'} onchange={() => (choices.storage = 'unified')} />
          <span>
            <span class="title"><strong>{t('All together, with %1', product)}</strong> <span class="badge accent">{t('recommended')}</span></span>
            <span class="path"><SafeText text={(hub.list?.chainsDataDir ?? '') + '/sidechains'} /></span>
          </span>
        </label>
        <label class="choice" class:chosen={choices.storage === 'advanced'}>
          <input type="radio" name="s" checked={choices.storage === 'advanced'} onchange={() => (choices.storage = 'advanced')} />
          <span>
            <strong>{t('I will choose a folder for each')}</strong>
            <span class="muted">{t('For a sidechain on another disk. Set it in Settings → Sidechains, with Change, before or after it starts: its data moves with it.')}</span>
          </span>
        </label>
      </fieldset>
    {:else if step === 'sync'}
      <h3>{finished ? t('All set') : t('What happens next')}</h3>
      {#if !finished}
        <ol class="plain numbered">
          <li>{t('%1 downloads and checks its blocks. The first time, this takes a while; the app works meanwhile.', product)}</li>
          <li>{t('Each sidechain follows %1 and catches up behind it; its keys are made from your backup phrase as it starts.', product)}</li>
          <li>{t('Then move coins into a sidechain from %1, and use them there.', product)}</li>
        </ol>
      {/if}
      {#if progress.length}
        <ul class="progress">
          {#each progress as p (p.stem)}
            <li data-stem={p.stem} data-status={p.status}>
              <Icon name={p.status === 'on' ? 'check' : p.status === 'error' ? 'alert' : 'clock'} size={15} />
              <span>{p.status === 'on' ? t('%1 is on', p.name) : p.status === 'starting' ? t('%1 is starting…', p.name) : p.status === 'error' ? t('%1 did not start', p.name) : t('%1 waits its turn', p.name)}</span>
              {#if p.error}<span class="error"><SafeText text={p.error} /></span>{/if}
            </li>
          {/each}
        </ul>
      {/if}
      {#if finished && shownChains.length}
        <h4 class="caps">{t('Your wallets')}</h4>
        <SeedChainList chains={shownChains} />
        {#if !allReady(status, choices.picked)}<p class="muted small">{t('Their keys are being made as their nodes start: you can close this, it goes on.')}</p>{/if}
      {/if}
      {#if error}<p class="error" role="alert"><SafeText text={error} /></p>{/if}
    {/if}
  </div>

  {#snippet actions()}
    {#if finished}
      {#if choices.picked.length}<button type="button" class="btn" onclick={() => { close(); onmove(); }} data-action="move">{t('Move coins into a sidechain…')}</button>{/if}
      <button type="button" class="btn primary" onclick={close} data-action="done">{t('Done')}</button>
    {:else}
      <button type="button" class="btn ghost" onclick={skip} disabled={finishing || making}>{made || hasPhrase ? t('Finish later') : t('Skip for now')}</button>
      {#if index > 0}<button type="button" class="btn" disabled={finishing || making} onclick={() => (step = steps[index - 1])}>{t('Back')}</button>{/if}
      {#if index < steps.length - 1}
        <button type="button" class="btn primary" data-action="next" disabled={!canGo || making} onclick={() => (step = steps[index + 1])}>{t('Next')}</button>
      {:else}
        <button type="button" class="btn primary" data-action="finish" disabled={finishing} onclick={finish}>{choices.picked.length ? t('Turn them on') : t('Finish')}</button>
      {/if}
    {/if}
  {/snippet}
</Dialog>

<style>
  .wizard {
    display: flex;
    flex-direction: column;
    gap: 0.75rem;
    min-height: 20rem;
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
    transition: width 0.2s ease;
  }
  .where,
  h3,
  h4,
  p {
    margin: 0;
  }
  .lead {
    font-size: 1.05rem;
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
  .numbered {
    list-style: decimal;
    padding-left: 1.3rem;
  }
  .numbered li {
    display: list-item;
  }
  .choices {
    display: flex;
    flex-direction: column;
    gap: 0.6rem;
    margin: 0;
    padding: 0;
    border: 0;
  }
  .choice {
    display: flex;
    gap: 0.7rem;
    align-items: flex-start;
    padding: 0.75rem 0.9rem;
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
    gap: 0.25rem;
    min-width: 0;
  }
  .picks {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(17rem, 1fr));
    gap: 0.5rem;
    max-height: 22rem;
    overflow: auto;
  }
  .note {
    display: flex;
    gap: 0.35rem;
    align-items: flex-start;
    font-size: 0.85rem;
    color: var(--text-muted);
  }
  .made {
    display: flex;
    gap: 0.45rem;
    align-items: center;
    font-weight: 600;
    color: var(--positive);
  }
  .more {
    align-self: flex-start;
  }
  .advanced {
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
    padding-left: 0.2rem;
  }
  .check {
    display: flex;
    gap: 0.45rem;
    align-items: center;
  }
  .path {
    font-family: var(--font-mono);
    font-size: 0.8rem;
    color: var(--text-muted);
    overflow-wrap: anywhere;
  }
  .small {
    font-size: 0.85rem;
  }
  .total {
    font-weight: 600;
  }
  .progress {
    display: flex;
    flex-direction: column;
    gap: 0.35rem;
    margin: 0;
    padding: 0;
    list-style: none;
  }
  .progress li {
    display: flex;
    align-items: center;
    gap: 0.4rem;
    flex-wrap: wrap;
  }
  .error {
    color: var(--negative);
  }
  .title {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: 0.45rem;
  }
</style>
