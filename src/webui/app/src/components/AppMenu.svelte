<script lang="ts" module>
  export interface MenuItem {
    label: string;
    action?: () => void;
    shortcut?: string;
    disabled?: boolean;
    checked?: boolean;
    /** A submenu: its items, shown under this one. */
    items?: MenuItem[];
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
  let { menus }: { menus: Menu[] } = $props();

  let open = $state(-1);
  let bar: HTMLDivElement | undefined = $state();

  function items(index: number): HTMLElement[] {
    return Array.from(bar?.querySelectorAll<HTMLElement>(`[data-menu="${index}"] [role^="menuitem"]:not([disabled])`) ?? []);
  }

  function openMenu(index: number, focusFirst = false) {
    open = (index + menus.length) % menus.length;
    if (focusFirst) queueMicrotask(() => items(open)[0]?.focus());
  }

  function close(refocus = true) {
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

<svelte:window onclick={onWindowClick} />

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
        <div class="dropdown" role="menu" aria-label={menu.label} tabindex="-1" onkeydown={(e) => onMenuKey(e, index)}>
          {#each menu.items as entry, i (i)}
            {#if entry === 'separator'}
              <div class="sep" role="separator"></div>
            {:else if entry.items}
              <div class="group-label" role="presentation">{entry.label}</div>
              {#each entry.items as sub (sub.label)}
                <button type="button" class="item sub" role="menuitemradio" aria-checked={!!sub.checked} disabled={sub.disabled} onclick={() => run(sub)}>
                  <span class="mark" aria-hidden="true">{sub.checked ? '●' : ''}</span>
                  <span class="grow">{sub.label}</span>
                </button>
              {/each}
            {:else}
              <button
                type="button"
                class="item"
                role={entry.checked === undefined ? 'menuitem' : 'menuitemcheckbox'}
                aria-checked={entry.checked === undefined ? undefined : entry.checked}
                disabled={entry.disabled}
                title={entry.tip}
                onclick={() => run(entry)}
              >
                <span class="mark" aria-hidden="true">{entry.checked ? '✓' : ''}</span>
                <span class="grow">{entry.label}</span>
                {#if entry.shortcut}<kbd>{entry.shortcut}</kbd>{/if}
              </button>
            {/if}
          {/each}
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
  .dropdown {
    position: fixed;
    z-index: 40;
    margin-top: 0.25rem;
    min-width: 16rem;
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
  .item.sub {
    padding-left: 0.9rem;
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
