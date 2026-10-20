<script lang="ts">
  // The sidechains in the sidebar's Chain section (doc/unified-app.md §19): one group per sidechain turned on, with its
  // mark, its name and how its node is doing, holding what that chain adds (lib/chaingroups.ts): Layers its Assets,
  // Trade and Staking; Hivemind its Markets, Positions and Votes; every chain its deposits and withdrawals and its own
  // pages. A group folds (remembered: prefs.chainGroupsFolded); opening an item selects its chain for every page. While
  // a chain's own pages are shown, its group lists them, so the chain stays one place of the sidebar.
  import { t } from '../lib/i18n.svelte';
  import Icon from './Icon.svelte';
  import SafeText from './SafeText.svelte';
  import ChainBadge from './ChainBadge.svelte';
  import { chainEntries, loadCapabilities, selectedChain, setSelectedChain } from '../lib/chains.svelte';
  import { chainGroupItems, chainItemCurrent, openChainItem, type ChainGroupItem } from '../lib/chaingroups';
  import { activeModule, modules, moduleRoute, NODE_PAGE, openModule, pageIconOf } from '../lib/modules.svelte';
  import { prefs, savePrefs } from '../lib/state.svelte';
  import { describeState } from '../lib/unified';

  let { collapsed = false, simple = false, onvisit = () => {} }: { collapsed?: boolean; simple?: boolean; onvisit?: () => void } = $props();

  $effect(() => {
    void loadCapabilities();
  });

  const sidechains = $derived(chainEntries().filter((e) => !e.host));
  const selection = $derived(selectedChain());
  const toneWords: Record<string, string> = { ok: 'Running', busy: 'Starting or stopping', warn: 'Has a problem', bad: 'Stopped: needs your attention', off: 'Off' };

  function folded(stem: string): boolean {
    return prefs.chainGroupsFolded.includes(stem);
  }
  function toggle(stem: string) {
    const now = folded(stem) ? prefs.chainGroupsFolded.filter((s) => s !== stem) : [...prefs.chainGroupsFolded, stem];
    savePrefs({ chainGroupsFolded: now });
  }
  function visit(stem: string, item: ChainGroupItem) {
    openChainItem(stem, item);
    onvisit();
  }
  function visitModulePage(stem: string, page: string) {
    setSelectedChain(stem);
    openModule(stem, page);
    onvisit();
  }
  // The rail (folded sidebar): a chain's mark opens its first item.
  function railOpen(stem: string, items: ChainGroupItem[]) {
    if (items[0]) visit(stem, items[0]);
  }
</script>

{#each sidechains as e (e.id)}
  {@const items = chainGroupItems(e, simple)}
  {@const tone = e.entry ? describeState(e.entry).tone : 'off'}
  {@const isFolded = folded(e.id)}
  {@const shownModule = activeModule() === e.id}
  {@const groupId = `chain-group-${e.id}`}
  <div class="group" class:selected={selection === e.id} data-chain-group={e.id}>
    {#if collapsed}
      <button type="button" class="entry head rail" title={`${e.name} · ${t(toneWords[tone])}`} aria-label={e.name} onclick={() => railOpen(e.id, items)}>
        <ChainBadge id={e.id} name={e.name} markOnly />
        <span class="dot {tone}" aria-hidden="true"></span>
      </button>
    {:else}
      <button type="button" class="entry head" aria-expanded={!isFolded} aria-controls={groupId} title={t(toneWords[tone])} data-chain-group-head={e.id} onclick={() => toggle(e.id)}>
        <ChainBadge id={e.id} name={e.name} markOnly />
        <span class="grow name"><SafeText text={e.name} /></span>
        <span class="dot {tone}" aria-label={t(toneWords[tone])}></span>
        <span class="chev" class:open={!isFolded}><Icon name="chevron-right" size={14} /></span>
      </button>
      {#if !isFolded}
        <div class="items" id={groupId} role="group" aria-label={e.name}>
          {#each items as item (item.id)}
            {@const current = chainItemCurrent(e.id, item)}
            <button type="button" class="entry sub" class:current aria-current={current ? 'page' : undefined} title={t(item.summary)} data-chain-item={`${e.id}:${item.id}`} onclick={() => visit(e.id, item)}>
              <Icon name={item.icon} size={16} />
              <span class="grow">{t(item.label)}</span>
            </button>
            {#if item.id === 'own' && shownModule}
              <!-- Its own pages while shown: listed here, under its group. -->
              {@const m = modules.manifests[e.id]}
              {@const at = moduleRoute(e.id).page}
              {#each m?.pages ?? [] as p (p.id)}
                {@const icon = pageIconOf(p)}
                <button type="button" class="entry sub deep" class:current={at === p.id} aria-current={at === p.id ? 'page' : undefined} title={p.summary} data-module-page={p.id} onclick={() => visitModulePage(e.id, p.id)}>
                  <Icon name={icon.name} path={icon.path} size={15} />
                  <span class="grow"><SafeText text={p.title} /></span>
                </button>
              {/each}
              <button type="button" class="entry sub deep" class:current={at === NODE_PAGE} aria-current={at === NODE_PAGE ? 'page' : undefined} data-module-page={NODE_PAGE} onclick={() => visitModulePage(e.id, NODE_PAGE)}>
                <Icon name="nodes" size={15} />
                <span class="grow">{t('Its node')}</span>
              </button>
            {/if}
          {/each}
        </div>
      {/if}
    {/if}
  </div>
{/each}

<style>
  .group {
    display: flex;
    flex-direction: column;
    gap: 0.05rem;
  }
  .entry {
    position: relative;
    display: flex;
    align-items: center;
    gap: 0.6rem;
    width: 100%;
    padding: 0.45rem 0.75rem;
    border: 0;
    border-radius: var(--radius-m);
    background: none;
    color: var(--text-muted);
    font: inherit;
    font-weight: 550;
    text-align: left;
    cursor: pointer;
  }
  .entry:hover {
    background: var(--raised);
    color: var(--text);
  }
  .head {
    color: var(--text);
    font-weight: 650;
  }
  .selected > .head {
    background: color-mix(in srgb, var(--accent) 12%, transparent);
  }
  .rail {
    justify-content: center;
    padding: 0.5rem 0;
  }
  .rail .dot {
    position: absolute;
    right: 0.55rem;
    bottom: 0.4rem;
  }
  .name {
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .grow {
    flex: 1;
  }
  .chev {
    display: inline-flex;
    color: var(--text-faint);
    transition: transform 120ms ease-out;
  }
  .chev.open {
    transform: rotate(90deg);
  }
  .items {
    display: flex;
    flex-direction: column;
    gap: 0.05rem;
    margin: 0 0 0.3rem 1.05rem;
    padding-left: 0.55rem;
    border-left: 1px solid var(--line);
  }
  .sub {
    padding: 0.36rem 0.6rem;
    font-size: 0.92rem;
  }
  .deep {
    padding-left: 1.4rem;
    font-size: 0.86rem;
  }
  .entry.current {
    background: var(--accent);
    color: var(--on-accent);
    box-shadow: 0 6px 20px -8px var(--accent);
  }
  .dot {
    width: 0.45rem;
    height: 0.45rem;
    flex: none;
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
</style>
