<script lang="ts">
  // The chain every page shows (doc/unified-app.md §19): in the top bar, next to back and forward and the page's title.
  // All, or one chain (this app's own, then each sidechain turned on, with its mark and how its node is doing). Every
  // page follows it: with All, every chain; with one, only that chain, and what does not apply to it goes. Remembered
  // (prefs.chain). Ctrl+] and Ctrl+[ go to the next and the one before; the palette has each. Only there once a
  // sidechain is on.
  import { t } from '../lib/i18n.svelte';
  import { css } from '../lib/actions';
  import Icon from './Icon.svelte';
  import SafeText from './SafeText.svelte';
  import ChainBadge from './ChainBadge.svelte';
  import { ALL, chainEntries, selectedChain, setSelectedChain, type ChainEntry } from '../lib/chains.svelte';
  import { describeState } from '../lib/unified';

  const entries = $derived(chainEntries());
  const value = $derived(selectedChain(entries));
  const current = $derived(entries.find((e) => e.id === value) ?? null);

  let open = $state(false);
  let button: HTMLButtonElement | undefined = $state();
  let list: HTMLDivElement | undefined = $state();
  let pos = $state({ left: 0, top: 0 });
  const listId = `chains-${Math.random().toString(36).slice(2)}`;

  const tone = (e: ChainEntry) => (e.host ? 'ok' : e.entry ? describeState(e.entry).tone : 'off');
  const toneWords: Record<string, string> = { ok: 'Running', busy: 'Starting or stopping', warn: 'Has a problem', bad: 'Stopped: needs your attention', off: 'Off' };

  function options(): HTMLElement[] {
    return Array.from(list?.querySelectorAll<HTMLElement>('[role="option"]') ?? []);
  }
  function show() {
    if (!button) return;
    const r = button.getBoundingClientRect();
    pos = { left: Math.max(4, Math.min(r.left, window.innerWidth - 17 * 16 - 4)), top: r.bottom + 4 };
    open = true;
    queueMicrotask(() => (options().find((o) => o.getAttribute('aria-selected') === 'true') ?? options()[0])?.focus());
  }
  function close(refocus = true) {
    open = false;
    if (refocus) button?.focus();
  }
  function pick(id: string) {
    setSelectedChain(id);
    close();
  }
  function keys(e: KeyboardEvent) {
    const opts = options();
    const at = opts.indexOf(document.activeElement as HTMLElement);
    if (e.key === 'Escape' || e.key === 'Tab') {
      e.preventDefault();
      close();
    } else if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
      e.preventDefault();
      opts[(at + (e.key === 'ArrowDown' ? 1 : -1) + opts.length) % opts.length]?.focus();
    } else if (e.key === 'Home' || e.key === 'End') {
      e.preventDefault();
      (e.key === 'Home' ? opts[0] : opts[opts.length - 1])?.focus();
    }
  }
</script>

<svelte:window
  onpointerdown={(e) => {
    if (open && !list?.contains(e.target as Node) && !button?.contains(e.target as Node)) close(false);
  }}
  onresize={() => open && close(false)}
/>

{#if entries.length > 1}
  <button
    type="button"
    class="selector"
    class:one={value !== ALL}
    bind:this={button}
    aria-haspopup="listbox"
    aria-expanded={open}
    aria-controls={open ? listId : undefined}
    aria-label={t('Chain shown: %1', current ? current.name : t('All chains'))}
    aria-keyshortcuts="Control+BracketRight Control+BracketLeft"
    title={t('The chain every page shows (Ctrl+] next, Ctrl+[ before)')}
    data-global-chain={value}
    onclick={() => (open ? close() : show())}
    onkeydown={(e) => {
      if (e.key === 'ArrowDown' && !open) {
        e.preventDefault();
        show();
      }
    }}
  >
    {#if current}
      <ChainBadge id={current.id} name={current.name} small />
    {:else}
      <span class="stack" aria-hidden="true">
        {#each entries.slice(0, 4) as e (e.id)}<ChainBadge id={e.id} name={e.name} small markOnly />{/each}
      </span>
      <span class="all">{t('All chains')}</span>
    {/if}
    <Icon name="chevron" size={14} />
  </button>
{/if}

{#if open}
  <div class="list" id={listId} role="listbox" tabindex="-1" aria-label={t('Chain shown')} bind:this={list} use:css={{ left: `${pos.left}px`, top: `${pos.top}px` }} onkeydown={keys} data-global-chain-list>
    <button type="button" role="option" class="option" aria-selected={value === ALL} data-chain-option={ALL} onclick={() => pick(ALL)}>
      <span class="stack" aria-hidden="true">{#each entries.slice(0, 3) as e (e.id)}<ChainBadge id={e.id} name={e.name} small markOnly />{/each}</span>
      <span class="text"><strong>{t('All chains')}</strong><small class="muted">{t('Every page shows every chain')}</small></span>
      {#if value === ALL}<Icon name="check" size={15} />{/if}
    </button>
    <div class="sep" role="presentation"></div>
    {#each entries as e (e.id)}
      {@const tn = tone(e)}
      <button type="button" role="option" class="option" aria-selected={value === e.id} data-chain-option={e.id} onclick={() => pick(e.id)}>
        <ChainBadge id={e.id} name={e.name} markOnly />
        <span class="text"><strong><SafeText text={e.name} /></strong><small class="muted">{e.host ? t('This app\'s own chain') : t(toneWords[tn])}</small></span>
        <span class="dot {tn}" aria-hidden="true"></span>
        {#if value === e.id}<Icon name="check" size={15} />{/if}
      </button>
    {/each}
  </div>
{/if}

<style>
  .selector {
    display: inline-flex;
    align-items: center;
    gap: 0.45rem;
    flex: none;
    min-height: 2rem;
    padding: 0.2rem 0.55rem 0.2rem 0.35rem;
    border: 1px solid var(--line);
    border-radius: 999px;
    background: var(--surface-2);
    color: var(--text);
    font: inherit;
    font-size: 0.9rem;
    font-weight: 650;
    cursor: pointer;
  }
  .selector:hover {
    border-color: var(--line-strong);
    background: var(--raised);
  }
  .selector.one {
    border-color: color-mix(in srgb, var(--accent) 45%, var(--line));
  }
  .stack {
    display: inline-flex;
  }
  .stack :global(.chain-badge + .chain-badge) {
    margin-left: -0.4rem;
  }
  .stack :global(.mark) {
    box-shadow: 0 0 0 2px var(--surface-2);
  }
  .all {
    white-space: nowrap;
  }
  .list {
    position: fixed;
    z-index: 70;
    width: min(17rem, calc(100vw - 8px));
    max-height: calc(100vh - 6rem);
    overflow: auto;
    padding: 0.3rem;
    border: 1px solid var(--line-strong);
    border-radius: var(--radius-l);
    background: var(--surface);
    box-shadow: var(--shadow);
  }
  .option {
    display: flex;
    align-items: center;
    gap: 0.6rem;
    width: 100%;
    padding: 0.45rem 0.55rem;
    border: 0;
    border-radius: var(--radius-m, 0.6rem);
    background: none;
    color: var(--text);
    font: inherit;
    text-align: left;
    cursor: pointer;
  }
  .option:hover,
  .option:focus-visible {
    background: var(--raised);
    outline: none;
  }
  .option[aria-selected='true'] {
    background: var(--accent-soft);
  }
  .text {
    display: flex;
    flex-direction: column;
    flex: 1;
    min-width: 0;
  }
  .text small {
    font-size: 0.78rem;
  }
  .sep {
    height: 1px;
    margin: 0.25rem 0.4rem;
    background: var(--line);
  }
  .dot {
    width: 0.5rem;
    height: 0.5rem;
    border-radius: 50%;
    background: var(--text-faint);
  }
  .dot.ok {
    background: var(--positive);
  }
  .dot.busy,
  .dot.warn {
    background: var(--warning);
  }
  .dot.bad {
    background: var(--negative);
  }
</style>
