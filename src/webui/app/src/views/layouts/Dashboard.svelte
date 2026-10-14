<script lang="ts">
  // Dashboard: a home of live cards (views/home/Home.svelte), every page in the sidebar, a light
  // bar above the page (where you are, the palette), the detail panel, the status bar.
  import DetailPanel from '../../components/DetailPanel.svelte';
  import Icon from '../../components/Icon.svelte';
  import PaletteButton from '../../components/PaletteButton.svelte';
  import Sidebar from '../../components/Sidebar.svelte';
  import StatusBar from '../../components/StatusBar.svelte';
  import { NAV_SECTIONS } from '../../lib/layout';
  import { go, nav, PAGES } from '../../lib/nav.svelte';
  import PageView from '../PageView.svelte';

  let drawer = $state(false);
  const section = $derived(NAV_SECTIONS.find((s) => s.pages.includes(nav.page))?.title ?? '');
</script>

<div class="dashboard">
  <Sidebar bind:open={drawer} />
  <div class="main">
    <header class="top">
      <button type="button" class="btn ghost icon small drawer" aria-label="Pages" aria-expanded={drawer} onclick={() => (drawer = true)}><Icon name="menu" size={18} /></button>
      <nav class="crumbs" aria-label="Where you are">
        {#if nav.page === 'home'}
          <span class="here" aria-current="page">Home</span>
        {:else}
          <button type="button" class="link" onclick={() => go('home')}><Icon name="home" size={15} /> Home</button>
          <span class="sep" aria-hidden="true">/</span>
          {#if section}<span class="muted">{section}</span><span class="sep" aria-hidden="true">/</span>{/if}
          <span class="here" aria-current="page">{PAGES[nav.page].title}</span>
        {/if}
      </nav>
      <PaletteButton />
    </header>
    <div class="body">
      <main class="content" id="content" tabindex="-1" data-page={nav.page}>
        <PageView />
      </main>
      <DetailPanel />
    </div>
    <StatusBar />
  </div>
</div>

<style>
  .dashboard {
    display: flex;
    height: 100%;
  }
  .main {
    flex: 1;
    min-width: 0;
    display: flex;
    flex-direction: column;
  }
  .top {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    min-height: 2.9rem;
    padding: 0.3rem 0.9rem;
    border-bottom: 1px solid var(--line);
    background: var(--surface);
  }
  .drawer {
    display: none;
  }
  .crumbs {
    flex: 1;
    min-width: 0;
    display: flex;
    align-items: center;
    gap: 0.45rem;
    white-space: nowrap;
    overflow: hidden;
  }
  .link {
    display: inline-flex;
    align-items: center;
    gap: 0.3rem;
    padding: 0.15rem 0.35rem;
    border: 0;
    border-radius: var(--radius-s);
    background: none;
    color: var(--text-muted);
    font: inherit;
    cursor: pointer;
  }
  .link:hover {
    background: var(--raised);
    color: var(--text);
  }
  .sep {
    color: var(--text-faint);
  }
  .here {
    font-weight: 650;
  }
  .body {
    flex: 1;
    min-height: 0;
    display: flex;
  }
  .content {
    flex: 1;
    min-width: 0;
    overflow: auto;
    outline: none;
  }
  @media (max-width: 760px) {
    .drawer {
      display: inline-flex;
    }
    .top :global(.palette-button) {
      min-width: 0;
    }
    .top :global(.palette-button .grow),
    .top :global(.palette-button kbd) {
      display: none;
    }
  }
</style>
