<script lang="ts">
  // The page of chains-app: what it shows follows the phase of the app.
  import { onMount } from 'svelte';
  import AppDialogs from './components/AppDialogs.svelte';
  import DialogHost from './components/DialogHost.svelte';
  import NodeDialogs from './components/NodeDialogs.svelte';
  import ResizeHandles from './components/ResizeHandles.svelte';
  import TitleStrip from './components/TitleStrip.svelte';
  import Toasts from './components/Toasts.svelte';
  import { hasHost, installEventSink, on } from './lib/bridge';
  import { handleShortcut } from './lib/keyboard';
  import { go, nav } from './lib/nav.svelte';
  import { host } from './lib/rpc';
  import { app, applySnapshot, loadPrefs, prefs, subscribe, toast } from './lib/state.svelte';
  import { applyTheme } from './lib/themes';
  import { parsePaymentUri } from './lib/uri';
  import { watchWindow } from './lib/windowframe.svelte';
  import Fatal from './views/Fatal.svelte';
  import Intro from './views/Intro.svelte';
  import Shell from './views/Shell.svelte';
  import Splash from './views/Splash.svelte';

  let prefersDark = $state(false);
  let connectError = $state('');

  $effect(() => {
    applyTheme(document, prefs.theme, prefs.fontAdjust, prefersDark);
  });

  async function refreshNode() {
    try {
      applySnapshot(await host.nodeState());
    } catch {
      // Not running yet; the events will tell.
    }
  }

  onMount(() => {
    const media = window.matchMedia('(prefers-color-scheme: dark)');
    prefersDark = media.matches;
    const onScheme = (e: MediaQueryListEvent) => (prefersDark = e.matches);
    media.addEventListener('change', onScheme);

    installEventSink();
    let offWindow: (() => void) | undefined;
    subscribe();
    const offPhase = on<{ phase: string }>('phase', ({ phase }) => {
      if (phase === 'ready') refreshNode();
      if (phase === 'init' || phase === 'ready') loadPrefs();
    });
    const offUri = on<{ uri: string }>('uri', ({ uri }) => {
      const parsed = parsePaymentUri(uri);
      if (!parsed.ok) {
        toast('error', 'Payment request error', parsed.error);
        return;
      }
      nav.pendingUri = uri;
      nav.uriSeq++;
      go('send');
      toast('info', 'Payment request', `Pay to ${parsed.request.address}`);
    });

    (async () => {
      if (import.meta.env.DEV && !hasHost()) {
        const mock = await import('./lib/mock');
        mock.installMock();
      }
      try {
        const hello = await host.hello();
        app.info = hello.info;
        app.dialogs = hello.dialogs;
        app.phaseData = hello.phaseData;
        app.phase = hello.phase;
        if (hello.info.test) {
          // The tests of the app drive the page through this (only with -webuitestscript).
          const state = await import('./lib/state.svelte');
          const navigation = await import('./lib/nav.svelte');
          const dialogs = await import('./lib/dialogs.svelte');
          const walletdata = await import('./lib/walletdata.svelte');
          (window as unknown as Record<string, unknown>).__chainsTest = { ...state, ...navigation, dialogs: dialogs.dialogs, answerDialog: dialogs.answer, walletData: walletdata.current };
        }
        offWindow = watchWindow();
        await loadPrefs();
        if (hello.phase === 'ready') await refreshNode();
      } catch (e) {
        connectError = e instanceof Error ? e.message : String(e);
      }
    })();

    return () => {
      media.removeEventListener('change', onScheme);
      offPhase();
      offUri();
      offWindow?.();
    };
  });

  function onkeydown(e: KeyboardEvent) {
    if (app.phase !== 'ready' || e.defaultPrevented) return;
    if (handleShortcut(e)) e.preventDefault();
  }
</script>

<svelte:window {onkeydown} />

{#if connectError || app.phase !== 'ready'}
  <!-- Before the node runs: the integrated title bar alone (the layouts have theirs). -->
  <div class="frame">
    <TitleStrip />
    <div class="phase">
      {#if connectError}
        <main class="lost">
          <h1>No connection to the node</h1>
          <p class="muted">{connectError}</p>
        </main>
      {:else if app.phase === 'intro'}
        <Intro />
      {:else if app.phase === 'fatal'}
        <Fatal />
      {:else if app.phase === 'shutdown'}
        <Splash stopping />
      {:else}
        <Splash />
      {/if}
    </div>
  </div>
{:else}
  <Shell />
{/if}
<ResizeHandles />

<NodeDialogs />
<AppDialogs />
<DialogHost />
<Toasts />

<style>
  .frame {
    display: flex;
    flex-direction: column;
    height: 100%;
  }
  .phase {
    flex: 1 1 0;
    min-height: 0;
  }
  .lost {
    display: grid;
    place-content: center;
    gap: 0.5rem;
    height: 100%;
    text-align: center;
  }
</style>
