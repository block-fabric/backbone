<script lang="ts">
  // Settings → Sidechains (doc/unified-app.md): the sidechains this app can run for you, each off
  // until you turn it on. On: its node runs in the background, wired to Chains. Off: it stops; its data
  // and keys stay (delete them apart). Its data goes with the data of Chains unless you choose a folder.
  import Icon from './Icon.svelte';
  import SafeText from './SafeText.svelte';
  import EnableWizard from './EnableWizard.svelte';
  import ModuleProgram from './ModuleProgram.svelte';
  import { downloads } from '../lib/downloads.svelte';
  import FirstRunWizard from './FirstRunWizard.svelte';
  import { confirm, errorText } from '../lib/dialogs.svelte';
  import { t } from '../lib/i18n.svelte';
  import { prefs, savePrefs, toast } from '../lib/state.svelte';
  import { simpleMode } from '../lib/uimode.svelte';
  import { chainColor } from '../lib/chaincolors';
  import { hub, refreshSidechains, selectChain, sidechainHost, watchSidechains } from '../lib/sidechains.svelte';
  import { describeState, resources, type SidechainEntry } from '../lib/unified';

  let busy = $state<Record<string, boolean>>({});
  let programOpen = $state<Record<string, boolean>>({});

  $effect(() => {
    watchSidechains();
    void refreshSidechains();
  });

  const list = $derived(hub.list?.chains ?? []);
  const anyOn = $derived(list.some((c) => c.enabled));

  async function toggle(e: SidechainEntry, box: HTMLInputElement) {
    if (box.checked) {
      // Turned on through its wizard: what it is, where its data goes, its wallet.
      box.checked = false;
      hub.wizard = { kind: 'enable', stem: e.stem };
      return;
    }
    const ok = await confirm(t('Turn %1 off?', e.name), t('Its node stops. Its data and its keys stay on this computer: turned on again, it carries on where it was.'), { yes: 'Turn off' });
    if (!ok) {
      box.checked = true;
      return;
    }
    busy[e.stem] = true;
    try {
      await sidechainHost.disable(e.stem);
    } catch (err) {
      toast('error', t('Sidechains'), errorText(err));
    } finally {
      busy[e.stem] = false;
      await refreshSidechains();
    }
  }

  async function changeFolder(e: SidechainEntry) {
    busy[e.stem] = true;
    try {
      const chosen = await sidechainHost.chooseDataDir(e.stem);
      if (!chosen) return;
      if (chosen.problem) {
        toast('error', t('Sidechains'), t(chosen.problem));
        return;
      }
      const move = e.hasData && (await confirm(t('Move the data of %1?', e.name), t('Move what it downloaded so far to the new folder (it is copied, then removed here, when the folder is on another disk)? Without moving, it starts again from nothing there.'), { yes: 'Move it', no: 'Start anew there' }));
      await sidechainHost.setDataDir(e.stem, chosen.path, move);
      toast('info', t('Sidechains'), t('%1 keeps its data in the new folder.', e.name));
    } catch (err) {
      toast('error', t('Sidechains'), errorText(err));
    } finally {
      busy[e.stem] = false;
      await refreshSidechains();
    }
  }

  async function resetFolder(e: SidechainEntry) {
    busy[e.stem] = true;
    try {
      const move = e.hasData && (await confirm(t('Move the data of %1 back?', e.name), t('Move it back with the data of Chains?'), { yes: 'Move it', no: 'Leave it there' }));
      await sidechainHost.setDataDir(e.stem, null, move);
    } catch (err) {
      toast('error', t('Sidechains'), errorText(err));
    } finally {
      busy[e.stem] = false;
      await refreshSidechains();
    }
  }

  async function deleteData(e: SidechainEntry) {
    busy[e.stem] = true;
    try {
      // The desktop asks (the page cannot answer for the user); the wallets stay.
      if (await sidechainHost.deleteData(e.stem, false)) toast('info', t('Sidechains'), t('The chain data of %1 was deleted; its wallet stays.', e.name));
    } catch (err) {
      toast('error', t('Sidechains'), errorText(err));
    } finally {
      busy[e.stem] = false;
      await refreshSidechains();
    }
  }
</script>

<section class="panel sidechain-settings">
  <div class="panel-body intro">
    <div class="grow">
      <h2>Sidechains</h2>
      <p class="muted">
        Chains runs on its own. Turn on any of these sidechains and this app also runs its node in the background, connected to your Chains node, with one window for all of
        them. You can turn one off at any time: its data and its keys stay.
      </p>
    </div>
    <button type="button" class="btn" onclick={() => (hub.wizard = { kind: 'firstrun' })}><Icon name="book" size={16} /> Setup guide</button>
  </div>
</section>

{#if hub.error}<p class="error" role="alert"><SafeText text={hub.error} /></p>{/if}

<div class="cards">
  {#each list as e (e.stem)}
    {@const st = describeState(e)}
    {@const res = resources(e)}
    <section class="panel card" data-stem={e.stem} class:on={e.enabled}>
      <div class="panel-body">
        <div class="top">
          <span class="mark" aria-hidden="true" style:background={e.enabled ? chainColor(e.stem) : undefined}><SafeText text={e.name.slice(0, 1)} /></span>
          <div class="grow">
            <h3><SafeText text={e.name} /></h3>
            <p class="muted desc">{t(e.description)}</p>
          </div>
          <label class="switch" title={e.enabled ? t('Turn off') : t('Turn on')}>
            <input type="checkbox" checked={e.enabled} disabled={busy[e.stem] || e.state === 'stopping'} aria-label={t('Run %1', e.name)} onchange={(ev) => toggle(e, ev.currentTarget)} />
            <span class="track"></span>
          </label>
        </div>
        <div class="facts">
          <span class="badge state {st.tone}" data-state={e.state}>{t(st.key, ...st.args)}</span>
          <span class="muted">{t(res.key, ...res.args)}</span>
          {#if e.enabled && e.state === 'running'}<button type="button" class="link" onclick={() => selectChain(e.stem)}>Show it</button>{/if}
        </div>
        {#if e.error}<p class="error small"><SafeText text={e.error} /></p>{/if}
        {#if !e.program}<p class="hint small"><Icon name="info" size={14} /> Its program ({e.stem}d) is not on this computer yet: turning it on downloads it from its releases.</p>{/if}
        {#if e.enabled || e.program}
          <details class="program-box" open={!!downloads.updates[e.stem]} data-program-box={e.stem} ontoggle={(ev) => (programOpen[e.stem] = (ev.currentTarget as HTMLDetailsElement).open)}>
            <summary>{t('Its program and its updates')}{#if downloads.updates[e.stem]}{' '}<span class="badge accent">{t('Update available')}</span>{/if}</summary>
            <!-- Asked of its releases only once opened (they are on GitHub: no request for every card shown). -->
            {#if programOpen[e.stem] || downloads.updates[e.stem]}<ModuleProgram stem={e.stem} name={e.name} mode="settings" />{/if}
          </details>
        {/if}
        <div class="where">
          <Icon name="folder" size={15} />
          <span class="grow path" title={e.dataDir}><SafeText text={e.dataDir} /></span>
          {#if e.customDataDir}<span class="badge">your folder</span>{/if}
          <button type="button" class="btn ghost small" disabled={e.enabled || busy[e.stem]} title={e.enabled ? t('Turn it off to move its data') : ''} onclick={() => changeFolder(e)}>Change</button>
          {#if e.customDataDir}<button type="button" class="btn ghost small" disabled={e.enabled || busy[e.stem]} onclick={() => resetFolder(e)}>Default</button>{/if}
          {#if !e.enabled && e.hasData}<button type="button" class="btn ghost small danger" disabled={busy[e.stem]} onclick={() => deleteData(e)}><Icon name="trash" size={14} /> Delete data</button>{/if}
        </div>
      </div>
    </section>
  {/each}
</div>

{#if !anyOn && list.length > 0}
  <p class="muted foot">Nothing more runs while every sidechain is off: no extra node, no extra memory.</p>
{/if}

{#if !simpleMode()}
  <!-- For debugging (doc/unified-app.md §20.1): each sidechain's whole own navigation under its group of the sidebar. -->
  <section class="panel sidebar-option">
    <div class="panel-body">
      <label class="check">
        <input id="opt-sidebar-all-pages" type="checkbox" checked={prefs.sidebarAllPages} onchange={(e) => savePrefs({ sidebarAllPages: (e.currentTarget as HTMLInputElement).checked })} />
        <span>{t('Show every page of each sidechain in the sidebar')}</span>
      </label>
      <p class="hint">{t('Its group then also lists "All its pages": everything its own app has. Useful for testing; off, the sidebar keeps only what is each chain\'s own.')}</p>
    </div>
  </section>
{/if}

{#if hub.wizard?.kind === 'enable'}
  {@const stem = hub.wizard?.stem}
  {@const target = list.find((c) => c.stem === stem)}
  {#if target}<EnableWizard entry={target} onclose={() => (hub.wizard = null)} />{/if}
{:else if hub.wizard?.kind === 'firstrun'}
  <FirstRunWizard onclose={() => (hub.wizard = null)} />
{/if}

<style>
  .sidebar-option {
    margin-top: 0.9rem;
  }
  .sidebar-option .panel-body {
    padding-top: 1rem;
  }
  .sidebar-option .hint {
    margin: 0.35rem 0 0 1.6rem;
  }
  .program-box {
    margin: 0.4rem 0;
    padding: 0.5rem 0.7rem;
    border: 1px solid var(--line);
    border-radius: var(--radius-m);
  }
  .program-box summary {
    cursor: pointer;
    font-weight: 600;
  }
  .program-box[open] summary {
    margin-bottom: 0.5rem;
  }
  .intro {
    display: flex;
    align-items: flex-start;
    gap: 1rem;
    padding-top: 1.1rem;
  }
  .intro h2 {
    margin: 0 0 0.3rem;
  }
  .intro p {
    margin: 0;
    max-width: 48rem;
  }
  .cards {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(24rem, 1fr));
    gap: 0.9rem;
    margin-top: 0.9rem;
  }
  .card .panel-body {
    display: flex;
    flex-direction: column;
    gap: 0.6rem;
    padding-top: 1rem;
  }
  .card.on {
    border-color: var(--accent);
  }
  .top {
    display: flex;
    align-items: flex-start;
    gap: 0.75rem;
  }
  .top h3 {
    margin: 0;
  }
  .desc {
    margin: 0.15rem 0 0;
    font-size: 0.92rem;
  }
  .mark {
    display: inline-grid;
    place-items: center;
    width: 2.2rem;
    height: 2.2rem;
    flex: none;
    border-radius: 0.7rem;
    background: var(--raised);
    font-weight: 800;
  }
  /* On: its colour, as its mark everywhere else (lib/chaincolors.ts). */
  .card.on .mark {
    color: #fff;
  }
  .facts,
  .where {
    display: flex;
    align-items: center;
    flex-wrap: wrap;
    gap: 0.5rem;
    font-size: 0.9rem;
  }
  .where {
    padding-top: 0.5rem;
    border-top: 1px solid var(--line);
  }
  .path {
    font-family: var(--font-mono);
    font-size: 0.8rem;
    color: var(--text-muted);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .state.ok {
    background: var(--accent-soft);
    color: var(--positive);
  }
  .state.busy,
  .state.warn {
    background: var(--warning-soft);
    color: var(--warning);
  }
  .state.bad {
    background: var(--danger-soft);
    color: var(--negative);
  }
  .small {
    font-size: 0.85rem;
    margin: 0;
  }
  .hint {
    display: flex;
    gap: 0.35rem;
    align-items: center;
    color: var(--text-muted);
  }
  .error {
    color: var(--negative);
    overflow-wrap: anywhere;
  }
  .foot {
    margin-top: 0.9rem;
  }
  .link {
    background: none;
    border: 0;
    padding: 0;
    color: var(--link);
    cursor: pointer;
    font: inherit;
  }
</style>
