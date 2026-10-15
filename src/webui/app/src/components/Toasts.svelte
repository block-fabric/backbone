<script lang="ts">
  // Short notices in a corner: copied, sent, a warning of the node.
  import Icon from './Icon.svelte';
  import { dismissToast, toasts } from '../lib/state.svelte';

  /** The newest two show; the others wait behind them (a column of notices covered the amounts of a table). */
  const MAX_SHOWN = 2;
  const shown = $derived(toasts.list.slice(-MAX_SHOWN));
  const hidden = $derived(toasts.list.length - shown.length);
</script>

<div class="toasts" role="status" aria-live="polite">
  {#if hidden > 0}
    <button type="button" class="more" onclick={() => dismissToast(toasts.list[0].id)} title="Dismiss the oldest">{hidden} more</button>
  {/if}
  {#each shown as t (t.id)}
    <div class="toast {t.kind}">
      <Icon name={t.kind === 'success' ? 'check' : t.kind === 'info' ? 'info' : 'alert'} size={18} />
      <div class="grow">
        <strong>{t.title}</strong>
        {#if t.message}<p class="break">{t.message}</p>{/if}
        {#if t.actions?.length}
          <div class="acts">
            {#each t.actions as a (a.label)}
              <button type="button" class="btn small" onclick={() => { dismissToast(t.id); a.run(); }}>{a.label}</button>
            {/each}
          </div>
        {/if}
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
    align-items: flex-end;
    width: min(21rem, calc(100vw - 2rem));
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
  .toast {
    width: 100%;
  }
  .toast p {
    margin: 0.15rem 0 0;
    color: var(--text-muted);
    font-size: 0.9rem;
    display: -webkit-box;
    -webkit-line-clamp: 3;
    line-clamp: 3;
    -webkit-box-orient: vertical;
    overflow: hidden;
  }
  .acts {
    display: flex;
    flex-wrap: wrap;
    gap: 0.4rem;
    margin-top: 0.45rem;
  }
  .more {
    padding: 0.15rem 0.6rem;
    border: 1px solid var(--line-strong);
    border-radius: 999px;
    background: var(--surface);
    color: var(--text-muted);
    font: inherit;
    font-size: 0.8rem;
    pointer-events: auto;
    cursor: pointer;
  }
</style>
