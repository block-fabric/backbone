<script lang="ts">
  // The wizard that turns one sidechain on (doc/unified-app.md, "Enable a sidechain"): what it is and
  // what it takes, where its data goes, its wallet, staking for a chain that has it, then a review.
  import { untrack } from 'svelte';
  import Dialog from './Dialog.svelte';
  import Icon from './Icon.svelte';
  import SafeText from './SafeText.svelte';
  import ModuleProgram from './ModuleProgram.svelte';
  import { productName } from '../lib/modules.svelte';
  import { errorText } from '../lib/dialogs.svelte';
  import { t } from '../lib/i18n.svelte';
  import { refreshSidechains, selectChain, sidechainHost } from '../lib/sidechains.svelte';
  import { refreshSeed, seed, seedHost } from '../lib/seed.svelte';
  import SeedChainList from './SeedChainList.svelte';
  import ZsideNote from './ZsideNote.svelte';
  import { canContinue, defaultChoices, diskCheck, enablePlan, enableSteps, nextStep, previousStep, resources, type EnableStep, type SidechainEntry } from '../lib/unified';

  let { entry, onclose }: { entry: SidechainEntry; onclose: () => void } = $props();

  const steps = $derived(enableSteps(entry));
  let step = $state<EnableStep>('about');
  // The choices start from how the sidechain is set now (the wizard is made anew for each one).
  let choices = $state(untrack(() => defaultChoices(entry)));
  let phase = $state<'asking' | 'working' | 'done'>('asking');
  let error = $state('');
  let open = $state(true);

  const TITLES: Record<EnableStep, string> = { about: 'What it is', program: 'Its program', storage: 'Where its data goes', wallet: 'Its wallet', staking: 'Staking', review: 'Ready' };
  const res = $derived(resources(entry));
  const room = $derived(diskCheck(entry.diskGB, choices.storage === 'custom' ? choices.customFreeGB : entry.freeGB));

  async function choose() {
    error = '';
    try {
      const chosen = await sidechainHost.chooseDataDir(entry.stem);
      if (!chosen) return;
      choices.customPath = chosen.path;
      choices.customProblem = chosen.problem;
      choices.customFreeGB = chosen.freeGB;
    } catch (e) {
      error = errorText(e);
    }
  }

  const hasPhrase = $derived(!!seed.status?.exists);
  // zSide: its wallet is always its own (its shielded keys are not made from the phrase, doc §23.13).
  const ownOnly = $derived(entry.phraseKeys === false);
  $effect(() => {
    if (!seed.status) void refreshSeed();
  });

  async function turnOn() {
    phase = 'working';
    error = '';
    const plan = enablePlan(entry, choices);
    // Its keys come from the backup phrase: the passphrase is asked once now (in the app's own window), the keys are made
    // as soon as its node answers (doc §23).
    if (plan.wallet === 'shared' && hasPhrase && !seed.status?.unlocked && seed.status?.password !== false) {
      try {
        const r = await seedHost.unlock([entry.stem]);
        if (r.cancelled) {
          error = t('Its keys are made from your backup phrase with your password: turn it on when you are ready, or choose a separate wallet.');
          phase = 'asking';
          return;
        }
      } catch (e) {
        error = errorText(e);
        phase = 'asking';
        return;
      }
    }
    try {
      if (plan.setDir) await sidechainHost.setDataDir(entry.stem, plan.setDir.dir, plan.setDir.move);
      await sidechainHost.enable(entry.stem, plan.wallet);
      phase = 'done';
    } catch (e) {
      error = errorText(e);
      phase = 'asking';
    } finally {
      await refreshSidechains();
    }
  }

  function close() {
    open = false;
    onclose();
  }
</script>

<Dialog bind:open title={t('Turn on %1', entry.name)} wide onclose={close}>
  <div class="wizard" data-wizard="enable" data-step={phase === 'done' ? 'done' : step}>
    {#if phase !== 'done'}
      <ol class="steps" aria-label={t('Steps')}>
        {#each steps as s, i (s)}
          <li class:current={s === step} class:past={steps.indexOf(step) > i}><span class="n">{i + 1}</span> {t(TITLES[s])}</li>
        {/each}
      </ol>
    {/if}

    {#if phase === 'done'}
      <div class="done">
        <span class="big" aria-hidden="true"><Icon name="check" size={28} /></span>
        <h3><SafeText text={entry.name} /> is on</h3>
        <p>{t('Its node is starting in the background. It catches up with %1 by itself; that can take a while the first time. It is in the chain selector at the top, and in the sidebar under Chain.', productName())}</p>
        {#if choices.wallet === 'shared' && hasPhrase && !ownOnly}
          {@const mine = (seed.status?.chains ?? []).filter((c) => c.id === entry.stem)}
          {#if mine.length}<SeedChainList chains={mine} />{/if}
        {/if}
        {#if ownOnly}<ZsideNote actions />{/if}
      </div>
    {:else if step === 'about'}
      <h3><SafeText text={entry.name} /></h3>
      <p class="lead">{t(entry.description)}</p>
      <ul class="plain">
        <li><Icon name="nodes" size={16} /> This app runs its node in the background, whenever the app runs. It connects to your Chains node on this computer by itself: nothing to set up.</li>
        <li><Icon name="download" size={16} /> {t(res.key, ...res.args)}. It downloads its own blocks the first time.</li>
        <li><Icon name="power" size={16} /> You can turn it off at any time. Its data and keys stay, until you delete them.</li>
      </ul>
      <p class="room {room.tone}"><Icon name={room.tone === 'ok' ? 'check' : 'alert'} size={15} /> {t(room.key, ...room.args)}</p>
    {:else if step === 'program'}
      <ModuleProgram stem={entry.stem} name={entry.name} onready={(r) => (choices.programReady = r || !!entry.program)} />
    {:else if step === 'storage'}
      <fieldset class="choices">
        <legend class="sr-only">Where its data goes</legend>
        <label class="choice" class:chosen={choices.storage === 'default'}>
          <input type="radio" name="storage" checked={choices.storage === 'default'} onchange={() => (choices.storage = 'default')} />
          <span>
            <span class="title"><strong>With the data of Chains</strong> <span class="badge accent">recommended</span></span>
            <span class="path"><SafeText text={entry.defaultDataDir} /></span>
            <span class="muted">One place for everything: easy to back up and to find.</span>
          </span>
        </label>
        <label class="choice" class:chosen={choices.storage === 'custom'}>
          <input type="radio" name="storage" checked={choices.storage === 'custom'} onchange={() => (choices.storage = 'custom')} />
          <span>
            <strong>A folder of my choice</strong>
            <span class="muted">For example on another disk with more room.</span>
            {#if choices.storage === 'custom'}
              <span class="row pick">
                <button type="button" class="btn small" onclick={choose}><Icon name="folder" size={15} /> Choose a folder…</button>
                {#if choices.customPath}<span class="path"><SafeText text={choices.customPath} /></span>{/if}
              </span>
              {#if choices.customProblem}<span class="error">{t(choices.customProblem)}</span>{/if}
              {#if entry.hasData}
                <label class="check"><input type="checkbox" bind:checked={choices.move} /> Move what it downloaded so far</label>
              {/if}
            {/if}
          </span>
        </label>
      </fieldset>
      <p class="room {room.tone}"><Icon name={room.tone === 'ok' ? 'check' : 'alert'} size={15} /> {t(room.key, ...room.args)}</p>
    {:else if step === 'wallet' && ownOnly}
      <div class="own-only" data-wallet-own-only>
        <h3>{t('A wallet of its own')}</h3>
        <ZsideNote />
        <p class="muted">{t('%1 makes its own keys when it starts, its private (shielded) ones too. Back its wallet up from here once it runs, or from Settings → Wallet; keep that backup with your phrase.', entry.name)}</p>
      </div>
    {:else if step === 'wallet'}
      <fieldset class="choices">
        <legend class="sr-only">Its wallet</legend>
        <label class="choice" class:chosen={choices.wallet === 'shared'}>
          <input type="radio" name="wallet" checked={choices.wallet === 'shared'} onchange={() => (choices.wallet = 'shared')} />
          <span>
            <span class="title"><strong>{t('Use my backup phrase')}</strong> <span class="badge accent">{t('recommended')}</span></span>
            <span class="muted">{t('One backup phrase for %1 and every sidechain: if this computer is lost, the phrase brings all of them back.', productName())}</span>
            {#if hasPhrase}
              <span class="note"><Icon name="key" size={14} /> {seed.status?.password === false ? t('Its keys are made from your phrase as it starts.') : t('Its keys are made from your phrase as it starts: you type your wallet password once, in the app’s own window.')}</span>
            {:else}
              <span class="note"><Icon name="info" size={14} /> {t('You have no backup phrase yet (Settings → Wallet → Backup phrase): until you make one, %1 gets a wallet of its own.', entry.name)}</span>
            {/if}
          </span>
        </label>
        <label class="choice" class:chosen={choices.wallet === 'separate'}>
          <input type="radio" name="wallet" checked={choices.wallet === 'separate'} onchange={() => (choices.wallet = 'separate')} />
          <span>
            <strong>A separate wallet for <SafeText text={entry.name} /></strong>
            <span class="muted">{t('Its own keys, apart from %1 and your phrase. It needs its own backup.', productName())}</span>
          </span>
        </label>
      </fieldset>
    {:else if step === 'staking'}
      <label class="choice" class:chosen={choices.stake}>
        <input type="checkbox" bind:checked={choices.stake} disabled />
        <span>
          <strong>Stake on this chain</strong>
          <span class="muted">Lock some of your coins on <SafeText text={entry.name} /> to help make its blocks, and earn part of its fees. You can stop and get them back later.</span>
          <span class="note"><Icon name="info" size={14} /> {t('Turn staking on from its Staking page once it runs (Staking, in the sidebar). Its validator key is made on this computer, never from your backup phrase.')}</span>
        </span>
      </label>
    {:else if step === 'review'}
      <h3>Ready to turn <SafeText text={entry.name} /> on</h3>
      <dl class="review">
        <dt>Data</dt>
        <dd class="path"><SafeText text={choices.storage === 'custom' ? choices.customPath : entry.defaultDataDir} /></dd>
        <dt>Wallet</dt>
        <dd>{ownOnly ? t('Its own wallet (its private funds need its own backup)') : choices.wallet === 'shared' ? (hasPhrase ? t('My backup phrase') : t('My backup phrase (keys of its own for now)')) : t('A separate wallet')}</dd>
        {#if entry.staking}<dt>Staking</dt><dd>Off</dd>{/if}
        <dt>Takes</dt>
        <dd>{t(res.key, ...res.args)}</dd>
      </dl>
    {/if}

    {#if error}<p class="error" role="alert"><SafeText text={error} /></p>{/if}
  </div>

  {#snippet actions()}
    {#if phase === 'done'}
      <button type="button" class="btn" onclick={close}>Close</button>
      <button
        type="button"
        class="btn primary"
        onclick={() => {
          // The stem first: once closed, the parent no longer gives this wizard its sidechain.
          const stem = entry.stem;
          close();
          selectChain(stem);
        }}>Show it</button
      >
    {:else}
      <button type="button" class="btn ghost" onclick={close} disabled={phase === 'working'}>Cancel</button>
      {#if previousStep(steps, step)}<button type="button" class="btn" disabled={phase === 'working'} onclick={() => (step = previousStep(steps, step) ?? step)}>Back</button>{/if}
      {#if step === 'review'}
        <button type="button" class="btn primary" data-action="turn-on" disabled={phase === 'working'} onclick={turnOn}>{phase === 'working' ? t('Starting…') : t('Turn it on')}</button>
      {:else}
        <button type="button" class="btn primary" data-action="next" disabled={!canContinue(step, choices) || room.tone === 'bad'} onclick={() => (step = nextStep(steps, step) ?? step)}>Next</button>
      {/if}
    {/if}
  {/snippet}
</Dialog>

<style>
  .wizard {
    display: flex;
    flex-direction: column;
    gap: 0.8rem;
    min-height: 17rem;
  }
  .steps {
    display: flex;
    flex-wrap: wrap;
    gap: 0.4rem;
    margin: 0 0 0.3rem;
    padding: 0;
    list-style: none;
    font-size: 0.85rem;
    color: var(--text-muted);
  }
  .steps li {
    display: flex;
    align-items: center;
    gap: 0.35rem;
    padding: 0.2rem 0.6rem 0.2rem 0.25rem;
    border-radius: 999px;
    background: var(--surface-2);
  }
  .steps .n {
    display: inline-grid;
    place-items: center;
    width: 1.3rem;
    height: 1.3rem;
    border-radius: 50%;
    background: var(--raised);
    font-weight: 700;
    font-size: 0.75rem;
  }
  .steps li.current {
    background: var(--accent-soft);
    color: var(--accent-strong);
    font-weight: 600;
  }
  .steps li.current .n,
  .steps li.past .n {
    background: var(--accent);
    color: var(--on-accent);
  }
  h3 {
    margin: 0.2rem 0 0;
  }
  .lead {
    margin: 0;
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
  .room {
    display: flex;
    align-items: center;
    gap: 0.4rem;
    margin: 0;
    font-size: 0.9rem;
  }
  .room.ok {
    color: var(--positive);
  }
  .room.warn {
    color: var(--warning);
  }
  .room.bad {
    color: var(--negative);
  }
  .own-only {
    display: flex;
    flex-direction: column;
    gap: 0.6rem;
  }
  .own-only h3,
  .own-only p {
    margin: 0;
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
    padding: 0.8rem 0.9rem;
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
    gap: 0.3rem;
    min-width: 0;
  }
  .path {
    font-family: var(--font-mono);
    font-size: 0.8rem;
    color: var(--text-muted);
    overflow-wrap: anywhere;
  }
  .note {
    display: flex;
    gap: 0.35rem;
    align-items: flex-start;
    font-size: 0.85rem;
    color: var(--text-muted);
  }
  .pick {
    margin-top: 0.2rem;
  }
  .check {
    display: flex;
    gap: 0.4rem;
    align-items: center;
    font-size: 0.9rem;
  }
  .review {
    display: grid;
    grid-template-columns: max-content 1fr;
    gap: 0.45rem 1rem;
    margin: 0;
  }
  .review dt {
    color: var(--text-muted);
  }
  .review dd {
    margin: 0;
  }
  .done {
    display: flex;
    flex-direction: column;
    align-items: center;
    text-align: center;
    gap: 0.5rem;
    padding: 1rem 0;
  }
  .done p {
    max-width: 30rem;
    margin: 0;
  }
  .big {
    display: inline-grid;
    place-items: center;
    width: 3.2rem;
    height: 3.2rem;
    border-radius: 50%;
    background: var(--accent);
    color: var(--on-accent);
  }
  .error {
    color: var(--negative);
    overflow-wrap: anywhere;
  }
  .title {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: 0.45rem;
  }
</style>
