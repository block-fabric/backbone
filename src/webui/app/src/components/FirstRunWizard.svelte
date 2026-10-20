<script lang="ts">
  // The guide of a first start (doc/unified-app.md, "First run"): your wallet and its backup phrase,
  // the network, the sidechains you want with plain words of what each is for, where their data goes,
  // what happens next. In this preview the phrase is not made yet (the single seed is phase 3): its
  // screens are here, their words are not; the sidechains picked are turned on for real.
  import Dialog from './Dialog.svelte';
  import Icon from './Icon.svelte';
  import SafeText from './SafeText.svelte';
  import { errorText } from '../lib/dialogs.svelte';
  import { t } from '../lib/i18n.svelte';
  import { hub, refreshSidechains, sidechainHost } from '../lib/sidechains.svelte';
  import { firstRunSteps, resources, restoreWords, seedGrid, totalResources, type FirstRunChoices, type FirstRunStep } from '../lib/unified';

  let { onclose }: { onclose: () => void } = $props();

  let open = $state(true);
  let choices = $state<FirstRunChoices>({ wallet: 'create', picked: [], storage: 'unified' });
  let step = $state<FirstRunStep>('welcome');
  let wroteDown = $state(false);
  let restoreText = $state('');
  let progress = $state<{ stem: string; name: string; status: 'waiting' | 'starting' | 'on' | 'error'; error?: string }[]>([]);
  let finishing = $state(false);
  let finished = $state(false);

  const steps = $derived(firstRunSteps(choices));
  const index = $derived(steps.indexOf(step));
  const list = $derived(hub.list?.chains ?? []);
  const total = $derived(totalResources(list, choices.picked));
  const restored = $derived(restoreWords(restoreText));
  // The layout of the backup words; the words themselves come with the single seed (phase 3).
  const placeholder = seedGrid(Array.from({ length: 12 }, () => '••••••'));
  const network = $derived(hub.list?.network ?? 'main');
  const NETWORKS: Record<string, string> = { main: 'the main network: real coins', test: 'the test network: coins for trying things out, worth nothing', testnet: 'the test network: coins for trying things out, worth nothing', signet: 'signet: a test network', regtest: 'regtest: a private test chain on this computer' };
  const TITLES: Record<FirstRunStep, string> = { welcome: 'Welcome', wallet: 'Your wallet', backup: 'Backup phrase', restore: 'Restore', network: 'Network', sidechains: 'Sidechains', storage: 'Storage', sync: 'Getting ready' };

  const canGo = $derived(step === 'backup' ? wroteDown : step === 'restore' ? restored.ok : true);

  function toggle(stem: string, on: boolean) {
    choices.picked = on ? [...choices.picked, stem] : choices.picked.filter((s) => s !== stem);
  }

  async function finish() {
    finishing = true;
    progress = list.filter((e) => choices.picked.includes(e.stem) && !e.enabled).map((e) => ({ stem: e.stem, name: e.name, status: 'waiting' as const }));
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
    finishing = false;
    finished = true;
  }

  function close() {
    open = false;
    onclose();
  }
</script>

<Dialog bind:open title={t('Set up Chains')} wide onclose={close}>
  <div class="wizard" data-wizard="firstrun" data-step={step}>
    <div class="bar" aria-hidden="true"><span style:width={`${((index + 1) / steps.length) * 100}%`}></span></div>
    <p class="caps where">{t('Step %1 of %2', index + 1, steps.length)} · {t(TITLES[step])}</p>

    {#if step === 'welcome'}
      <h3>Welcome to Chains</h3>
      <p class="lead">Chains is a coin with sidechains: other chains, each good at something, that your Chains coins can move into and back out of.</p>
      <ul class="plain">
        <li><Icon name="wallet" size={16} /> First, your wallet: one backup phrase keeps all of it safe.</li>
        <li><Icon name="sidechains" size={16} /> Then, if you like, a few sidechains. You can add or remove them any time later in Settings.</li>
        <li><Icon name="clock" size={16} /> A few minutes to set up; the download of the chains runs in the background after.</li>
      </ul>
    {:else if step === 'wallet'}
      <h3>Your wallet</h3>
      <fieldset class="choices">
        <legend class="sr-only">Your wallet</legend>
        <label class="choice" class:chosen={choices.wallet === 'create'}>
          <input type="radio" name="w" checked={choices.wallet === 'create'} onchange={() => (choices.wallet = 'create')} />
          <span><strong>Create a new wallet</strong><span class="muted">You get a backup phrase of 12 words. It is the key to everything: Chains and every sidechain.</span></span>
        </label>
        <label class="choice" class:chosen={choices.wallet === 'restore'}>
          <input type="radio" name="w" checked={choices.wallet === 'restore'} onchange={() => (choices.wallet = 'restore')} />
          <span><strong>Restore from a backup phrase</strong><span class="muted">Bring back a wallet you had, with its coins on Chains and on the sidechains.</span></span>
        </label>
      </fieldset>
    {:else if step === 'backup'}
      <h3>Write these words down</h3>
      <p>On paper, in this order, and keep them somewhere safe. Anyone with them can take your coins; without them, a lost computer means lost coins.</p>
      <ol class="seed" aria-label={t('Backup phrase')}>
        {#each placeholder as w (w.n)}<li><span class="n">{w.n}</span> <span class="word">{w.word}</span></li>{/each}
      </ol>
      <p class="note"><Icon name="info" size={14} /> Preview: no words are made yet. The one backup phrase for Chains and all sidechains comes in a later version; until then each wallet keeps its own keys.</p>
      <label class="check"><input type="checkbox" bind:checked={wroteDown} /> I wrote the words down and put them somewhere safe</label>
    {:else if step === 'restore'}
      <h3>Type your backup phrase</h3>
      <p>The 12 or 24 words, in order, separated by spaces. Chains and the sidechains you pick are brought back from them; their history is read again from the chains, which can take a while.</p>
      <textarea class="words" rows="3" bind:value={restoreText} spellcheck="false" autocomplete="off" aria-label={t('Backup phrase')}></textarea>
      <p class="muted small">{t('%1 words', restored.words.length)}{restored.ok ? '' : ' · 12 or 24 are needed'}</p>
      <p class="note"><Icon name="info" size={14} /> Preview: the words are not used yet (the single seed comes in a later version).</p>
    {:else if step === 'network'}
      <h3>Network</h3>
      <p>This app is on <strong>{t(NETWORKS[network] ?? network)}</strong>.</p>
      <p class="muted">The network is chosen when the app starts. Try things on the test network first if you like: its coins are free and worth nothing. Each network keeps its own wallet and data.</p>
    {:else if step === 'sidechains'}
      <h3>Pick the sidechains you want</h3>
      <p class="muted">None is needed: Chains works on its own. Each one you pick runs in the background and takes some room.</p>
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
      <h3>Where their data goes</h3>
      <fieldset class="choices">
        <legend class="sr-only">Storage</legend>
        <label class="choice" class:chosen={choices.storage === 'unified'}>
          <input type="radio" name="s" checked={choices.storage === 'unified'} onchange={() => (choices.storage = 'unified')} />
          <span>
            <strong>All together, with Chains</strong> <span class="badge accent">recommended</span>
            <span class="path"><SafeText text={(hub.list?.chainsDataDir ?? '') + '/sidechains'} /></span>
          </span>
        </label>
        <label class="choice" class:chosen={choices.storage === 'advanced'}>
          <input type="radio" name="s" checked={choices.storage === 'advanced'} onchange={() => (choices.storage = 'advanced')} />
          <span>
            <strong>I will choose a folder for each</strong>
            <span class="muted">For a sidechain on another disk. Set it in Settings → Sidechains, with Change, before or after it starts: its data moves with it.</span>
          </span>
        </label>
      </fieldset>
    {:else if step === 'sync'}
      <h3>What happens next</h3>
      <ol class="plain numbered">
        <li>Chains downloads and checks its blocks. The first time, this takes a while; the app works meanwhile.</li>
        <li>Each sidechain follows Chains and catches up behind it. The chain switcher in the sidebar shows how each one is doing.</li>
        <li>Then move coins into a sidechain from Chains (Sidechains → Move coins) and use them there.</li>
      </ol>
      {#if progress.length}
        <ul class="progress">
          {#each progress as p (p.stem)}
            <li data-stem={p.stem} data-status={p.status}>
              <Icon name={p.status === 'on' ? 'check' : p.status === 'error' ? 'alert' : 'clock'} size={15} />
              <SafeText text={p.name} />: {p.status === 'on' ? t('on') : p.status === 'starting' ? t('starting…') : p.status === 'error' ? t('not started') : t('waiting')}
              {#if p.error}<span class="error"><SafeText text={p.error} /></span>{/if}
            </li>
          {/each}
        </ul>
      {/if}
    {/if}
  </div>

  {#snippet actions()}
    {#if finished}
      <button type="button" class="btn primary" onclick={close}>Done</button>
    {:else}
      <button type="button" class="btn ghost" onclick={close} disabled={finishing}>Skip for now</button>
      {#if index > 0}<button type="button" class="btn" disabled={finishing} onclick={() => (step = steps[index - 1])}>Back</button>{/if}
      {#if index < steps.length - 1}
        <button type="button" class="btn primary" data-action="next" disabled={!canGo} onclick={() => (step = steps[index + 1])}>Next</button>
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
  .where {
    margin: 0;
  }
  h3 {
    margin: 0;
  }
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
  .seed {
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    gap: 0.45rem;
    margin: 0;
    padding: 0;
    list-style: none;
  }
  .seed li {
    padding: 0.45rem 0.6rem;
    border: 1px solid var(--line);
    border-radius: var(--radius-m);
    background: var(--surface-2);
    font-family: var(--font-mono);
  }
  .seed .n {
    color: var(--text-faint);
    margin-right: 0.3rem;
  }
  .word {
    color: var(--text-muted);
    letter-spacing: 0.1em;
  }
  .words {
    width: 100%;
    font-family: var(--font-mono);
  }
  .note {
    display: flex;
    gap: 0.35rem;
    align-items: flex-start;
    font-size: 0.85rem;
    color: var(--text-muted);
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
</style>
