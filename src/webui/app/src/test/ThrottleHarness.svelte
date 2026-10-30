<script lang="ts">
  // A value followed through lib/throttle.svelte.ts (perf.test.ts), with what it is of (its scope) when asked.
  import { throttled } from '../lib/throttle.svelte';
  let { ms, scoped = false }: { ms: number; scoped?: boolean } = $props();
  let n = $state(0);
  let of = $state('all');
  // svelte-ignore state_referenced_locally
  const shown = scoped ? throttled(() => n, ms, () => of) : throttled(() => n, ms);
  export function set(value: number, scope?: string) {
    n = value;
    if (scope !== undefined) of = scope;
  }
</script>

<span data-throttled>{shown.value}</span>
