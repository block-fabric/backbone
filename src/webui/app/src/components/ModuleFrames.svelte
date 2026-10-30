<script lang="ts">
  // The frames of the modules (doc/unified-app.md §14): one per sidechain turned on whose pages are built in and whose
  // node speaks this app's version, kept while it runs (switching back is instant, a page keeps its state). The one of
  // the module shown takes the place of the page, with no border and nothing of its own around it; the others wait,
  // hidden. Their look is the app's, sent again when it changes.
  import { look as themeLook } from '../lib/themestore.svelte';
  import { prefs } from '../lib/state.svelte';
  import { hub, refreshSidechains, sidechainHost, watchSidechains } from '../lib/sidechains.svelte';
  import { onMount } from 'svelte';
  import { loadPacks, packHost, packs, tellFrame } from '../lib/packs.svelte';
  import { activeModule, currentLook, embeddedIn, frameShown, liveModules, modules } from '../lib/modules.svelte';
  import ModuleFrame from './ModuleFrame.svelte';
  import { loadCapabilities } from '../lib/chains.svelte';
  import { addAppNotice, moduleName } from '../lib/modules.svelte';
  import { checkModuleUpdates } from '../lib/downloads.svelte';
  import { t } from '../lib/i18n.svelte';
  import { toast } from '../lib/state.svelte';

  // Updates of the modules turned on, once a run: in the bell and a notice; approved in Settings → Sidechains.
  $effect(() => {
    if (!(hub.list?.chains ?? []).some((c) => c.enabled)) return;
    void checkModuleUpdates((stem, tag, current) => {
      const title = t('Update available: %1 %2', moduleName(stem), tag);
      const text = t('Now %1. Settings → Sidechains → its program: it stops, is replaced, and starts again.', current ?? '—');
      addAppNotice(stem, title, text);
      toast('info', title, text);
    });
  });

  // The sidechains this app runs, followed for the life of the window (every page shows them now, §18; the switcher is
  // only shown once one is on).
  onMount(() => {
    watchSidechains();
    void refreshSidechains();
  });

  // What each chain can do (the app's own pages over every chain, lib/chains.svelte.ts): once a sidechain is on.
  $effect(() => {
    if ((hub.list?.chains ?? []).some((c) => c.enabled)) void loadCapabilities();
  });

  // The packs built in: asked again until known (the host may not answer before its node is ready).
  $effect(() => {
    void hub.list;
    if (!packs.loaded) void loadPacks();
  });

  // Whether each module's pages can be shown: asked again when its node starts or stops.
  $effect(() => {
    if (!packs.loaded) return;
    for (const c of hub.list?.chains ?? []) {
      if (!c.enabled || !packs.list.some((p) => p.stem === c.stem)) continue;
      const stem = c.stem;
      void c.state;
      void c.ready;
      packHost.info(stem).then(
        (i) => (modules.info[stem] = i),
        () => undefined,
      );
    }
  });

  // A module starting (its node runs, its wallet not ready yet): its state asked again until it is ready, the host's
  // events aside (one may say "running" before the node answers).
  $effect(() => {
    const waiting = (hub.list?.chains ?? []).some((c) => c.enabled && (c.state === 'starting' || (c.state === 'running' && !c.ready)));
    if (!waiting) return;
    const timer = setInterval(() => void refreshSidechains(), 2000);
    return () => clearInterval(timer);
  });

  const live = $derived(liveModules());
  const shown = $derived(activeModule());
  // Only the modules shown so far have a frame (§20.6): made at the first visit, kept after.
  const wanted = $derived(live.filter((c) => modules.opened[c.stem] || shown === c.stem || modules.embed?.stem === c.stem));
  $effect(() => {
    for (const c of wanted) if (!modules.opened[c.stem]) modules.opened[c.stem] = true;
  });

  // The app's look, to every frame, once the app has drawn it.
  $effect(() => {
    void prefs.theme;
    void prefs.customThemes;
    void prefs.fontAdjust;
    void prefs.fontFamily;
    void prefs.moneyFont;
    void prefs.uiMode;
    void themeLook.preview;
    const stems = wanted.map((c) => c.stem);
    const timer = setTimeout(() => {
      const l = currentLook();
      for (const stem of stems) tellFrame(stem, 'look', l);
    }, 30);
    return () => clearTimeout(timer);
  });

  // How the module shown is doing (the status bar), every few seconds while it is shown.
  $effect(() => {
    const stem = hub.selected;
    if (stem === 'chains') return;
    const load = () =>
      sidechainHost.overview(stem).then(
        (o) => (modules.overview[stem] = o),
        () => undefined,
      );
    void load();
    const timer = setInterval(() => {
      if (!document.hidden) void load();
    }, 5000);
    return () => clearInterval(timer);
  });
</script>

{#each wanted as c (c.stem)}
  {@const embedded = !(frameShown() && shown === c.stem) && embeddedIn(c.stem)}
  <ModuleFrame stem={c.stem} name={c.name} running={c.state === 'running' && c.ready} visible={(frameShown() && shown === c.stem) || embedded} above={!!modules.modal[c.stem]} rect={embedded ? modules.embedRect : null} />
{/each}
