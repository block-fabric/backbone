<script lang="ts">
  // The border of a window the page frames: strips along the sides and corners that resize it.
  import { frame, resizeFrom } from '../lib/frame.svelte';

  const EDGES = ['n', 'ne', 'e', 'se', 's', 'sw', 'w', 'nw'];
</script>

{#if frame.framed && !frame.maximized}
  {#each EDGES as edge (edge)}
    <!-- svelte-ignore a11y_no_static_element_interactions -->
    <div class="edge {edge}" aria-hidden="true" onmousedown={(e) => resizeFrom(edge, e)}></div>
  {/each}
{/if}

<style>
  .edge {
    position: fixed;
    z-index: 100;
  }
  .n,
  .s {
    left: 8px;
    right: 8px;
    height: 4px;
    cursor: ns-resize;
  }
  .n {
    top: 0;
  }
  .s {
    bottom: 0;
  }
  .e,
  .w {
    top: 8px;
    bottom: 8px;
    width: 4px;
    cursor: ew-resize;
  }
  .e {
    right: 0;
  }
  .w {
    left: 0;
  }
  .ne,
  .nw,
  .se,
  .sw {
    width: 8px;
    height: 8px;
  }
  .ne {
    top: 0;
    right: 0;
    cursor: nesw-resize;
  }
  .sw {
    bottom: 0;
    left: 0;
    cursor: nesw-resize;
  }
  .nw {
    top: 0;
    left: 0;
    cursor: nwse-resize;
  }
  .se {
    bottom: 0;
    right: 0;
    cursor: nwse-resize;
  }
</style>
