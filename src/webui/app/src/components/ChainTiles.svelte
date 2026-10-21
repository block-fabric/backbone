<script lang="ts">
  // The chain of a form (Send, Receive; doc/unified-app.md §19), its first field, above "Pay to": a tile per chain with
  // its mark, its name and what its wallet holds there (in its own coin; hidden in privacy mode). The one picked is the
  // chain every page shows (the top bar's selector), and the other way round; with All in the top bar none is picked
  // and the form waits for one. Arrow keys move between the tiles (a radio group).
  import { t } from '../lib/i18n.svelte';
  import SafeText from './SafeText.svelte';
  import ChainBadge from './ChainBadge.svelte';
  import Amount from './Amount.svelte';
  import ModuleAmount from './ModuleAmount.svelte';
  import { chainColor } from '../lib/chaincolors';
  import { balanceOf, chainEntries, chainState, refreshSummary, setSelectedChain, type ChainEntry } from '../lib/chains.svelte';
  import { wallets } from '../lib/state.svelte';

  let { value, label, kind }: { value: string | null; label: string; kind: 'send' | 'receive' } = $props();

  const entries = $derived(chainEntries());
  const own = $derived(wallets.current !== null ? wallets.balances[wallets.current] : undefined);

  $effect(() => {
    for (const e of entries) if (!e.host && e.running) void refreshSummary(e.id, 4000);
  });

  function pick(e: ChainEntry) {
    if (!e.running && !e.host) return;
    setSelectedChain(e.id);
  }
  function keys(ev: KeyboardEvent, i: number) {
    const step: Record<string, number> = { ArrowRight: 1, ArrowDown: 1, ArrowLeft: -1, ArrowUp: -1 };
    if (!(ev.key in step)) return;
    ev.preventDefault();
    const usable = entries.filter((e) => e.host || e.running);
    const at = usable.findIndex((e) => e.id === entries[i].id);
    const next = usable[(at + step[ev.key] + usable.length) % usable.length];
    if (!next) return;
    pick(next);
    (ev.currentTarget as HTMLElement).parentElement?.querySelector<HTMLElement>(`[data-chain-tile="${next.id}"]`)?.focus();
  }
  const focusable = $derived(value ?? entries.find((e) => e.host || e.running)?.id);
</script>

{#if entries.length > 1}
  <div class="chain-step" data-chain-tiles={kind}>
    <div class="label-row">
      <span class="label" id="chain-tiles-{kind}">{t(label)}</span>
      {#if !value}<span class="need" data-chain-needed>{kind === 'send' ? t('Pick the chain your coins are on.') : t('Pick the chain to be paid on.')}</span>{/if}
    </div>
    <div class="tiles" role="radiogroup" aria-labelledby="chain-tiles-{kind}" aria-required="true">
      {#each entries as e, i (e.id)}
        {@const s = chainState.summaries[e.id]}
        {@const b = balanceOf(s)}
        {@const on = value === e.id}
        {@const empty = kind === 'send' && (e.host ? own !== undefined && own.trusted === 0n : !!b.available && !/[1-9]/.test(b.available))}
        <button
          type="button"
          role="radio"
          class="tile"
          class:on
          class:empty
          aria-checked={on}
          tabindex={e.id === focusable ? 0 : -1}
          disabled={!e.host && !e.running}
          style:--chain={chainColor(e.id)}
          data-chain-tile={e.id}
          onclick={() => pick(e)}
          onkeydown={(ev) => keys(ev, i)}
        >
          <span class="top"><ChainBadge id={e.id} name={e.name} /></span>
          <span class="bal">
            {#if !e.host && !e.running}
              <span class="muted">{t('Not running')}</span>
            {:else if e.host}
              <Amount sats={own?.trusted} />
            {:else}
              <ModuleAmount text={b.available} unit={e.unit} coins />
            {/if}
          </span>
          {#if !e.host && s?.assets?.length}<span class="extra muted"><SafeText text={s.assets.length === 1 ? t('1 asset') : t('%1 assets', String(s.assets.length))} /></span>
          {:else if empty}<span class="extra muted">{t('Nothing to send')}</span>{/if}
        </button>
      {/each}
    </div>
  </div>
{/if}

<style>
  .chain-step {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
  }
  .label-row {
    display: flex;
    align-items: baseline;
    gap: 0.8rem;
    flex-wrap: wrap;
  }
  .label {
    font-weight: 650;
  }
  .need {
    color: var(--text-muted);
    font-size: 0.88rem;
  }
  .tiles {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(10.5rem, 1fr));
    gap: 0.55rem;
  }
  .tile {
    position: relative;
    display: flex;
    flex-direction: column;
    align-items: flex-start;
    gap: 0.35rem;
    padding: 0.65rem 0.8rem 0.6rem;
    border: 1px solid var(--line);
    border-radius: var(--radius-l);
    background: var(--surface-2);
    color: var(--text);
    font: inherit;
    text-align: left;
    cursor: pointer;
    overflow: hidden;
  }
  .tile::before {
    content: '';
    position: absolute;
    inset: 0 auto 0 0;
    width: 3px;
    background: var(--chain);
    opacity: 0;
  }
  .tile:hover:not(:disabled) {
    border-color: var(--line-strong);
    background: var(--raised);
  }
  .tile.on {
    border-color: var(--accent);
    background: color-mix(in srgb, var(--accent) 10%, var(--surface-2));
    box-shadow: 0 0 0 1px var(--accent) inset;
  }
  .tile.on::before {
    opacity: 1;
  }
  .tile:disabled {
    opacity: 0.55;
    cursor: default;
  }
  /* Nothing to send there: still a choice (assets may be), quieter. */
  .tile.empty:not(.on) .bal {
    color: var(--text-muted);
    font-weight: 500;
  }
  .bal {
    font-size: 0.95rem;
    font-weight: 600;
  }
  .extra {
    font-size: 0.78rem;
  }
</style>
