<script lang="ts">
  // The navigation on the left: every page, in sections. Two forms:
  //  - the dashboard's: names and icons, with Home first; on a narrow window a drawer (open);
  //  - the compact layout's rail: icons with tooltips, the names on demand (prefs.sidebarExpanded).
  // At its foot: the menus of chains-qt, the theme, the text size (and the palette in the rail).
  import Brand from './Brand.svelte';
  import Icon from './Icon.svelte';
  import MenuButton from './MenuButton.svelte';
  import PaletteButton from './PaletteButton.svelte';
  import WalletPicker from './WalletPicker.svelte';
  import { css } from '../lib/actions';
  import { buildCommands, menuSections, pageAvailable, pageDisabled, type Command } from '../lib/commands.svelte';
  import { NAV_SECTIONS, pageIcon } from '../lib/layout';
  import { go, nav, PAGES, type PageId } from '../lib/nav.svelte';
  import { prefs, savePrefs, selectWallet, wallets } from '../lib/state.svelte';
  import { clampFontAdjust, THEMES, type ThemeId } from '../lib/themes';
  import { frame } from '../lib/windowframe.svelte';

  let { rail = false, open = $bindable(false) }: { rail?: boolean; open?: boolean } = $props();

  let narrow = $state(false);
  $effect(() => {
    const media = window.matchMedia('(max-width: 760px)');
    narrow = media.matches;
    const onChange = (e: MediaQueryListEvent) => (narrow = e.matches);
    media.addEventListener('change', onChange);
    return () => media.removeEventListener('change', onChange);
  });

  const collapsed = $derived(rail && (!prefs.sidebarExpanded || narrow));
  const sections = $derived([
    ...(rail ? [] : [{ title: '', pages: ['home'] as PageId[] }]),
    ...NAV_SECTIONS.map((s) => ({ title: s.title, pages: s.pages.filter(pageAvailable) })),
  ]);
  const all = $derived(buildCommands());
  const menus = $derived(menuSections(all));
  const looks = $derived([{ title: 'Theme', items: all.filter((c) => c.id.startsWith('node.theme')) }]);
  const walletChoices = $derived([
    {
      title: 'Wallet shown',
      items: wallets.list.map((w): Command => ({ id: `wallet.select.${w.name}`, group: 'Your wallet', label: w.name === '' ? 'default wallet' : w.name, checked: wallets.current === w.name, run: () => selectWallet(w.name) })),
    },
  ]);

  function visit(id: PageId) {
    go(id);
    open = false;
  }

  // Tooltips of the rail: one, placed beside the entry (the rail scrolls, so not inside it).
  let tip: { text: string; top: number; left: number } | null = $state(null);
  function showTip(e: Event, text: string) {
    if (!collapsed) return;
    const r = (e.currentTarget as HTMLElement).getBoundingClientRect();
    tip = { text, top: r.top + r.height / 2, left: r.right + 8 };
  }
  function hideTip() {
    tip = null;
  }
</script>

{#if open}
  <!-- The drawer of a narrow window: a click beside it closes it. -->
  <button type="button" class="scrim" aria-label="Close the navigation" onclick={() => (open = false)}></button>
{/if}

<svelte:window
  onkeydown={(e) => {
    if (e.key === 'Escape' && open) open = false;
  }}
/>

<nav class="sidebar" class:rail class:collapsed class:open aria-label="Pages">
  {#if rail && !narrow}
    <!-- In the compact layout's rail, the logo grows the sidebar (the page names), and shrinks it again. -->
    <Brand
      name={!collapsed}
      expanded={prefs.sidebarExpanded}
      label={prefs.sidebarExpanded ? 'Icons only' : 'Show the names of the pages'}
      onclick={() => savePrefs({ sidebarExpanded: !prefs.sidebarExpanded })}
    />
  {:else if rail || !frame.integrated}
    <!-- The dashboard's name of the app is in the integrated title bar, when there is one. -->
    <Brand name={!collapsed} onclick={() => visit(rail ? 'overview' : 'home')} />
  {/if}

  {#if !collapsed}
    <WalletPicker />
  {:else if wallets.list.length > 1}
    <div class="center"><MenuButton label="Wallet shown: {wallets.current || 'default wallet'}" icon="wallet" align="start" sections={walletChoices} /></div>
  {/if}

  <div class="scroll">
    {#each sections as section (section.title)}
      <div class="section" role="group" aria-label={section.title || 'Home'}>
        {#if section.title && !collapsed}<div class="caption">{section.title}</div>{/if}
        {#if section.title && collapsed}<div class="rule" aria-hidden="true"></div>{/if}
        {#each section.pages as id (id)}
          {@const page = PAGES[id]}
          {@const tipText = page.shortcut ? `${page.title} (${page.shortcut})` : page.title}
          <button
            type="button"
            class="entry"
            class:current={nav.page === id}
            aria-current={nav.page === id ? 'page' : undefined}
            aria-label={collapsed ? page.title : undefined}
            disabled={pageDisabled(id)}
            title={collapsed ? undefined : page.summary}
            onclick={() => visit(id)}
            onpointerenter={(e) => showTip(e, tipText)}
            onpointerleave={hideTip}
            onfocus={(e) => showTip(e, tipText)}
            onblur={hideTip}
          >
            <Icon name={pageIcon(id)} size={19} />
            {#if !collapsed}<span class="grow">{page.title}</span>{/if}
            {#if !collapsed && !page.ready}<span class="soon" title="Being rebuilt">•</span>{/if}
          </button>
        {/each}
      </div>
    {/each}
  </div>

  <div class="foot" class:column={collapsed}>
    {#if collapsed}
      <PaletteButton compact />
      <MenuButton label="Menus of chains-qt: wallet, banking, tools, window, node" sections={menus} placement="right" icon="menu" />
      <MenuButton label="Theme" icon="sun" sections={looks} placement="right" />
    {:else}
      <div class="row">
        {#if rail}<PaletteButton compact />{/if}
        <MenuButton label="Menus of chains-qt: wallet, banking, tools, window, node" sections={menus} placement="above" text="Menus" icon="menu" align="start" />
        <select class="look" aria-label="Theme" title="Theme" value={prefs.theme} onchange={(e) => savePrefs({ theme: (e.currentTarget as HTMLSelectElement).value as ThemeId })}>
          {#each THEMES as theme (theme.id)}
            <option value={theme.id}>{theme.name}</option>
          {/each}
        </select>
      </div>
    {/if}
    <div class="row" class:column={collapsed}>
      <button type="button" class="btn ghost small" title="Smaller text (Ctrl+-)" aria-label="Smaller text" onclick={() => savePrefs({ fontAdjust: clampFontAdjust(prefs.fontAdjust - 1) })}>A−</button>
      <button type="button" class="btn ghost small" title="Larger text (Ctrl++)" aria-label="Larger text" onclick={() => savePrefs({ fontAdjust: clampFontAdjust(prefs.fontAdjust + 1) })}>A+</button>
      {#if rail && !narrow}
        <button
          type="button"
          class="btn ghost small icon toggle"
          aria-expanded={prefs.sidebarExpanded}
          aria-label={prefs.sidebarExpanded ? 'Icons only' : 'Show the names of the pages'}
          title={prefs.sidebarExpanded ? 'Icons only' : 'Show the names of the pages'}
          onclick={() => savePrefs({ sidebarExpanded: !prefs.sidebarExpanded })}
        >
          <Icon name="sidebar" size={16} />
        </button>
      {/if}
    </div>
  </div>
</nav>

{#if tip}
  <div class="tip" role="tooltip" use:css={{ top: `${tip.top}px`, left: `${tip.left}px` }}>{tip.text}</div>
{/if}

<style>
  .sidebar {
    display: flex;
    flex-direction: column;
    gap: 0.35rem;
    width: 15rem;
    flex: none;
    padding: 0.7rem 0.6rem 0.6rem;
    border-right: 1px solid var(--line);
    background: var(--surface);
    min-height: 0;
  }
  .sidebar.rail:not(.collapsed) {
    width: 13.5rem;
  }
  .sidebar.collapsed {
    width: 3.7rem;
    align-items: center;
    padding-inline: 0.35rem;
  }
  .scroll {
    flex: 1;
    min-height: 0;
    overflow-y: auto;
    width: 100%;
    scrollbar-width: thin;
  }
  .center {
    display: flex;
    justify-content: center;
  }
  .caption {
    padding: 0.65rem 0.55rem 0.2rem;
    color: var(--text-faint);
    font-size: 0.8rem;
    font-weight: 600;
  }
  .rule {
    height: 1px;
    margin: 0.45rem 0.5rem;
    background: var(--line);
  }
  .section {
    display: flex;
    flex-direction: column;
    gap: 0.08rem;
  }
  .entry {
    display: flex;
    align-items: center;
    gap: 0.65rem;
    width: 100%;
    padding: 0.42rem 0.6rem;
    border: 0;
    border-radius: var(--radius-s);
    background: none;
    color: var(--text-muted);
    font: inherit;
    font-weight: 550;
    text-align: left;
    cursor: pointer;
  }
  .collapsed .entry {
    justify-content: center;
    padding: 0.55rem;
  }
  .entry:hover:not(:disabled) {
    background: var(--raised);
    color: var(--text);
  }
  .entry.current {
    background: var(--accent-soft);
    color: var(--text);
    box-shadow: inset 3px 0 0 var(--accent);
  }
  .entry:disabled {
    opacity: 0.45;
    cursor: default;
  }
  .soon {
    color: var(--text-faint);
  }
  .foot {
    display: flex;
    flex-direction: column;
    gap: 0.35rem;
    padding-top: 0.5rem;
    border-top: 1px solid var(--line);
    width: 100%;
  }
  .foot.column {
    align-items: center;
  }
  .look {
    flex: 1;
    min-width: 0;
    width: auto;
    min-height: 1.9rem;
    padding-block: 0.15rem;
    font-size: 0.88rem;
  }
  .row {
    display: flex;
    align-items: center;
    gap: 0.2rem;
  }
  .row.column {
    flex-direction: column;
  }
  .toggle {
    margin-left: auto;
  }
  .column .toggle {
    margin-left: 0;
  }
  .tip {
    position: fixed;
    z-index: 50;
    transform: translateY(-50%);
    padding: 0.3rem 0.6rem;
    border-radius: var(--radius-s);
    background: var(--text);
    color: var(--bg);
    font-size: 0.86rem;
    font-weight: 550;
    white-space: nowrap;
    pointer-events: none;
    box-shadow: var(--shadow);
  }
  .scrim {
    display: none;
  }
  @media (max-width: 760px) {
    .sidebar:not(.rail) {
      position: fixed;
      z-index: 36;
      top: 0;
      bottom: 0;
      left: 0;
      width: min(17rem, 85vw);
      transform: translateX(-105%);
      transition: transform 160ms ease-out;
      box-shadow: var(--shadow);
    }
    .sidebar.open {
      transform: none;
    }
    .scrim {
      display: block;
      position: fixed;
      inset: 0;
      z-index: 35;
      border: 0;
      background: var(--overlay);
    }
  }
</style>
