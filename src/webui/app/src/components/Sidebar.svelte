<script lang="ts">
  // The navigation on the left, in the sections of chains-qt: the wallet, the chain, the node, the look.
  import Icon from './Icon.svelte';
  import { go, nav, PAGES, type PageId } from '../lib/nav.svelte';
  import { app, prefs, savePrefs, selectWallet, wallets, walletsEnabled } from '../lib/state.svelte';
  import { clampFontAdjust, THEMES, type ThemeId } from '../lib/themes';
  import { narrowWindow } from '../lib/media.svelte';
  import { pendingNow } from '../lib/settingsdraft.svelte';

  const sections: { title: string; pages: PageId[] }[] = [
    { title: 'Wallet', pages: ['overview', 'send', 'receive', 'transactions'] },
    { title: 'Chain', pages: ['sidechains', 'sidechain-nodes', 'mine', 'explorer'] },
    { title: 'Node', pages: ['node', 'settings'] },
  ];

  const noWallet = $derived(wallets.list.length === 0);
  // Icons only when the user wants it, or when the window is narrow (the page keeps its room).
  const compact = $derived(prefs.compactNav || narrowWindow.matches);
</script>

<nav class="sidebar" class:compact={compact} aria-label="Pages">
  <div class="brand">
    <svg class="mark" viewBox="0 0 32 32" aria-hidden="true">
      <rect x="3" y="9" width="12" height="14" rx="3.5" />
      <rect x="17" y="9" width="12" height="14" rx="3.5" />
      <path d="M11 16h10" />
    </svg>
    {#if !compact}
      <span class="name">Chains</span>
      {#if app.info?.networkLabel}<span class="badge warning net">{app.info.networkLabel}</span>{/if}
    {/if}
  </div>

  {#if wallets.list.length > 1 && compact}
    <!-- Icons only: the wallet chosen in a narrow box (its list opens as usual). -->
    <label class="wallet narrow" title="The wallet shown: {wallets.current === '' ? 'default wallet' : (wallets.current ?? '')}">
      <span class="sr-only">Wallet</span>
      <select value={wallets.current ?? ''} onchange={(e) => selectWallet((e.currentTarget as HTMLSelectElement).value)}>
        {#each wallets.list as w (w.name)}
          <option value={w.name} translate={w.name === '' ? undefined : 'no'}>{w.name === '' ? 'default wallet' : w.name}</option>
        {/each}
      </select>
    </label>
  {/if}
  {#if wallets.list.length > 0 && !compact}
    <label class="wallet">
      <span class="sr-only">Wallet</span>
      <select title="The wallet shown" value={wallets.current ?? ''} onchange={(e) => selectWallet((e.currentTarget as HTMLSelectElement).value)}>
        {#each wallets.list as w (w.name)}
          <option value={w.name} translate={w.name === '' ? undefined : 'no'}>{w.name === '' ? 'default wallet' : w.name}</option>
        {/each}
      </select>
    </label>
  {/if}

  {#each sections as section (section.title)}
    <div class="section">
      {#if !compact}<div class="caption">{section.title}</div>{/if}
      {#each section.pages.filter((id) => walletsEnabled() || !PAGES[id].wallet) as id (id)}
        {@const page = PAGES[id]}
        <button
          type="button"
          class="entry"
          class:current={nav.page === id}
          aria-current={nav.page === id ? 'page' : undefined}
          disabled={page.wallet && noWallet}
          title={page.shortcut ? `${page.summary} (${page.shortcut})` : page.summary}
          onclick={() => go(id)}
        >
          <Icon name={page.icon} size={19} />
          {#if !compact}<span class="grow">{page.title}</span>{/if}
          {#if id === 'settings' && pendingNow() > 0}<span class="pending" title="Changes of the settings wait for Apply" aria-label="Changes wait for Apply"></span>{/if}
        </button>
      {/each}
    </div>
  {/each}

  <div class="look">
    {#if !compact}
      <label class="caption" for="look">Theme</label>
      <select id="look" value={prefs.theme} onchange={(e) => savePrefs({ theme: (e.currentTarget as HTMLSelectElement).value as ThemeId })}>
        {#each THEMES as theme (theme.id)}
          <option value={theme.id}>{theme.name}</option>
        {/each}
      </select>
    {/if}
    <div class="sizes" class:column={compact}>
      <button type="button" class="btn ghost small" title="Smaller text (Ctrl+-)" aria-label="Smaller text" onclick={() => savePrefs({ fontAdjust: clampFontAdjust(prefs.fontAdjust - 1) })}>A−</button>
      <button type="button" class="btn ghost small" title="Larger text (Ctrl++)" aria-label="Larger text" onclick={() => savePrefs({ fontAdjust: clampFontAdjust(prefs.fontAdjust + 1) })}>A+</button>
      {#if !narrowWindow.matches}<button type="button" class="btn ghost small icon" title={compact ? 'Show the names of the pages' : 'Icons only'} aria-label={compact ? 'Show the names of the pages' : 'Icons only'} onclick={() => savePrefs({ compactNav: !prefs.compactNav })}>
        <Icon name="menu" size={16} />
      </button>{/if}
    </div>
  </div>
</nav>

<style>
  .sidebar {
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
    width: 14.5rem;
    padding: 0.75rem 0.6rem;
    border-right: 1px solid var(--line);
    background: var(--surface);
    overflow-y: auto;
  }
  .sidebar.compact {
    width: 3.6rem;
    align-items: center;
  }
  .brand {
    display: flex;
    align-items: center;
    gap: 0.55rem;
    padding: 0.2rem 0.4rem 0.7rem;
  }
  .mark {
    width: 1.9rem;
    height: 1.9rem;
    fill: none;
    stroke: var(--accent);
    stroke-width: 2.4;
  }
  .name {
    font-size: 1.15rem;
    font-weight: 700;
    letter-spacing: -0.02em;
  }
  .net {
    margin-left: auto;
  }
  .wallet {
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
    padding: 0 0.4rem 0.5rem;
  }
  .wallet.narrow {
    width: 2.8rem;
    padding: 0 0 0.5rem;
  }
  .wallet.narrow select {
    padding-inline: 0.3rem;
    font-size: 0.8rem;
  }
  .caption {
    padding: 0.7rem 0.55rem 0.25rem;
    color: var(--text-faint);
    font-size: 0.8rem;
    font-weight: 600;
  }
  .section {
    display: flex;
    flex-direction: column;
    gap: 0.1rem;
  }
  .entry {
    display: flex;
    align-items: center;
    gap: 0.65rem;
    width: 100%;
    padding: 0.45rem 0.6rem;
    border: 0;
    border-radius: var(--radius-s);
    background: none;
    color: var(--text-muted);
    font: inherit;
    font-weight: 550;
    text-align: left;
    cursor: pointer;
  }
  .compact .entry {
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
  .pending {
    width: 0.5rem;
    height: 0.5rem;
    border-radius: 50%;
    background: var(--warning);
  }
  .look {
    margin-top: auto;
    padding-top: 0.8rem;
    display: flex;
    flex-direction: column;
    gap: 0.35rem;
  }
  .look .caption {
    padding: 0 0.4rem;
  }
  .look select {
    min-height: 2rem;
  }
  .sizes {
    display: flex;
    gap: 0.2rem;
  }
  .sizes.column {
    flex-direction: column;
  }
  .sizes .icon {
    margin-left: auto;
  }
  @media (max-width: 760px) {
    .sidebar:not(.compact) {
      width: 12rem;
    }
  }
</style>
