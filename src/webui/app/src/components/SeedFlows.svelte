<script lang="ts">
  // The guides of the backup phrase (doc/unified-app.md §23), over every page: the first start (no wallet yet) and the
  // offer to those who had wallets before (once, decision 6) open by themselves; the guided move of coins opens from Home,
  // the Sidechains page, the palette and the end of the first start.
  import FirstRunWizard from './FirstRunWizard.svelte';
  import MigrateWizard from './MigrateWizard.svelte';
  import MoveWizard from './MoveWizard.svelte';
  import { app } from '../lib/state.svelte';
  import { autoGuide, refreshSeed, seed, watchSeed } from '../lib/seed.svelte';
  import { refreshSidechains, watchSidechains } from '../lib/sidechains.svelte';

  let defaulted = false;
  $effect(() => {
    // The tests of the app ask for the guides themselves (seed.autoOpen), not to cover the pages they drive: once.
    if (app.info?.test && !defaulted) seed.autoOpen = false;
    if (app.info) defaulted = true;
  });
  $effect(() => {
    watchSeed();
    watchSidechains();
    void refreshSeed();
    void refreshSidechains();
  });
  $effect(() => {
    const guide = autoGuide(seed.status, seed);
    if (guide) {
      seed.autoOpened = true;
      seed.open = guide;
    }
  });
</script>

{#if seed.open === 'firstrun'}
  <FirstRunWizard onclose={() => (seed.open = null)} onmove={() => setTimeout(() => (seed.open = 'move'), 0)} />
{:else if seed.open === 'migrate'}
  <MigrateWizard onclose={() => (seed.open = null)} />
{:else if seed.open === 'move'}
  <MoveWizard onclose={() => (seed.open = null)} />
{/if}
