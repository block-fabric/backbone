<script lang="ts" module>
  export interface MenuItem {
    label: string;
    action?: () => void;
    shortcut?: string;
    disabled?: boolean;
    checked?: boolean;
    /** One of a group of which one is chosen (a radio button), rather than a box to tick. */
    radio?: boolean;
    /** A submenu: its items, shown under this one as a group. */
    items?: MenuEntry[];
    tip?: string;
  }
  export type MenuEntry = MenuItem | 'separator';
  export interface Menu {
    label: string;
    items: MenuEntry[];
  }
</script>

<script lang="ts">
  // The menu bar, with the menus of chains-qt. Keyboard: arrows move, Enter runs, Escape closes.
  // The status tip of the item under the pointer, or with the focus, shows in the status bar.
  import { statusTip } from '../lib/state.svelte';
  // The menus that do not fit in the width of the window go in a last one, "More", as groups.
  let { menus: all }: { menus: Menu[] } = $props();

  let open = $state(-1);
  let bar: HTMLDivElement | undefined = $state();
  let measure: HTMLDivElement | undefined = $state();
  let available = $state(10000);
  let widths: number[] = $state([]);
  let moreWidth = $state(70);

  $effect(() => {
    void all.map((m) => m.label).join();
    if (!measure) return;
    const tops = [...measure.querySelectorAll<HTMLElement>('.top')];
    widths = tops.slice(0, all.length).map((t) => t.getBoundingClientRect().width);
    moreWidth = tops[all.length]?.getBoundingClientRect().width || 70;
  });
  $effect(() => {
    const holder = bar?.parentElement;
    if (!holder || typeof ResizeObserver === 'undefined') return;
    // The room of the menus: the bar, less what else is on it (the buttons of a framed window, and some
    // room for its title).
    const inner = () => {
      const cs = getComputedStyle(holder);
      const gap = parseFloat(cs.columnGap || '0') || 0;
      let others = 0;
      for (const child of holder.children) {
        if (child === bar || child === measure) continue;
        const el = child as HTMLElement;
        others += (el.classList.contains('title') ? 8 * parseFloat(getComputedStyle(document.documentElement).fontSize || '16') : el.getBoundingClientRect().width) + gap;
      }
      return holder.clientWidth - parseFloat(cs.paddingLeft || '0') - parseFloat(cs.paddingRight || '0') - others;
    };
    const o = new ResizeObserver(() => (available = inner()));
    const watch = () => {
      o.disconnect();
      o.observe(holder);
      for (const child of holder.children) if (child !== bar && child !== measure) o.observe(child);
      available = inner();
    };
    // The buttons of the frame come and go with the option: the room is measured again.
    const m = new MutationObserver(watch);
    m.observe(holder, { childList: true });
    watch();
    return () => {
      o.disconnect();
      m.disconnect();
    };
  });

  /** How many menus fit, with room for "More" when not all do (jsdom measures nothing: all fit). */
  const fit = $derived.by(() => {
    if (widths.length !== all.length || widths.every((w) => w === 0)) return all.length;
    const total = widths.reduce((a, b) => a + b, 0);
    if (total <= available) return all.length;
    let used = moreWidth;
    let k = 0;
    while (k < all.length && used + widths[k] <= available) used += widths[k++];
    return k;
  });
  const menus: Menu[] = $derived(
    fit >= all.length ? all : [...all.slice(0, fit), { label: 'More', items: all.slice(fit).map((m) => ({ label: m.label, items: m.items })) }],
  );

  /** Where the open menu shows: under its button, inside the window, scrolled when it is too long. */
  let place = $state('');
  $effect(() => {
    if (open < 0 || !bar) return;
    const top = bar.querySelectorAll<HTMLElement>('.top')[open];
    if (!top) return;
    const r = top.getBoundingClientRect();
    const width = Math.min(18 * 16, window.innerWidth - 16);
    const left = Math.max(8, Math.min(r.left, window.innerWidth - width - 8));
    place = `left: ${left}px; top: ${r.bottom + 4}px; max-height: ${Math.max(160, window.innerHeight - r.bottom - 16)}px;`;
  });

  function items(index: number): HTMLElement[] {
    return Array.from(bar?.querySelectorAll<HTMLElement>(`[data-menu="${index}"] [role^="menuitem"]:not([disabled])`) ?? []);
  }

  function openMenu(index: number, focusFirst = false) {
    open = (index + menus.length) % menus.length;
    if (focusFirst) queueMicrotask(() => items(open)[0]?.focus());
  }

  function close(refocus = true) {
    statusTip.text = '';
    const was = open;
    open = -1;
    if (refocus && was >= 0) bar?.querySelectorAll<HTMLElement>('.top')[was]?.focus();
  }

  function run(item: MenuItem) {
    if (item.disabled || !item.action) return;
    close(false);
    item.action();
  }

  function onMenuKey(e: KeyboardEvent, index: number) {
    const list = items(index);
    const at = list.indexOf(document.activeElement as HTMLElement);
    if (e.key === 'ArrowDown') list[(at + 1) % list.length]?.focus();
    else if (e.key === 'ArrowUp') list[(at - 1 + list.length) % list.length]?.focus();
    else if (e.key === 'ArrowRight') openMenu(index + 1, true);
    else if (e.key === 'ArrowLeft') openMenu(index - 1, true);
    else if (e.key === 'Escape') close();
    else if (e.key === 'Home') list[0]?.focus();
    else if (e.key === 'End') list[list.length - 1]?.focus();
    else return;
    e.preventDefault();
  }

  function onTopKey(e: KeyboardEvent, index: number) {
    if (e.key === 'ArrowDown' || e.key === 'Enter' || e.key === ' ') {
      e.preventDefault();
      openMenu(index, true);
    } else if (e.key === 'ArrowRight' || e.key === 'ArrowLeft') {
      e.preventDefault();
      const tops = bar?.querySelectorAll<HTMLElement>('.top');
      const next = (index + (e.key === 'ArrowRight' ? 1 : -1) + menus.length) % menus.length;
      if (open >= 0) openMenu(next, true);
      else tops?.[next]?.focus();
    }
  }

  function onWindowClick(e: MouseEvent) {
    if (open >= 0 && bar && !bar.contains(e.target as Node)) close(false);
  }
</script>

<svelte:window onclick={onWindowClick} onresize={() => open >= 0 && close(false)} />

{#snippet entries(list: MenuEntry[], depth: number)}
  {#each list as entry, i (i)}
    {#if entry === 'separator'}
      <div class="sep" role="separator"></div>
    {:else if entry.items}
      <div class="group-label" role="presentation" style="padding-left: {1.6 + depth * 0.8}rem">{entry.label}</div>
      {@render entries(entry.items, depth + 1)}
    {:else}
      <button
        type="button"
        class="item"
        role={entry.checked === undefined ? 'menuitem' : entry.radio ? 'menuitemradio' : 'menuitemcheckbox'}
        aria-checked={entry.checked === undefined ? undefined : entry.checked}
        disabled={entry.disabled}
        title={entry.tip}
        onmouseenter={() => (statusTip.text = entry.tip ?? '')}
        onmouseleave={() => (statusTip.text = '')}
        onfocus={() => (statusTip.text = entry.tip ?? '')}
        onblur={() => (statusTip.text = '')}
        style={depth > 0 ? `padding-left: ${0.3 + depth * 0.6}rem` : undefined}
        onclick={() => run(entry)}
      >
        <span class="mark" aria-hidden="true">{entry.checked ? (entry.radio ? '●' : '✓') : ''}</span>
        <span class="grow">{entry.label}</span>
        {#if entry.shortcut}<kbd>{entry.shortcut}</kbd>{/if}
      </button>
    {/if}
  {/each}
{/snippet}

<!-- The widths of the menus, measured where nobody sees them. -->
<div class="measure" aria-hidden="true" bind:this={measure}>
  {#each all as menu (menu.label)}<span class="top">{menu.label}</span>{/each}<span class="top">More</span>
</div>

<div class="menubar" role="menubar" aria-label="Menu" bind:this={bar}>
  {#each menus as menu, index (menu.label)}
    <div class="holder" data-menu={index}>
      <button
        type="button"
        class="top"
        class:active={open === index}
        role="menuitem"
        aria-haspopup="menu"
        aria-expanded={open === index}
        tabindex={index === 0 ? 0 : -1}
        onclick={() => (open === index ? close(false) : openMenu(index))}
        onmouseenter={() => {
          if (open >= 0 && open !== index) openMenu(index);
        }}
        onkeydown={(e) => onTopKey(e, index)}
      >
        {menu.label}
      </button>
      {#if open === index}
        <div class="dropdown" role="menu" aria-label={menu.label} tabindex="-1" style={place} onkeydown={(e) => onMenuKey(e, index)}>
          {@render entries(menu.items, 0)}
        </div>
      {/if}
    </div>
  {/each}
</div>

<style>
  .menubar {
    display: flex;
    align-items: stretch;
    gap: 0.1rem;
    min-width: 0;
    overflow-x: auto;
    scrollbar-width: none;
  }
  .holder {
    position: relative;
  }
  .top {
    height: 100%;
    padding: 0.3rem 0.65rem;
    border: 0;
    border-radius: var(--radius-s);
    background: none;
    color: var(--text-muted);
    font: inherit;
    font-size: 0.93rem;
    white-space: nowrap;
    cursor: pointer;
  }
  .top:hover,
  .top.active {
    background: var(--raised);
    color: var(--text);
  }
  .measure {
    position: absolute;
    visibility: hidden;
    pointer-events: none;
    white-space: nowrap;
    height: 0;
    overflow: hidden;
  }
  .measure .top {
    display: inline-block;
  }
  .dropdown {
    position: fixed;
    z-index: 40;
    min-width: min(16rem, calc(100vw - 16px));
    max-width: calc(100vw - 16px);
    overflow-y: auto;
    padding: 0.35rem;
    border: 1px solid var(--line-strong);
    border-radius: var(--radius-m);
    background: var(--surface);
    box-shadow: var(--shadow);
  }
  .item {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    width: 100%;
    padding: 0.4rem 0.6rem 0.4rem 0.3rem;
    border: 0;
    border-radius: var(--radius-s);
    background: none;
    color: var(--text);
    font: inherit;
    text-align: left;
    cursor: pointer;
  }
  .item:hover:not(:disabled),
  .item:focus-visible {
    background: var(--accent-soft);
    outline: none;
  }
  .item:disabled {
    color: var(--text-faint);
    cursor: default;
  }
  .mark {
    width: 1rem;
    text-align: center;
    color: var(--accent-text);
    font-size: 0.8rem;
  }
  .sep {
    height: 1px;
    margin: 0.3rem 0.4rem;
    background: var(--line);
  }
  .group-label {
    padding: 0.4rem 0.6rem 0.2rem 1.6rem;
    color: var(--text-muted);
    font-size: 0.85rem;
    font-weight: 600;
  }
</style>
