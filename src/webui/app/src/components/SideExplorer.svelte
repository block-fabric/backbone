<script lang="ts">
  // The block explorer with one sidechain selected (doc/unified-app.md §20.5): that chain's own explorer, as its own app
  // has it (its blocks with their peg and the block of the mainchain that merged-mined each, its transactions with its
  // own operations, the addresses of its wallet), shown in the app's page in the chain's frame (§14, unchanged in what it
  // can reach). A search given to the page ("search:<q>": a link from another chain, a row of Home or Overview) is
  // handed to it.
  //
  // The search is taken from the page's place (as MainExplorer takes its own): it is this chain's, and must not be
  // searched again on another chain when the selector changes.
  import { untrack } from 'svelte';
  import ModuleEmbed from './ModuleEmbed.svelte';
  import { nav } from '../lib/nav.svelte';
  import type { ChainEntry } from '../lib/chains.svelte';

  let { entry }: { entry: ChainEntry } = $props();
  let tab = $state(untrack(() => (nav.tab.startsWith('search:') ? nav.tab : '')));
  $effect(() => {
    const asked = nav.tab;
    if (!asked.startsWith('search:')) return;
    tab = asked;
    nav.tab = '';
  });
</script>

<div class="page side-explorer" data-side-explorer={entry.id}>
  <ModuleEmbed stem={entry.id} page="explorer" {tab} name={entry.name} height="calc(100vh - 9rem)" bare />
</div>

<style>
  .side-explorer {
    display: flex;
    flex-direction: column;
  }
</style>
