<script lang="ts">
  // The main window once the node runs, in the layout chosen in Settings → Appearance:
  // workspace tabs, dashboard, or compact sidebar (doc/webui-layouts.md). Whatever the
  // layout: the same pages, the command palette (Ctrl+K), the notices of new transactions.
  import { untrack } from 'svelte';
  import CommandPalette from '../components/CommandPalette.svelte';
  import TxNotifier from '../components/TxNotifier.svelte';
  import { closeDetail, detail } from '../lib/detail.svelte';
  import { go, nav } from '../lib/nav.svelte';
  import { prefs } from '../lib/state.svelte';
  import Compact from './layouts/Compact.svelte';
  import Dashboard from './layouts/Dashboard.svelte';
  import Workspace from './layouts/Workspace.svelte';

  $effect(() => {
    detail.follow = prefs.layout === 'workspace';
  });

  // A new page: what was selected on the last one goes.
  $effect(() => {
    void nav.page;
    untrack(closeDetail);
  });

  // Home belongs to the dashboard, which starts on it.
  let started = false;
  $effect(() => {
    const layout = prefs.layout;
    const page = nav.page;
    untrack(() => {
      if (layout !== 'dashboard' && page === 'home') go('overview');
      else if (layout === 'dashboard' && page === 'overview' && !started && prefs.loaded) go('home');
      if (prefs.loaded) started = true;
    });
  });
</script>

<a class="skip" href="#content">Skip to the page</a>
{#if prefs.layout === 'workspace'}
  <Workspace />
{:else if prefs.layout === 'dashboard'}
  <Dashboard />
{:else}
  <Compact />
{/if}
<CommandPalette />
<TxNotifier />

<style>
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
