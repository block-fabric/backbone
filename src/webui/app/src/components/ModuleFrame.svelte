<script lang="ts">
  // The pages of one module (doc/unified-app.md §14): its own origin, sandboxed as before (§13.5 #10), and now
  // chromeless: no border, the app's look, the size of the main area. The app owns where it is (the sidebar, back and
  // forward): it tells the frame the page to show; the frame tells it where a link of its pages went.
  import { packUrl, registerFrame, setFrameHidden, tellFrame, tellPhase } from '../lib/packs.svelte';
  import { clearModal, currentLook, modules, moduleRoute, NODE_PAGE, type EmbedRect } from '../lib/modules.svelte';

  // rect: shown inside one of the app's pages (ModuleEmbed), laid over the place it keeps for it.
  let {
    stem,
    name,
    running,
    visible,
    above = false,
    rect = null,
  }: { stem: string; name: string; running: boolean; visible: boolean; above?: boolean; rect?: EmbedRect | null } = $props();
  let frame: HTMLIFrameElement | undefined = $state();
  let loaded = $state(false);

  $effect(() => {
    const win = frame?.contentWindow ?? null;
    const mine = stem;
    registerFrame(mine, win);
    return () => registerFrame(mine, null);
  });

  function onload() {
    loaded = true;
    // Loaded again: whatever it said before (a dialog open) is gone with it.
    clearModal(stem);
    tellFrame(stem, 'look', currentLook());
    // A page loaded again starts seen: told whether it waits hidden.
    tellFrame(stem, 'visibility', { hidden: !visible });
    const r = modules.routes[stem];
    if (r && r.page !== NODE_PAGE) tellFrame(stem, 'navigate', r);
  }

  $effect(() => {
    if (loaded) tellPhase(stem, running ? 'ready' : 'init');
  });

  // Hidden (another page shown): its pages rest as in a window not seen, the newest state of its node handed to it when
  // it is shown again (doc/unified-app.md §21.3).
  $effect(() => {
    setFrameHidden(stem, !visible);
  });

  // Shown: the page it should be at (the app may have moved while it was hidden), and the keyboard follows.
  $effect(() => {
    if (!visible) clearModal(stem);
    if (!loaded || !visible) return;
    const embed = rect && modules.embed?.stem === stem ? modules.embed : null;
    const r = embed ?? moduleRoute(stem);
    if (r.page !== NODE_PAGE) tellFrame(stem, 'navigate', { page: r.page, tab: r.tab, ...(embed?.headless ? { headless: true } : {}) });
  });
</script>

<!-- Its own origin (chains-pack://<stem>/): it cannot reach this page, only send it messages. Sandboxed: no pop-ups,
     no navigation of the app, no forms. Hidden while another page is shown (it keeps running, as a tab would). -->
<iframe
  bind:this={frame}
  class="module-frame"
  class:above
  class:embedded={!!rect}
  style:top={rect ? `${rect.top}px` : undefined}
  style:left={rect ? `${rect.left}px` : undefined}
  style:width={rect ? `${rect.width}px` : undefined}
  style:height={rect ? `${rect.height}px` : undefined}
  style:clip-path={rect && (rect.clipTop || rect.clipBottom) ? `inset(${rect.clipTop}px 0 ${rect.clipBottom}px 0)` : undefined}
  hidden={!visible}
  src={packUrl(stem)}
  title={name}
  data-stem={stem}
  data-module={stem}
  sandbox="allow-scripts allow-same-origin"
  referrerpolicy="no-referrer"
  {onload}
></iframe>

<style>
  .module-frame {
    display: block;
    flex: 1 1 auto;
    width: 100%;
    min-width: 0;
    min-height: 0;
    border: 0;
    margin: 0;
    padding: 0;
    background: var(--bg);
    color-scheme: normal;
  }
  /* Inside one of the app's pages: over the place that page keeps for it. */
  .module-frame.embedded {
    position: fixed;
    z-index: 5;
    flex: none;
    border-radius: var(--radius-m, 0.7rem);
  }
  .module-frame[hidden] {
    display: none;
  }
  /* A dialog of its pages is open: above the dim the app lays over its own window. */
  .module-frame.above {
    position: relative;
    z-index: 41;
  }
</style>
