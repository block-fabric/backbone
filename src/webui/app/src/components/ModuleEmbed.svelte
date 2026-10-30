<script lang="ts">
  // A page of a module inside one of the app's own pages (doc/unified-app.md §18), where the app has no view of its own
  // for it (a chain's DEX, its bonds): the place is kept here, and the module's frame (its own origin, sandboxed, talking
  // only by messages: §13.5 #10, unchanged) is laid over it. Its questions are asked in the app's trusted dialog as
  // always. Without the module's pages (not built in, its node not ready), a way to its own pages instead.
  import { onDestroy } from 'svelte';
  import { t } from '../lib/i18n.svelte';
  import Icon from './Icon.svelte';
  import SafeText from './SafeText.svelte';
  import { liveModules, modules, openModule, setEmbed } from '../lib/modules.svelte';
  import { hub } from '../lib/sidechains.svelte';

  // bare: the page is the chain's page alone (the explorer): no border of its own. headless: under the title of the app's
  // page (Staking): the chain's page without its own title.
  let {
    stem,
    page,
    tab = '',
    name,
    height = '34rem',
    bare = false,
    headless = false,
  }: { stem: string; page: string; tab?: string; name: string; height?: string; bare?: boolean; headless?: boolean } = $props();

  let place: HTMLDivElement | undefined = $state();
  const live = $derived(liveModules().some((c) => c.stem === stem) && !!hub.list?.chains.find((c) => c.stem === stem && c.state === 'running' && c.ready));

  function measure() {
    if (!place) return;
    const r = place.getBoundingClientRect();
    // Only the part inside the page's own area: scrolled past, the frame does not cover the header or the status bar.
    const area = place.closest('main')?.getBoundingClientRect() ?? { top: 0, bottom: window.innerHeight };
    const clipTop = Math.max(0, Math.round(area.top - r.top));
    const clipBottom = Math.max(0, Math.round(r.bottom - area.bottom));
    const next = { top: Math.round(r.top), left: Math.round(r.left), width: Math.round(r.width), height: Math.round(r.height), clipTop, clipBottom };
    const now = modules.embedRect;
    if (!now || (Object.keys(next) as (keyof typeof next)[]).some((k) => now[k] !== next[k])) modules.embedRect = next;
  }

  $effect(() => {
    if (!live || !place) return;
    setEmbed({ stem, page, tab, headless });
    measure();
    const observer = new ResizeObserver(measure);
    observer.observe(place);
    window.addEventListener('scroll', measure, true);
    window.addEventListener('resize', measure);
    // A move of the page around it that no event tells (a panel opening): looked at a few times a second.
    const timer = setInterval(measure, 300);
    return () => {
      observer.disconnect();
      window.removeEventListener('scroll', measure, true);
      window.removeEventListener('resize', measure);
      clearInterval(timer);
      setEmbed(null);
    };
  });
  onDestroy(() => {
    if (modules.embed?.stem === stem) setEmbed(null);
  });
</script>

<div class="module-embed" class:bare bind:this={place} style:height data-module-embed={stem} data-embed-page={page}>
  {#if !live}
    <div class="fallback">
      <Icon name="modules" size={22} />
      <p class="muted">{t('The pages of %1 are not ready here yet.', name)}</p>
      <button type="button" class="btn small" onclick={() => openModule(stem, page, tab)}>{t('Open in %1', name)}</button>
    </div>
  {:else}
    <span class="sr-only"><SafeText text={name} /></span>
  {/if}
</div>

<style>
  .module-embed {
    position: relative;
    width: 100%;
    border: 1px solid var(--line);
    border-radius: var(--radius-m, 0.7rem);
    background: var(--bg);
    min-height: 12rem;
  }
  .module-embed.bare {
    border: 0;
    border-radius: 0;
    background: transparent;
  }
  .fallback {
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    gap: 0.6rem;
    height: 100%;
  }
  .sr-only {
    position: absolute;
    width: 1px;
    height: 1px;
    overflow: hidden;
    clip: rect(0 0 0 0);
  }
</style>
