<script lang="ts">
  // Swap on one chain's DEX (doc/unified-app.md §22): what is paid and what comes, the chain's own quote (its routes
  // through its pools where it has routes, else the pool of the two), the route said hop by hop with each pool's kind
  // and fee, the price impact, the least received at the slippage chosen. The swap is built by the host from these
  // fields, quoted again there and its route checked, then asked in the trusted dialog by the chain's own describer.
  // Simple mode: the same swap, its route in one line, no choice of route or slippage beyond the default.
  import { t } from '../../lib/i18n.svelte';
  import Icon from '../Icon.svelte';
  import SafeText from '../SafeText.svelte';
  import ModuleAmount from '../ModuleAmount.svelte';
  import AssetPicker from './AssetPicker.svelte';
  import RouteView from './RouteView.svelte';
  import { errorText } from '../../lib/dialogs.svelte';
  import { ErrorCode } from '../../lib/bridge';
  import { prefs, savePrefs, toast } from '../../lib/state.svelte';
  import { amountProblem, chainState, chainWallet, refreshSummary } from '../../lib/chains.svelte';
  import { COIN_ID, dex, dexHost, minReceived, slippageBps, stale, tradedAssets, trimNumber, type Quote } from '../../lib/dex.svelte';
  import { go } from '../../lib/nav.svelte';

  let { stem, name, simple = false }: { stem: string; name: string; simple?: boolean } = $props();

  const info = $derived(dex.info[stem]);
  const pools = $derived(dex.pools[stem]);
  const summary = $derived(chainState.summaries[stem]);
  const assets = $derived(tradedAssets(pools));
  /** What the wallet holds of each asset the pools trade (the coin: its available coins). */
  const balances = $derived.by(() => {
    const out: Record<string, string> = {};
    if (summary?.balances?.trusted) out[COIN_ID] = summary.balances.trusted;
    for (const a of summary?.assets ?? []) out[a.id] = a.balance;
    return out;
  });
  const decimalsOf = (id: string) => (id === COIN_ID ? 8 : (summary?.assets?.find((a) => a.id === id)?.decimals ?? null));

  let payId = $state('');
  let getId = $state('');
  let amount = $state('');
  let routeAt = $state(0);
  let busy = $state(false);
  let error = $state('');
  let done = $state<{ txid?: string; minOut: string; unit: string } | null>(null);
  let quote = $state<{ key: string; data: Quote | null; error: string; loading: boolean }>({ key: '', data: null, error: '', loading: false });
  let customSlip = $state('');

  // The pair by default: the coin, and the asset of the deepest pool with it.
  $effect(() => {
    if (!assets.length) return;
    if (!payId || !assets.some((a) => a.id === payId)) payId = assets.find((a) => a.id === COIN_ID)?.id ?? assets[0].id;
    if (!getId || getId === payId || !assets.some((a) => a.id === getId)) getId = assets.find((a) => a.id !== payId)?.id ?? '';
  });

  const payLabel = $derived(assets.find((a) => a.id === payId)?.label ?? '');
  const getLabel = $derived(assets.find((a) => a.id === getId)?.label ?? '');
  const decIn = $derived(quote.data?.in === payId ? quote.data.decimalsIn : decimalsOf(payId));
  const amountError = $derived(amount.trim() && decIn !== null ? amountProblem(amount, decIn, balances[payId] ?? '0') : null);
  const maxHops = $derived(info?.features.route ? info.maxHops : 1);
  const key = $derived(`${stem}|${payId}|${getId}|${amount.trim()}|${maxHops}`);

  async function ask(k: string) {
    const [, p, g, a, h] = k.split('|');
    quote = { ...quote, key: k, loading: true };
    try {
      const data = await dexHost.quote(stem, p, g, a, Number(h));
      if (quote.key !== k) return;
      // The same answer: left in place (nothing redrawn).
      if (JSON.stringify(quote.data) !== JSON.stringify(data) || quote.error) quote = { key: k, data, error: '', loading: false };
      else quote.loading = false;
    } catch (e) {
      if (quote.key === k) quote = { key: k, data: null, error: errorText(e), loading: false };
    }
  }

  // Quoted a moment after the typing stops, then again every 15 s while it is shown (the pools move).
  $effect(() => {
    const k = key;
    const ready = !!payId && !!getId && payId !== getId && !!amount.trim() && !amountProblem(amount, decIn ?? 8, null);
    if (!ready) {
      quote = { key: '', data: null, error: '', loading: false };
      return;
    }
    routeAt = 0;
    const first = setTimeout(() => void ask(k), 350);
    const again = setInterval(() => {
      if (!document.hidden && !busy) void ask(k);
    }, 15_000);
    return () => {
      clearTimeout(first);
      clearInterval(again);
    };
  });

  const fresh = $derived(quote.data && quote.key === key ? quote.data : null);
  const route = $derived(fresh?.routes[Math.min(routeAt, (fresh?.routes.length ?? 1) - 1)] ?? null);
  const bps = $derived(prefs.dexSlippage);
  const least = $derived(route && fresh ? minReceived(route.amountOut, fresh.decimalsOut, bps) : null);
  const impact = $derived(route?.priceImpact != null ? parseFloat(route.priceImpact) : null);
  const rate = $derived(route && fresh ? trimNumber(parseFloat(route.amountOut) / parseFloat(fresh.amountIn)) : null);
  const canSwap = $derived(!busy && !!route && !!least && !amountError && !quote.loading && (impact === null || impact < 50));

  function flip() {
    const p = payId;
    payId = getId;
    getId = p;
    amount = '';
  }
  function useAll() {
    const b = balances[payId];
    if (b) amount = b;
  }
  function setSlip(v: number) {
    if (v !== prefs.dexSlippage) savePrefs({ dexSlippage: v });
    customSlip = '';
  }
  function typedSlip() {
    const v = slippageBps(customSlip);
    if (v !== null) savePrefs({ dexSlippage: v });
  }

  async function swap() {
    if (!canSwap || !route || !least || !fresh) return;
    busy = true;
    error = '';
    done = null;
    try {
      const r = await dexHost.swap({ stem, wallet: chainWallet(stem), in: payId, out: getId, amount: fresh.amountIn, path: route.path, slippage: bps, minOut: least });
      done = { txid: r.txid, minOut: r.minOut, unit: getLabel };
      toast('success', t('Swap sent on %1', name), r.txid ? t('Transaction %1…', r.txid.slice(0, 16)) : '');
      amount = '';
      stale(`pools:${stem}`, `positions:${stem}`);
      void refreshSummary(stem, 0);
    } catch (e) {
      const code = e && typeof e === 'object' && 'code' in e ? (e as { code: number }).code : 0;
      error = code === ErrorCode.CANCELLED ? t('Declined in the dialog: nothing was swapped.') : errorText(e);
      if (code !== ErrorCode.CANCELLED) void ask(key);
    } finally {
      busy = false;
    }
  }
  const SLIPS = [10, 50, 100];
</script>

<div class="swap" class:simple data-swap={stem}>
  <section class="panel form" aria-label={t('Swap on %1', name)}>
    {#if !pools}
      <p class="muted">{t('Reading the pools of %1…', name)}</p>
    {:else if assets.length < 2}
      <p class="muted" data-swap-empty>{t('%1 has no pool yet.', name)} {#if !simple}<button type="button" class="btn ghost small" onclick={() => go('trade', 'create')}>{t('Create a pool')}</button>{/if}</p>
    {:else}
      <div class="side">
        <div class="side-head">
          <label for="swap-pay-{stem}" class="label">{t('You pay')}</label>
          <span class="muted small">{t('Balance')} <ModuleAmount text={balances[payId] ?? '0'} />
            {#if balances[payId] && /[1-9]/.test(balances[payId])}<button type="button" class="link" onclick={useAll} data-swap-max>{t('Max')}</button>{/if}
          </span>
        </div>
        <div class="side-row">
          <input id="swap-pay-{stem}" class="num big" type="text" inputmode="decimal" autocomplete="off" placeholder="0.00" bind:value={amount} aria-invalid={amountError ? 'true' : undefined} data-swap-amount />
          <AssetPicker {stem} chainName={name} {assets} {balances} value={payId} onpick={(id) => { if (id === getId) getId = payId; payId = id; }} label={t('What you pay')} side="pay" />
        </div>
        {#if amountError}<span class="error small">{t(amountError, String(decIn ?? 8))}</span>{/if}
      </div>

      <div class="flip-row"><button type="button" class="flip" onclick={flip} aria-label={t('Swap the two sides')} title={t('Swap the two sides')} data-swap-flip><Icon name="swap" size={17} /></button></div>

      <div class="side">
        <div class="side-head">
          <span class="label">{t('You receive, about')}</span>
          <span class="muted small">{t('Balance')} <ModuleAmount text={balances[getId] ?? '0'} /></span>
        </div>
        <div class="side-row">
          <span class="num big out" data-swap-out>{#if route}<ModuleAmount text={route.amountOut} />{:else if quote.loading}<span class="muted">…</span>{:else}<span class="faint">0.00</span>{/if}</span>
          <AssetPicker {stem} chainName={name} {assets} {balances} value={getId} onpick={(id) => { if (id === payId) payId = getId; getId = id; }} label={t('What you receive')} side="get" />
        </div>
      </div>

      {#if quote.error}
        <p class="error small" role="alert" data-swap-quote-error><SafeText text={quote.error} /></p>
      {:else if fresh && !route}
        <p class="warn small" data-swap-noroute>{t('No route between these two assets in the pools of %1.', name)}</p>
      {/if}

      {#if route && fresh}
        <dl class="facts" data-swap-facts>
          <dt>{t('Rate')}</dt>
          <dd class="num">1 <SafeText text={payLabel} /> ≈ <SafeText text={rate ?? ''} /> <SafeText text={getLabel} /></dd>
          <dt>{t('You receive at least')}</dt>
          <dd class="num" data-swap-least><ModuleAmount text={least} unit={getLabel} /></dd>
          {#if simple}
            {#if route.hops.length > 1}<dt>{t('Through')}</dt><dd><SafeText text={route.labels.slice(1, -1).join(' → ')} /></dd>{/if}
          {:else}
            <dt>{t('Price impact')}</dt>
            <dd class="num" class:warn={impact !== null && impact >= 3} class:bad={impact !== null && impact >= 10} data-swap-impact>{impact === null ? '–' : `${impact.toFixed(2)} %`}</dd>
          {/if}
        </dl>
        {#if impact !== null && impact >= 10}
          <p class="warn small" data-swap-impact-warn>{t('A large trade for these pools: the price you get is much worse than theirs. Try a smaller amount.')}</p>
        {/if}
      {/if}

      {#if !simple}
        <div class="slip" role="group" aria-label={t('Slippage')}>
          <span class="label small">{t('Slippage')}</span>
          {#each SLIPS as v (v)}
            <button type="button" class="chip" aria-pressed={bps === v} onclick={() => setSlip(v)} data-swap-slippage={v}>{(v / 100).toString()} %</button>
          {/each}
          <input class="num chip-input" type="text" inputmode="decimal" placeholder={t('Other')} bind:value={customSlip} onchange={typedSlip} aria-label={t('Another slippage, in percent')} />
          {#if !SLIPS.includes(bps)}<span class="muted small">{(bps / 100).toFixed(2)} %</span>{/if}
        </div>
      {/if}

      {#if error}<p class="error" role="alert" data-swap-error><SafeText text={error} /></p>{/if}
      {#if done}
        <p class="ok" data-swap-done>{t('Swap sent: at least %1 %2 when its block clears it.', done.minOut, done.unit)} <button type="button" class="btn ghost small" onclick={() => go('transactions')}>{t('Transactions')}</button></p>
      {/if}

      <footer class="actions">
        <button type="button" class="btn primary" disabled={!canSwap} onclick={swap} data-swap-go><Icon name="swap" size={17} /> {busy ? t('Working…') : t('Swap…')}</button>
        <span class="muted small">{t('You confirm the details in the next step.')}</span>
      </footer>
    {/if}
  </section>

  {#if !simple}
    <section class="panel route" aria-label={t('Route')} data-swap-route-panel>
      <h2>{t('Route')}</h2>
      {#if route && fresh}
        <RouteView {stem} {name} {route} amountIn={fresh.amountIn} />
        {#if fresh.routes.length > 1}
          <div class="others" role="radiogroup" aria-label={t('Other routes')}>
            <span class="caps faint">{t('Other routes')}</span>
            {#each fresh.routes as r, i (r.path.join('>'))}
              <button type="button" role="radio" class="other" aria-checked={i === routeAt} onclick={() => (routeAt = i)} data-swap-route={i}>
                <span><SafeText text={r.labels.join(' → ')} /></span>
                <span class="num"><ModuleAmount text={r.amountOut} unit={getLabel} /></span>
              </button>
            {/each}
          </div>
        {/if}
        <p class="faint small">{fresh.via === 'route' ? t('%1 clears every swap of a block together, at one price per pool. If the route would give less than the least, what you paid comes back.', name) : t('A swap in one pool, at its price when the block takes it.')}</p>
      {:else}
        <p class="muted">{t('Enter an amount to see the best route through the pools of %1.', name)}</p>
        {#if info && !info.features.route}<p class="faint small">{t('%1 swaps in the pool of the two assets.', name)}</p>{/if}
      {/if}
    </section>
  {/if}
</div>

<style>
  .swap {
    display: grid;
    grid-template-columns: minmax(0, 30rem) minmax(0, 1fr);
    gap: 1.1rem;
    align-items: start;
  }
  .swap.simple {
    grid-template-columns: minmax(0, 32rem);
  }
  @media (max-width: 62rem) {
    .swap {
      grid-template-columns: minmax(0, 1fr);
    }
  }
  .form {
    display: flex;
    flex-direction: column;
    gap: 0.8rem;
    padding: 1.1rem 1.3rem 1.2rem;
  }
  .route {
    display: flex;
    flex-direction: column;
    gap: 0.9rem;
    padding: 1.1rem 1.3rem 1.2rem;
  }
  .route h2 {
    margin: 0;
    font-size: 1.2rem;
  }
  .side {
    display: flex;
    flex-direction: column;
    gap: 0.35rem;
    padding: 0.8rem 0.9rem;
    border: 1px solid var(--line);
    border-radius: var(--radius-l);
    background: var(--surface-2);
  }
  .side-head {
    display: flex;
    justify-content: space-between;
    align-items: baseline;
    gap: 0.5rem;
  }
  .side-row {
    display: flex;
    align-items: center;
    gap: 0.6rem;
  }
  .label {
    font-weight: 650;
  }
  .big {
    flex: 1;
    min-width: 0;
    font-size: 1.45rem;
    font-weight: 650;
  }
  input.big {
    border: 0;
    background: none;
    padding: 0.15rem 0;
    color: var(--text);
    outline: none;
  }
  .out {
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .flip-row {
    display: flex;
    justify-content: center;
    margin: -0.75rem 0;
    z-index: 1;
  }
  .flip {
    display: grid;
    place-items: center;
    width: 2.1rem;
    height: 2.1rem;
    border: 1px solid var(--line-strong, var(--line));
    border-radius: 50%;
    background: var(--surface);
    color: var(--text-muted);
    cursor: pointer;
    transform: rotate(90deg);
  }
  .flip:hover {
    color: var(--text);
  }
  .facts {
    display: grid;
    grid-template-columns: auto 1fr;
    gap: 0.35rem 1rem;
    margin: 0;
  }
  .facts dt {
    color: var(--text-muted);
  }
  .facts dd {
    margin: 0;
    text-align: right;
    font-variant-numeric: tabular-nums;
  }
  .warn {
    color: var(--warning);
  }
  .bad {
    color: var(--negative);
  }
  .slip {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: 0.35rem;
  }
  .slip .label {
    margin-right: 0.3rem;
  }
  .chip {
    padding: 0.2rem 0.65rem;
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
  .chip-input {
    width: 4.5rem;
    padding: 0.2rem 0.5rem;
    font-size: 0.85rem;
  }
  .link {
    border: 0;
    background: none;
    color: var(--accent-strong);
    font: inherit;
    font-weight: 650;
    cursor: pointer;
    padding: 0 0 0 0.3rem;
  }
  .actions {
    display: flex;
    gap: 1rem;
    align-items: center;
    flex-wrap: wrap;
  }
  .others {
    display: flex;
    flex-direction: column;
    gap: 0.3rem;
  }
  .other {
    display: flex;
    justify-content: space-between;
    gap: 1rem;
    padding: 0.45rem 0.6rem;
    border: 1px solid var(--line);
    border-radius: var(--radius-m);
    background: var(--surface-2);
    color: var(--text);
    font: inherit;
    cursor: pointer;
    text-align: left;
  }
  .other[aria-checked='true'] {
    border-color: var(--accent);
  }
  .small {
    font-size: 0.85rem;
  }
  .ok {
    color: var(--positive);
  }
  p {
    margin: 0;
  }
</style>
