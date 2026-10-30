<script lang="ts">
  // Your sidechains, on the Sidechains page (doc/unified-app.md §20.4): a row per sidechain this app runs (within the top
  // bar's selection): how its node keeps up with this app's chain, what you hold there, its merged mining (or its blocks
  // by stake), and a link to its own pages; then what is on its way between the chains: the deposits made here not yet
  // credited there, and your withdrawals not paid yet, with where each one is.
  import { t } from '../lib/i18n.svelte';
  import ChainBadge from './ChainBadge.svelte';
  import ModuleAmount from './ModuleAmount.svelte';
  import Icon from './Icon.svelte';
  import SafeText from './SafeText.svelte';
  import { formatCount } from '../lib/format';
  import { openModule, productName } from '../lib/modules.svelte';
  import { describeState } from '../lib/unified';
  import { go } from '../lib/nav.svelte';
  import { simpleMode } from '../lib/uimode.svelte';
  import { ALL, chainEntries, chainHost, chainState, chainWallet, HOST, pendingDeposits, refreshSummary, selectedChain, type ChainPeg } from '../lib/chains.svelte';

  const entries = $derived(chainEntries().filter((e) => !e.host));
  const sel = $derived(selectedChain());
  const shown = $derived(sel === ALL || sel === HOST ? entries : entries.filter((e) => e.id === sel));
  let pegs = $state<Record<string, ChainPeg>>({});
  /** How a chain makes its blocks is Advanced's (Simple: where the coins are, and what is on its way). */
  const simple = $derived(simpleMode());

  $effect(() => {
    const stems = shown.filter((e) => e.running).map((e) => e.id);
    const load = () => {
      for (const s of stems) {
        void refreshSummary(s, 6000);
        chainHost.peg(s, chainWallet(s)).then(
          (p) => (pegs[s] = p),
          () => undefined,
        );
      }
    };
    load();
    const timer = setInterval(() => {
      if (!document.hidden) load();
    }, 15000);
    return () => clearInterval(timer);
  });

  const STATUS: Record<string, string> = { waiting: 'waiting for a bundle', bundled: 'in a bundle', pending: 'voted on by the miners', paid: 'paid' };
  const onTheirWay = $derived([
    ...shown.flatMap((e) => pendingDeposits(e.id).map((d) => ({ key: `d:${d.txid}`, kind: 'deposit' as const, chain: e, amount: d.amount, status: t('waiting for its blocks') }))),
    ...shown.flatMap((e) =>
      (pegs[e.id]?.withdrawals ?? [])
        .filter((w) => w.mine)
        .map((w) => ({ key: `w:${w.txid}:${w.vout}`, kind: 'withdrawal' as const, chain: e, amount: w.amount, status: t(STATUS[w.status] ?? w.status) })),
    ),
  ]);
</script>

<section class="panel sidechain-status" aria-labelledby="status-title" data-sidechain-status>
  <div class="panel-head">
    <h2 id="status-title">{#if shown.length === 1}<SafeText text={shown[0].name} />{:else}{t('Your sidechains')}{/if}</h2>
    <button type="button" class="btn ghost small" onclick={() => go('settings', 'sidechains')}>{t('Turn on or off')}</button>
  </div>
  <div class="panel-body">
    <table class="rows">
      <thead><tr><th>{t('Chain')}</th><th>{t('Node')}</th><th class="end">{t('You hold')}</th>{#if !simple}<th>{t('Blocks')}</th>{/if}</tr></thead>
      <tbody>
        {#each shown as e (e.id)}
          {@const st = e.entry ? describeState(e.entry) : null}
          {@const s = chainState.summaries[e.id]}
          {@const p = pegs[e.id]}
          {@const stakes = !!e.caps?.staking}
          <tr class="clickable" onclick={() => openModule(e.id, 'home')} title={t('Open the pages of %1', e.name)} data-status-row={e.id}>
            <td><ChainBadge id={e.id} name={e.name} /></td>
            <td>
              <span class="state {st?.tone ?? 'off'}">{#if st?.tone === 'ok' && s?.mainchain?.behind_blocks === 0}{t('In step')}{:else if st}{t(st.key, ...st.args)}{:else}{t('Off')}{/if}</span>
              {#if s?.height !== undefined}<span class="faint small">{t('block %1', formatCount(s.height))}</span>{/if}
            </td>
            <td class="end"><ModuleAmount text={s?.balances?.trusted ?? null} unit={e.unit} coins /></td>
            {#if !simple}<td class="small">
              {#if !p}<span class="faint">–</span>
              {:else if stakes}{t('By stake')}{#if p.bmm.mining}<span class="dot-sep">{' · '}</span><span class="muted">{t('anchored by merged mining')}</span>{/if}
              {:else if p.bmm.mining}<span class="ok">{t('Merged mining')}</span><span class="dot-sep">{' · '}</span><span class="muted">{tBlocks(p.bmm.blocks ?? 0)}</span>
              {:else}<span class="muted">{t('Merged mining off')}</span>{/if}
            </td>{/if}
          </tr>
        {:else}
          <tr><td colspan={simple ? 3 : 4} class="muted">{t('No sidechain runs in this app yet.')}</td></tr>
        {/each}
      </tbody>
    </table>

    <h3 class="way-title">{t('On their way')}</h3>
    {#if onTheirWay.length}
      <ul class="way">
        {#each onTheirWay as w (w.key)}
          <li data-on-their-way={w.kind}>
            <span class="icon" aria-hidden="true"><Icon name={w.kind === 'deposit' ? 'down' : 'up'} size={14} /></span>
            <span class="grow">{w.kind === 'deposit' ? t('Into %1', w.chain.name) : t('From %1 to %2', w.chain.name, productName())}<span class="dot-sep">{' · '}</span><span class="muted"><SafeText text={w.status} /></span></span>
            <ModuleAmount text={w.amount} coins />
          </li>
        {/each}
      </ul>
    {:else}
      <p class="muted small none">{t('Nothing is moving between the chains.')}</p>
    {/if}
  </div>
</section>

<script module lang="ts">
  import { tn } from '../lib/i18n.svelte';
  function tBlocks(n: number): string {
    return tn('%n block(s)', n);
  }
</script>

<style>
  .state {
    display: block;
    font-weight: 600;
  }
  .state.ok {
    color: var(--positive);
  }
  .state.busy,
  .state.warn {
    color: var(--warning);
  }
  .state.bad {
    color: var(--negative);
  }
  .small {
    font-size: 0.82rem;
  }
  .ok {
    color: var(--positive);
  }
  .way-title {
    margin: 1.1rem 0 0.35rem;
    color: var(--text-muted);
    font-size: 0.8rem;
    font-weight: 650;
    text-transform: uppercase;
    letter-spacing: 0.06em;
  }
  .way {
    display: flex;
    flex-direction: column;
    margin: 0;
    padding: 0;
    list-style: none;
  }
  .way li {
    display: flex;
    align-items: center;
    gap: 0.6rem;
    padding: 0.45rem 0.2rem;
    border-top: 1px solid var(--line);
  }
  .way li:first-child {
    border-top: 0;
  }
  .icon {
    display: inline-grid;
    place-items: center;
    width: 1.6rem;
    height: 1.6rem;
    border-radius: 50%;
    background: color-mix(in srgb, var(--warning) 14%, transparent);
    color: var(--warning);
  }
  .grow {
    flex: 1;
    min-width: 0;
  }
  .none {
    margin: 0;
  }
</style>
