<script lang="ts">
  // The pages of one module (doc/unified-app.md §14): its own origin, sandboxed as before (§13.5 #10), and now
  // chromeless: no border, the app's look, the size of the main area. The app owns where it is (the sidebar, back and
  // forward): it tells the frame the page to show; the frame tells it where a link of its pages went.
  import { packUrl, registerFrame, tellFrame, tellPhase } from '../lib/packs.svelte';
  import { clearModal, currentLook, modules, moduleRoute, NODE_PAGE } from '../lib/modules.svelte';

  let { stem, name, running, visible, above = false }: { stem: string; name: string; running: boolean; visible: boolean; above?: boolean } = $props();
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
    const r = modules.routes[stem];
    if (r && r.page !== NODE_PAGE) tellFrame(stem, 'navigate', r);
  }

  $effect(() => {
    if (loaded) tellPhase(stem, running ? 'ready' : 'init');
  });

  // Shown: the page it should be at (the app may have moved while it was hidden), and the keyboard follows.
  $effect(() => {
    if (!visible) clearModal(stem);
    if (!loaded || !visible) return;
    const r = moduleRoute(stem);
    if (r.page !== NODE_PAGE) tellFrame(stem, 'navigate', r);
  });
</script>

<!-- Its own origin (chains-pack://<stem>/): it cannot reach this page, only send it messages. Sandboxed: no pop-ups,
     no navigation of the app, no forms. Hidden while another page is shown (it keeps running, as a tab would). -->
<iframe
  bind:this={frame}
  class="module-frame"
  class:above
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
  .module-frame[hidden] {
    display: none;
  }
  /* A dialog of its pages is open: above the dim the app lays over its own window. */
  .module-frame.above {
    position: relative;
    z-index: 41;
  }
</style>
