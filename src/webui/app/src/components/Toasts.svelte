<script lang="ts">
  // Short notices in a corner: copied, sent, a warning of the node.
  import Icon from './Icon.svelte';
  import { dismissToast, toasts } from '../lib/state.svelte';
</script>

<div class="toasts" role="status" aria-live="polite">
  {#each toasts.list as t (t.id)}
    <div class="toast {t.kind}">
      <Icon name={t.kind === 'success' ? 'check' : t.kind === 'info' ? 'info' : 'alert'} size={18} />
      <div class="grow">
        <strong>{t.title}</strong>
        {#if t.message}<p class="break">{t.message}</p>{/if}
      </div>
      <button type="button" class="btn ghost icon small" aria-label="Dismiss" onclick={() => dismissToast(t.id)}>✕</button>
    </div>
  {/each}
</div>

<style>
  .toasts {
    position: fixed;
    right: 1rem;
    bottom: 3rem;
    z-index: 50;
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
    width: min(24rem, calc(100vw - 2rem));
    pointer-events: none;
  }
  .toast {
    display: flex;
    align-items: flex-start;
    gap: 0.65rem;
    padding: 0.7rem 0.6rem 0.7rem 0.85rem;
    border: 1px solid var(--line-strong);
    border-left: 4px solid var(--text-muted);
    border-radius: var(--radius-m);
    background: var(--surface);
    box-shadow: var(--shadow);
    pointer-events: auto;
  }
  .toast.success {
    border-left-color: var(--positive);
  }
  .toast.warning {
    border-left-color: var(--warning);
  }
  .toast.error {
    border-left-color: var(--negative);
  }
  .toast p {
    margin: 0.15rem 0 0;
    color: var(--text-muted);
    font-size: 0.9rem;
  }
</style>
