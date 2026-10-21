<script lang="ts">
  // Liquidity of one pool, in or out (doc/unified-app.md §22). In: an amount of one asset, the other worked out at the
  // pool's price (exactly, rounded up: the most offered; the chain takes what its price takes); a pool nobody provides
  // to any more is opened again by both amounts, which set its price. Out: a share of what the wallet holds, what it
  // gives back about. Each is the chain's own question, in the trusted dialog, with what the app checked first.
  import { t } from '../../lib/i18n.svelte';
  import Icon from '../Icon.svelte';
  import SafeText from '../SafeText.svelte';
  import ModuleAmount from '../ModuleAmount.svelte';
  import { errorText } from '../../lib/dialogs.svelte';
  import { ErrorCode } from '../../lib/bridge';
  import { toast } from '../../lib/state.svelte';
  import { amountProblem, chainState, chainWallet, refreshSummary } from '../../lib/chains.svelte';
  import { COIN_ID, dexHost, pairedAmount, shareOf, stale, type Pool, type Position } from '../../lib/dex.svelte';

  let { stem, name, pool, position, tab = $bindable('add') }: { stem: string; name: string; pool: Pool; position: Position | null; tab?: 'add' | 'remove' } = $props();

  const summary = $derived(chainState.summaries[stem]);
  const holding = (id: string) => (id === COIN_ID ? (summary?.balances?.trusted ?? null) : (summary?.assets?.find((a) => a.id === id)?.balance ?? '0'));
  const decimals = (id: string) => (id === COIN_ID ? 8 : (summary?.assets?.find((a) => a.id === id)?.decimals ?? null));

  let amountA = $state('');
  let amountB = $state('');
  let percent = $state('50');
  let busy = $state(false);
  let error = $state('');
  let done = $state('');

  // Another tab: what the last one said goes.
  $effect(() => {
    void tab;
    done = '';
    error = '';
  });
  const decA = $derived(decimals(pool.a.id));
  const decB = $derived(decimals(pool.b.id));
  // Into a pool someone provides to: the second amount at its price; a pool with no providers: both typed.
  const open = $derived(!pool.abandoned && /[1-9]/.test(pool.reserveA));
  $effect(() => {
    if (!open || decA === null || decB === null) return;
    const paired = amountA.trim() && !amountProblem(amountA, decA, null) ? pairedAmount(amountA, decA, pool.reserveA, pool.reserveB, decB) : null;
    amountB = paired ?? '';
  });
  const problemA = $derived(amountA.trim() && decA !== null ? amountProblem(amountA, decA, holding(pool.a.id)) : null);
  const problemB = $derived(amountB.trim() && decB !== null ? amountProblem(amountB, decB, holding(pool.b.id)) : null);
  const canAdd = $derived(!busy && !!amountA.trim() && !!amountB.trim() && !problemA && !problemB && decA !== null && decB !== null);

  const pctOk = $derived(/^(100(\.0{1,2})?|\d{1,2}(\.\d{1,2})?)$/.test(percent.trim()) && parseFloat(percent) > 0);
  const outA = $derived(position && pctOk ? shareOf(position.valueA, percent.trim()) : null);
  const outB = $derived(position && pctOk ? shareOf(position.valueB, percent.trim()) : null);
  const canRemove = $derived(!busy && !!position && pctOk);

  async function run(what: 'add' | 'remove') {
    busy = true;
    error = '';
    done = '';
    try {
      const r =
        what === 'add'
          ? await dexHost.addLiquidity({ stem, wallet: chainWallet(stem), a: pool.a.id, b: pool.b.id, amountA: amountA.trim(), amountB: amountB.trim(), setsPrice: !open })
          : await dexHost.removeLiquidity({ stem, wallet: chainWallet(stem), pool: pool.pool, percent: percent.trim() });
      done = what === 'add' ? t('Liquidity added on %1.', name) : t('Liquidity taken out on %1.', name);
      toast('success', done, r.txid ? t('Transaction %1…', r.txid.slice(0, 16)) : '');
      amountA = '';
      stale(`pools:${stem}`, `positions:${stem}`);
      void refreshSummary(stem, 0);
    } catch (e) {
      const code = e && typeof e === 'object' && 'code' in e ? (e as { code: number }).code : 0;
      error = code === ErrorCode.CANCELLED ? t('Declined in the dialog: nothing was sent.') : errorText(e);
    } finally {
      busy = false;
    }
  }
</script>

<section class="panel liq" aria-label={t('Liquidity')} data-liquidity={tab}>
  <div class="tabs" role="tablist" aria-label={t('Liquidity')}>
    <button type="button" role="tab" aria-selected={tab === 'add'} onclick={() => (tab = 'add')} data-liq-tab="add">{t('Add liquidity')}</button>
    <button type="button" role="tab" aria-selected={tab === 'remove'} onclick={() => (tab = 'remove')} disabled={!position} data-liq-tab="remove">{t('Remove')}</button>
  </div>

  {#if tab === 'add'}
    <div class="field">
      <div class="lab"><label for="liq-a"><SafeText text={pool.a.label} /></label><span class="muted small">{t('Balance')} <ModuleAmount text={holding(pool.a.id)} /></span></div>
      <input id="liq-a" class="num" type="text" inputmode="decimal" placeholder="0.00" bind:value={amountA} data-liq-amount-a />
      {#if problemA}<span class="error small">{t(problemA, String(decA ?? 8))}</span>{/if}
    </div>
    <div class="field">
      <div class="lab"><label for="liq-b"><SafeText text={pool.b.label} /></label><span class="muted small">{t('Balance')} <ModuleAmount text={holding(pool.b.id)} /></span></div>
      <input id="liq-b" class="num" type="text" inputmode="decimal" placeholder="0.00" bind:value={amountB} readonly={open} data-liq-amount-b />
      {#if problemB}<span class="error small">{t(problemB, String(decB ?? 8))}</span>{:else if open}<span class="muted small">{t('At the pool\'s price now; the chain takes only what its price takes.')}</span>{:else}<span class="warn small">{t('Nobody provides to this pool: your two amounts set its price.')}</span>{/if}
    </div>
    {#if error}<p class="error" role="alert" data-liq-error><SafeText text={error} /></p>{/if}
    {#if done}<p class="ok" data-liq-done><SafeText text={done} /></p>{/if}
    <footer class="actions">
      <button type="button" class="btn primary" disabled={!canAdd} onclick={() => run('add')} data-liq-add><Icon name="plus" size={16} /> {busy ? t('Working…') : t('Add liquidity…')}</button>
      <span class="muted small">{t('You confirm the details in the next step.')}</span>
    </footer>
  {:else}
    <div class="field">
      <label for="liq-pct">{t('Share of your liquidity to take out')}</label>
      <div class="pct-row">
        {#each ['25', '50', '75', '100'] as p (p)}<button type="button" class="chip" aria-pressed={percent === p} onclick={() => (percent = p)} data-liq-pct={p}>{p} %</button>{/each}
        <input id="liq-pct" class="num pct" type="text" inputmode="decimal" bind:value={percent} aria-invalid={!pctOk ? 'true' : undefined} data-liq-percent /><span class="muted">%</span>
      </div>
    </div>
    {#if outA && outB}
      <dl class="facts">
        <dt>{t('You get about')}</dt>
        <dd class="num"><ModuleAmount text={outA} unit={pool.a.id === position?.a.id ? pool.a.label : pool.b.label} /></dd>
        <dt></dt>
        <dd class="num"><ModuleAmount text={outB} unit={pool.a.id === position?.a.id ? pool.b.label : pool.a.label} /></dd>
      </dl>
    {/if}
    {#if error}<p class="error" role="alert" data-liq-error><SafeText text={error} /></p>{/if}
    {#if done}<p class="ok" data-liq-done><SafeText text={done} /></p>{/if}
    <footer class="actions">
      <button type="button" class="btn primary" disabled={!canRemove} onclick={() => run('remove')} data-liq-remove>{busy ? t('Working…') : t('Remove liquidity…')}</button>
      <span class="muted small">{t('You confirm the details in the next step.')}</span>
    </footer>
  {/if}
</section>

<style>
  .liq {
    display: flex;
    flex-direction: column;
    gap: 0.9rem;
    padding: 1rem 1.3rem 1.2rem;
  }
  .field {
    display: flex;
    flex-direction: column;
    gap: 0.3rem;
  }
  .lab {
    display: flex;
    justify-content: space-between;
    gap: 0.6rem;
    font-weight: 600;
  }
  .pct-row {
    display: flex;
    gap: 0.35rem;
    align-items: center;
    flex-wrap: wrap;
  }
  .chip {
    padding: 0.25rem 0.7rem;
    border: 1px solid var(--line);
    border-radius: 999px;
    background: var(--surface-2);
    color: var(--text);
    font: inherit;
    font-size: 0.85rem;
    cursor: pointer;
  }
  .chip[aria-pressed='true'] {
    border-color: var(--accent);
    box-shadow: 0 0 0 1px var(--accent) inset;
  }
  .pct {
    width: 5rem;
  }
  .facts {
    display: grid;
    grid-template-columns: auto 1fr;
    gap: 0.25rem 1rem;
    margin: 0;
  }
  .facts dt {
    color: var(--text-muted);
  }
  .facts dd {
    margin: 0;
    text-align: right;
  }
  .actions {
    display: flex;
    gap: 1rem;
    align-items: center;
    flex-wrap: wrap;
  }
  .small {
    font-size: 0.85rem;
  }
  .warn {
    color: var(--warning);
  }
  .ok {
    color: var(--positive);
  }
  p {
    margin: 0;
  }
</style>
