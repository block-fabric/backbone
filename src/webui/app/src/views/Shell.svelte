<script lang="ts">
  // The main window once the node runs, in the layout chosen in Settings → Appearance:
  // workspace tabs or dashboard (doc/webui-layouts.md). Whatever the
  // layout: the same pages, the command palette (Ctrl+K), the notices of new transactions.
  import { untrack } from 'svelte';
  import { pageTitleSource } from '../lib/uimode.svelte';
  import CommandPalette from '../components/CommandPalette.svelte';
  import TitleStrip from '../components/TitleStrip.svelte';
  import TxNotifier from '../components/TxNotifier.svelte';
  import { closeDetail, detail } from '../lib/detail.svelte';
  import { go, nav } from '../lib/nav.svelte';
  import { prefs, wallets } from '../lib/state.svelte';
  import Dashboard from './layouts/Dashboard.svelte';
  import Workspace from './layouts/Workspace.svelte';
  import { followHistory } from '../lib/history.svelte';
  import { activeModule, clearModal, frameShown, moduleName, moduleTitle, modules } from '../lib/modules.svelte';
  import { hub } from '../lib/sidechains.svelte';

  // Back and forward: the app's (lib/history.svelte.ts), for its pages and the modules' alike.
  followHistory();
  // A page of the app's own (Settings, from a module): the module switcher goes back to the app's pages.
  $effect(() => {
    if (nav.page !== 'sidechain' && hub.selected !== 'chains') untrack(() => (hub.selected = 'chains'));
  });
  // The title strip: the module shown and its page, its wallet (the app's own page and wallet else).
  const stripTitle = $derived(activeModule() ? `${moduleName(activeModule())} · ${moduleTitle(activeModule())}` : pageTitleSource(nav.page));
  const stripDetail = $derived(activeModule() ? (modules.summaries[activeModule()]?.wallet ?? '') : wallets.current !== null ? wallets.current || 'default wallet' : '');
  // A dialog of a module's pages is open: the app's window is dimmed around its page, as for a dialog of its own.
  const dimmed = $derived(!!activeModule() && frameShown() && !!modules.modal[activeModule()]);
  // Another module or page shown: the dim of the one before goes (it cannot follow the user around).
  let dimFor = '';
  $effect(() => {
    const now = activeModule() && frameShown() ? activeModule() : '';
    untrack(() => {
      if (dimFor && dimFor !== now) clearModal(dimFor);
      dimFor = now;
    });
  });

  $effect(() => {
    detail.follow = prefs.layout === 'workspace';
  });

  // A new page: what was selected on the last one goes.
  $effect(() => {
    void nav.page;
    untrack(closeDetail);
  });

  // Every layout starts on Home (unless a page was asked for before: a payment URI opens Send).
  let started = false;
  $effect(() => {
    const page = nav.page;
    // Followed too: the page can be ready before the preferences are loaded (a fast start).
    const loaded = prefs.loaded;
    untrack(() => {
      if (page === 'overview' && !started && loaded) go('home');
      if (loaded) started = true;
    });
  });
</script>

<a class="skip" href="#content">Skip to the page</a>
<div class="frame">
  <!-- The integrated title bar: the workspace's is its top bar; the dashboard has a slim strip. -->
  {#if prefs.layout !== 'workspace'}
    <TitleStrip title={stripTitle} detail={stripDetail} />
  {/if}
  <div class="layout">
    {#if prefs.layout === 'workspace'}
      <Workspace />
    {:else}
      <Dashboard />
    {/if}
  </div>
</div>
{#if dimmed}
  <!-- The app's own way out, whatever the module's pages do: a click on the dim, or Escape, lifts it (and a module
       cannot dim again for a few seconds). The app's trusted dialog is a window of its own, above all of this. -->
  <button type="button" class="module-scrim" aria-label="Back to the app" onclick={() => clearModal(activeModule(), 3000)}></button>
{/if}
<svelte:window onkeydown={(e) => { if (dimmed && e.key === 'Escape') clearModal(activeModule(), 3000); }} />
<CommandPalette />
<TxNotifier />

<style>
  .frame {
    display: flex;
    flex-direction: column;
    height: 100%;
  }
  .layout {
    flex: 1 1 0;
    min-height: 0;
  }
  .module-scrim {
    position: fixed;
    inset: 0;
    z-index: 40;
    padding: 0;
    border: 0;
    background: var(--overlay);
    cursor: default;
  }
  .skip {
    position: fixed;
    z-index: 60;
    top: -3rem;
    left: 0.5rem;
    padding: 0.4rem 0.8rem;
    border-radius: var(--radius-s);
    background: var(--accent);
    color: var(--on-accent);
  }
  .skip:focus {
    top: 0.5rem;
  }
</style>
