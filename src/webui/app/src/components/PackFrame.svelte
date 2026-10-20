<script lang="ts">
  // The pages of a sidechain the app runs, in a frame (doc/unified-app.md §13): its own pages, built into the app,
  // under an origin of their own, talking to their node through messages to the app (lib/packs.svelte.ts). What they
  // spend is asked in the app's own dialog.
  import { packUrl, registerFrame, tellPhase } from '../lib/packs.svelte';

  let { stem, name, running }: { stem: string; name: string; running: boolean } = $props();
  let frame: HTMLIFrameElement | undefined = $state();
  let loaded = $state(false);

  // Known as this sidechain's frame from the start: its pages send their first messages before it has loaded.
  $effect(() => {
    const win = frame?.contentWindow ?? null;
    const shown = stem;
    registerFrame(shown, win);
    return () => registerFrame(shown, null);
  });

  function onload() {
    loaded = true;
  }

  // Its node got ready (or went): the pages leave their splash (or go back to it).
  $effect(() => {
    if (loaded) tellPhase(stem, running ? 'ready' : 'init');
  });
</script>

<!-- Its own origin (chains-pack://<stem>/): it cannot reach this page, only send it messages (lib/packs.svelte.ts).
     Sandboxed: no pop-ups, no navigation of the app, no forms. -->
<iframe bind:this={frame} class="pack" src={packUrl(stem)} title={name} data-stem={stem} sandbox="allow-scripts allow-same-origin" referrerpolicy="no-referrer" {onload}></iframe>

<style>
  .pack {
    display: block;
    flex: 1 1 auto;
    width: 100%;
    min-height: 0;
    border: 0;
    background: var(--bg);
  }
</style>
