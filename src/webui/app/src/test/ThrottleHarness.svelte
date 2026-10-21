<script lang="ts">
  // A value followed through lib/throttle.svelte.ts (perf.test.ts).
  import { throttled } from '../lib/throttle.svelte';
  let { ms }: { ms: number } = $props();
  let n = $state(0);
  // svelte-ignore state_referenced_locally
  const shown = throttled(() => n, ms);
  export function set(value: number) {
    n = value;
  }
</script>

<span data-throttled>{shown.value}</span>
