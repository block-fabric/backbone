<script lang="ts">
  // The block explorer follows the top bar's selector (doc/unified-app.md §20.5): this app's own chain shows its own
  // explorer (MainExplorer: blocks, transactions, addresses of the wallet, the drivechain's messages of each block); a
  // sidechain shows its own explorer, in the page (its frame: its blocks, their peg, its own operations); every chain at
  // once shows one search over all of them and each chain's newest blocks and waiting transactions (AllExplorer). A
  // sidechain turned on shows at once; one turned off goes (the selector forgets it).
  import MainExplorer from '../components/MainExplorer.svelte';
  import AllExplorer from '../components/AllExplorer.svelte';
  import SideExplorer from '../components/SideExplorer.svelte';
  import { ALL, chainEntries, HOST, selectedChain } from '../lib/chains.svelte';

  const entries = $derived(chainEntries());
  const sel = $derived(selectedChain(entries));
  const side = $derived(sel !== ALL && sel !== HOST ? (entries.find((e) => e.id === sel) ?? null) : null);
  const all = $derived(sel === ALL && entries.length > 1);
</script>

{#if side}
  {#key side.id}<SideExplorer entry={side} />{/key}
{:else if all}
  <AllExplorer />
{:else}
  <MainExplorer />
{/if}
