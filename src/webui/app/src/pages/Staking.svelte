<script lang="ts">
  // Staking (user, 2026-10-21: "the bond / unbond / turn on section visible by default; we would show the full page;
  // beside the Staking title just a tab selector, of the chains the top selector shows that have PoS only"): the whole
  // staking page of one chain that stakes, its own (its frame, §14, laid in this page without its title: headless), so
  // that its overview, this node as a validator (staking on or off, its keys, its payout), its bonds (bond, add,
  // unbond) and the network (the validator set, the schedule, finality, slashing) are here at once; their questions
  // are asked in the app's trusted dialog, with the chain's mark, as always. Beside the title, a tab for each chain that
  // stakes, is turned on and is shown by the top bar's selector (All: every one; one chain: that one); the last tab is
  // remembered (prefs.stakingChain). Only the chain of the tab shown is asked anything: its frame, nothing else.
  import { t } from '../lib/i18n.svelte';
  import ChainBadge from '../components/ChainBadge.svelte';
  import ModuleEmbed from '../components/ModuleEmbed.svelte';
  import { ALL, chainEntries, chainsWith, loadCapabilities, selectedChain, setSelectedChain, type ChainEntry } from '../lib/chains.svelte';
  import { prefs, savePrefs } from '../lib/state.svelte';
  import { describeState } from '../lib/unified';

  $effect(() => {
    void loadCapabilities();
  });

  const entries = $derived(chainEntries());
  /** Every chain that stakes and is turned on. */
  const stakers = $derived(chainsWith('staking', entries));
  const sel = $derived(selectedChain(entries));
  /** The chain the selector shows, when it is one. */
  const selEntry = $derived(sel === ALL ? null : (entries.find((e) => e.id === sel) ?? null));
  /** The tabs: the chains that stake among those the selector shows. */
  const tabs = $derived(sel === ALL ? stakers : stakers.filter((e) => e.id === sel));
  const shown = $derived<ChainEntry | null>(tabs.find((e) => e.id === prefs.stakingChain) ?? tabs[0] ?? null);
  const toneWords: Record<string, string> = { ok: 'Running', busy: 'Starting or stopping', warn: 'Has a problem', bad: 'Stopped: needs your attention', off: 'Off' };

  function pick(e: ChainEntry) {
    // A chain the selector does not show (the selector is on a chain that does not stake): it becomes the selection.
    if (sel !== ALL && sel !== e.id) setSelectedChain(e.id);
    if (prefs.stakingChain !== e.id) savePrefs({ stakingChain: e.id });
  }
  /** The tabs offered: those shown, or, with a chain selected that does not stake, the chains that do (a click away). */
  const offered = $derived(tabs.length ? tabs : stakers);
</script>

<div class="page staking" data-staking-shown={shown?.id ?? ''}>
  <header class="page-head">
    <h1>{t('Staking')}</h1>
    {#if offered.length}
      <div class="chain-tabs" role="tablist" aria-label={t('Chains that stake')}>
        {#each offered as e (e.id)}
          {@const tone = e.entry ? describeState(e.entry).tone : 'off'}
          <button type="button" role="tab" class="chain-tab" aria-selected={shown?.id === e.id} title={`${e.name}: ${t(toneWords[tone])}`} onclick={() => pick(e)} data-staking-tab={e.id}>
            <ChainBadge id={e.id} name={e.name} small />
            <span class="dot {tone}" aria-hidden="true"></span>
          </button>
        {/each}
      </div>
    {/if}
  </header>

  {#if stakers.length === 0}
    <section class="panel note">
      <p class="muted">{t('No chain that stakes is turned on. Turn on FastBlocks or Layers in Settings → Sidechains.')}</p>
    </section>
  {:else if !shown}
    <!-- The chain the selector shows does not stake: said, and those that do are the tabs above. -->
    <section class="panel note" data-not-on-chain={selEntry?.id ?? ALL}>
      <p><strong>{t('%1 has no staking.', selEntry?.name ?? '')}</strong> <span class="muted">{t('Pick a chain that stakes above, or show all chains.')}</span></p>
      <button type="button" class="btn ghost small" onclick={() => setSelectedChain(ALL)}>{t('All chains')}</button>
    </section>
  {:else}
    {#key shown.id}
      <div class="embed" data-staking-chain={shown.id}>
        <ModuleEmbed stem={shown.id} page="staking" name={shown.name} height="calc(100vh - 12.6rem)" bare headless />
      </div>
    {/key}
  {/if}
</div>

<style>
  /* The chain's page fills the rest of the window, and scrolls inside its frame: one scroll bar, the frame's. */
  .staking {
    display: flex;
    flex-direction: column;
    gap: 0.6rem;
    padding-bottom: 0.6rem;
  }
  .page-head {
    display: flex;
    align-items: center;
    justify-content: flex-start;
    gap: 1.1rem;
    flex-wrap: wrap;
  }
  .chain-tabs {
    display: flex;
    flex-wrap: wrap;
    gap: 0.3rem;
    padding: 0.2rem;
    border: 1px solid var(--line);
    border-radius: 999px;
    background: var(--surface-2, var(--raised));
  }
  .chain-tab {
    display: inline-flex;
    align-items: center;
    gap: 0.45rem;
    padding: 0.3rem 0.75rem 0.3rem 0.45rem;
    border: 0;
    border-radius: 999px;
    background: none;
    color: var(--text-muted);
    font: inherit;
    font-weight: 600;
    cursor: pointer;
  }
  .chain-tab:hover {
    color: var(--text);
    background: var(--raised);
  }
  .chain-tab[aria-selected='true'] {
    background: var(--surface, var(--bg));
    color: var(--text);
    box-shadow: 0 0 0 1px var(--line-strong, var(--line));
  }
  .dot {
    width: 0.45rem;
    height: 0.45rem;
    border-radius: 50%;
    background: var(--text-faint);
  }
  .dot.ok {
    background: var(--positive);
  }
  .dot.busy,
  .dot.warn {
    background: var(--warning);
  }
  .dot.bad {
    background: var(--negative);
  }
  .note {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: 0.8rem;
    padding: 1rem 1.2rem;
  }
  .note p {
    margin: 0;
  }
  .embed {
    display: flex;
    flex-direction: column;
    margin: 0 -0.5rem;
  }
</style>
