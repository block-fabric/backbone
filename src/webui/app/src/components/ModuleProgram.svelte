<script lang="ts">
  // The program of a module (doc/unified-app.md §15), in its enable wizard and in Settings → Sidechains: where it comes
  // from now, the releases of its repository this app can use (the newest proposed; one it cannot use greyed, with
  // why), the download with its progress, Cancel and Try again; in Settings, the update the user approves.
  import { t } from '../lib/i18n.svelte';
  import { errorText } from '../lib/dialogs.svelte';
  import Icon from './Icon.svelte';
  import SafeText from './SafeText.svelte';
  import { busy, downloads, fraction, moduleHost, proposedRelease, sizeText, watchDownloads, type ReleaseList } from '../lib/downloads.svelte';

  let { stem, name, mode = 'wizard', onready }: { stem: string; name: string; mode?: 'wizard' | 'settings'; onready?: (ready: boolean) => void } = $props();

  let list = $state<ReleaseList | null>(null);
  let error = $state('');
  let loading = $state(false);
  let chosen = $state('');
  let showAll = $state(false);

  async function load() {
    loading = true;
    error = '';
    try {
      list = await moduleHost.releases(stem);
      downloads.releases[stem] = list;
      if (!chosen || !list.releases.some((r) => r.tag === chosen && r.compatible)) chosen = proposedRelease(list)?.tag ?? '';
    } catch (e) {
      error = errorText(e);
    } finally {
      loading = false;
    }
  }

  $effect(() => {
    watchDownloads();
    void stem;
    void load();
    moduleHost.progress(stem).then(
      (p) => (downloads.progress[stem] = p),
      () => undefined,
    );
  });

  const progress = $derived(downloads.progress[stem]);
  const working = $derived(busy(progress));
  const ready = $derived(!!list && list.source !== 'none');
  $effect(() => onready?.(ready));
  // Done: the list again (its current version, the source).
  let lastState = '';
  $effect(() => {
    const s = progress?.state ?? '';
    if (s !== lastState && (s === 'done' || s === 'failed')) void load();
    lastState = s;
  });

  const shown = $derived(list ? (showAll ? list.releases : list.releases.slice(0, 4)) : []);

  async function install() {
    if (!chosen) return;
    error = '';
    try {
      // An older release than the one in use only by the user's own choice here (never an update found by itself).
      const older = !!list?.current && chosen < list.current;
      if (mode === 'settings' && list?.current) await moduleHost.update(stem, chosen, older);
      else await moduleHost.install(stem, chosen, older);
      downloads.progress[stem] = await moduleHost.progress(stem);
    } catch (e) {
      error = errorText(e);
    }
  }
  async function cancel() {
    try {
      await moduleHost.cancel(stem);
    } catch (e) {
      error = errorText(e);
    }
  }
  async function rollback() {
    error = '';
    try {
      await moduleHost.rollback(stem);
      await load();
    } catch (e) {
      error = errorText(e);
    }
  }
</script>

<div class="program" data-module-program={stem} data-source={list?.source ?? ''}>
  <p class="now">
    <Icon name={ready ? 'check' : 'download'} size={16} />
    <span>
      <strong>{t('Its program')}:</strong>
      {#if list?.source === 'download'}{t('Downloaded release %1', list.current ?? '')}{:else if list?.source === 'bundled'}{t('Came with this app')}{:else if list?.source === 'folder'}{t('From the folder you chose')}{:else if list}{t('Not on this computer yet')}{:else if loading}{t('Loading…')}{/if}
      {#if list?.source === 'bundled' && mode === 'wizard'}<span class="muted">· {t('A newer release can be downloaded instead.')}</span>{/if}
    </span>
  </p>

  {#if list?.updateAvailable && list.newestCompatible && mode === 'settings'}
    <p class="update" role="status" data-update-available={list.newestCompatible}><Icon name="bell" size={15} /> {t('Update available: release %1 (now %2).', list.newestCompatible, list.current ?? '—')}</p>
  {/if}

  {#if list}
    <fieldset class="releases">
      <legend class="caps">{t('Releases of %1', list.repo)}</legend>
      {#each shown as r (r.tag)}
        <label class="rel" class:off={!r.compatible} class:chosen={chosen === r.tag} data-release={r.tag} data-compatible={r.compatible ? 'yes' : 'no'}>
          <input type="radio" name="release-{stem}" value={r.tag} disabled={!r.compatible || working} checked={chosen === r.tag} onchange={() => (chosen = r.tag)} />
          <span class="what">
            <span class="line">
              <strong translate="no"><SafeText text={r.tag} /></strong>
              {#if r.tag === list.newestCompatible}<span class="badge accent">{t('newest it can use')}</span>{/if}
              {#if r.tag === list.current}<span class="badge">{t('in use')}</span>{/if}
              {#if r.prerelease}<span class="badge">{t('test release')}</span>{/if}
              {#if r.signed}<span class="badge ok" title={t('Its manifest is signed with the release key built into this app')}><Icon name="shield" size={12} /> {t('signed')}</span>{:else if r.compatible && r.unsigned}<span class="badge warn-badge" title={t('No release key is built into this app yet: checked by the checksums of its release only')}>{t('unsigned (checksums only)')}</span>{/if}
              {#if r.asset}<span class="muted small"><SafeText text={sizeText(r.asset.size)} /></span>{/if}
            </span>
            {#if !r.compatible}<span class="why small"><SafeText text={r.reason} /></span>{/if}
          </span>
        </label>
      {/each}
      {#if list.releases.length > 4}<button type="button" class="link small" onclick={() => (showAll = !showAll)}>{showAll ? t('Fewer') : t('All %1 releases', list.releases.length)}</button>{/if}
      {#if list.releases.length === 0}<p class="muted small">{t('No release found.')}</p>{/if}
    </fieldset>
  {/if}

  {#if progress && progress.state !== 'idle'}
    <div class="progress {progress.state}" data-progress={progress.state} role="status" aria-live="polite">
      <span class="label">{#if progress.state === 'listing'}{t('Looking at the releases…')}{:else if progress.state === 'downloading'}{t('Downloading…')}{:else if progress.state === 'verifying'}{t('Checking what came…')}{:else if progress.state === 'unpacking'}{t('Unpacking…')}{:else if progress.state === 'installing'}{t('Installing…')}{:else if progress.state === 'done'}{t('Installed')}{:else if progress.state === 'failed'}{t('It did not work')}{:else}{t('Cancelled')}{/if} <span translate="no"><SafeText text={progress.tag} /></span></span>
      {#if progress.state === 'downloading'}
        <progress max="1" value={fraction(progress)} aria-label={t('Download')}></progress>
        <span class="num small"><SafeText text={`${sizeText(progress.done)} / ${sizeText(progress.total)}`} /></span>
      {/if}
      {#if progress.error}<span class="error small"><SafeText text={progress.error} /></span>{/if}
    </div>
  {/if}

  <div class="row">
    {#if working}
      <button type="button" class="btn" data-action="cancel-download" onclick={cancel}>{t('Cancel')}</button>
    {:else if chosen}
      {#if mode === 'wizard' || !list?.current || chosen !== list.current}
        <button type="button" class="btn primary" data-action="download" onclick={install} disabled={!chosen}>
          <Icon name="download" size={15} />
          {#if progress?.state === 'failed' || progress?.state === 'cancelled'}{t('Try again')}{:else if mode === 'settings' && list?.current}{t('Update to %1', chosen)}{:else}{t('Download %1', chosen)}{/if}
        </button>
      {/if}
    {/if}
    <button type="button" class="btn ghost" data-action="check-updates" disabled={loading || working} onclick={load}>{t('Check for updates')}</button>
    {#if mode === 'settings' && list?.previous && !working}
      <button type="button" class="btn ghost" data-action="rollback" onclick={rollback}>{t('Back to %1', list.previous)}</button>
    {/if}
  </div>
  {#if mode === 'wizard'}
    <p class="faint small">{t('From the releases of %1 on GitHub: checked against the release\'s checksums before anything is unpacked; only its node program is kept, in the data folder of this app.', name)}</p>
  {/if}
  {#if error}<p class="error" role="alert"><SafeText text={error} /></p>{/if}
</div>

<style>
  .program {
    display: flex;
    flex-direction: column;
    gap: 0.6rem;
  }
  .now,
  .update {
    display: flex;
    align-items: center;
    gap: 0.45rem;
    margin: 0;
  }
  .update {
    padding: 0.45rem 0.7rem;
    border-radius: var(--radius-m);
    background: var(--accent-soft);
    color: var(--accent-strong);
    font-weight: 600;
  }
  .releases {
    display: flex;
    flex-direction: column;
    gap: 0.35rem;
    margin: 0;
    padding: 0;
    border: 0;
  }
  .releases legend {
    margin-bottom: 0.3rem;
  }
  .rel {
    display: flex;
    gap: 0.6rem;
    align-items: flex-start;
    padding: 0.5rem 0.7rem;
    border: 1px solid var(--line);
    border-radius: var(--radius-m);
    cursor: pointer;
  }
  .rel.chosen {
    border-color: var(--accent);
    background: var(--accent-soft);
  }
  .rel.off {
    opacity: 0.7;
    cursor: default;
  }
  .what {
    display: flex;
    flex-direction: column;
    gap: 0.2rem;
    min-width: 0;
  }
  .line {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: 0.4rem;
  }
  .why {
    color: var(--warning);
  }
  .warn-badge {
    color: var(--warning);
  }
  .badge.ok {
    color: var(--positive);
  }
  .progress {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: 0.6rem;
    padding: 0.5rem 0.7rem;
    border-radius: var(--radius-m);
    background: var(--surface-2);
  }
  .progress.done .label {
    color: var(--positive);
  }
  .progress.failed .label {
    color: var(--negative);
  }
  progress {
    flex: 1 1 10rem;
    height: 0.5rem;
    appearance: none;
    border: 0;
    border-radius: 999px;
    background: var(--raised);
    overflow: hidden;
  }
  progress::-webkit-progress-bar {
    background: var(--raised);
  }
  progress::-webkit-progress-value {
    background: var(--accent);
  }
  .row {
    display: flex;
    flex-wrap: wrap;
    gap: 0.4rem;
  }
  .small {
    font-size: 0.84rem;
  }
  .link {
    align-self: flex-start;
    padding: 0;
    border: 0;
    background: none;
    color: var(--accent-strong);
    cursor: pointer;
    font: inherit;
  }
  .error {
    color: var(--negative);
    overflow-wrap: anywhere;
  }
</style>
