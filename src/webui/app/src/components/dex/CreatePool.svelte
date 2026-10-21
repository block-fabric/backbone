<script lang="ts">
  // A new pool, in four steps (doc/unified-app.md §22): the pair; its kind (the curve: constant product, stable with its
  // amplification A, weighted with its weights; the fee tier); the first amounts, which set its price; then a review.
  // The kinds a chain offers are those its node says apply (chain.dex.info: Layers' pool kinds once its rules apply);
  // until then, and on a chain with only constant-product pools at 0.30 %, the other kinds are shown, not offered.
  import { t } from '../../lib/i18n.svelte';
  import Icon from '../Icon.svelte';
  import SafeText from '../SafeText.svelte';
  import ModuleAmount from '../ModuleAmount.svelte';
  import AssetPicker from './AssetPicker.svelte';
  import { errorText } from '../../lib/dialogs.svelte';
  import { ErrorCode } from '../../lib/bridge';
  import { toast } from '../../lib/state.svelte';
  import { amountProblem, chainState, chainUnit, chainWallet, refreshSummary } from '../../lib/chains.svelte';
  import { COIN_ID, dex, dexHost, feeText, poolTab, stale, trimNumber, type Curve, type DexAsset } from '../../lib/dex.svelte';
  import { go } from '../../lib/nav.svelte';

  let { stem, name }: { stem: string; name: string } = $props();

  const STEPS = [
    { id: 'pair', label: 'Pair' },
    { id: 'kind', label: 'Kind' },
    { id: 'amounts', label: 'Amounts' },
    { id: 'review', label: 'Review' },
  ] as const;
  type Step = (typeof STEPS)[number]['id'];
  let step = $state<Step>('pair');

  // What the wallet holds now (a pool starts with both assets): read when the wizard opens.
  $effect(() => void refreshSummary(stem, 1000));
  const info = $derived(dex.info[stem]);
  const summary = $derived(chainState.summaries[stem]);
  /** What the wallet holds (the coin first): a pool starts with both of its assets. */
  const assets = $derived<DexAsset[]>([{ id: COIN_ID, label: chainUnit(stem) }, ...(summary?.assets ?? []).map((a) => ({ id: a.id, label: a.label }))]);
  const balances = $derived.by(() => {
    const out: Record<string, string> = {};
    if (summary?.balances?.trusted) out[COIN_ID] = summary.balances.trusted;
    for (const a of summary?.assets ?? []) out[a.id] = a.balance;
    return out;
  });
  const decimals = (id: string) => (id === COIN_ID ? 8 : (summary?.assets?.find((a) => a.id === id)?.decimals ?? 8));

  let a = $state('');
  let b = $state(COIN_ID);
  let curve = $state<Curve>('constant-product');
  let fee = $state(3000);
  let amp = $state(100);
  let weightA = $state(80);
  let amountA = $state('');
  let amountB = $state('');
  let busy = $state(false);
  let error = $state('');

  const curvesOn = $derived(!!info?.curvesActive);
  const tiers = $derived(curvesOn ? info!.feeTiers : [3000]);
  const allTiers = [100, 500, 3000, 10000];
  const weights = $derived(curvesOn && info!.weights.length ? info!.weights : [2, 5, 10, 20, 25, 40, 60, 75, 80, 90, 95, 98]);
  const labelOf = (id: string) => assets.find((x) => x.id === id)?.label ?? '';
  // A pool of this pair there is already (of the kind every chain makes): liquidity goes into it instead.
  const existing = $derived(dex.pools[stem]?.pools.find((p) => !p.abandoned && p.curve === curve && p.fee === fee && ((p.a.id === a && p.b.id === b) || (p.a.id === b && p.b.id === a))) ?? null);
  const pairOk = $derived(!!a && !!b && a !== b);
  const kindOk = $derived(curve === 'constant-product' ? true : curvesOn && (curve !== 'stable' || (amp >= 1 && amp <= 10000)));
  const problemA = $derived(amountA.trim() ? amountProblem(amountA, decimals(a), balances[a] ?? '0') : 'Enter an amount');
  const problemB = $derived(amountB.trim() ? amountProblem(amountB, decimals(b), balances[b] ?? '0') : 'Enter an amount');
  const amountsOk = $derived(!problemA && !problemB);
  const price = $derived(amountsOk ? trimNumber(parseFloat(amountB) / parseFloat(amountA)) : null);
  const kindWords = $derived(curve === 'stable' ? `${t('Stable')} · A ${amp}` : curve === 'weighted' ? `${t('Weighted')} ${weightA}/${100 - weightA}` : t('Constant product'));
  const at = $derived(STEPS.findIndex((s) => s.id === step));
  const canNext = $derived(step === 'pair' ? pairOk && !existing : step === 'kind' ? kindOk && !existing : step === 'amounts' ? amountsOk : false);

  $effect(() => {
    if (!a) a = assets.find((x) => x.id !== COIN_ID)?.id ?? '';
  });
  function pickCurve(c: Curve) {
    if (c !== 'constant-product' && !curvesOn) return;
    curve = c;
    if (!tiers.includes(fee)) fee = tiers[0];
  }
  async function create() {
    busy = true;
    error = '';
    try {
      const r = await dexHost.createPool({ stem, wallet: chainWallet(stem), a, b, amountA: amountA.trim(), amountB: amountB.trim(), curve, fee, ...(curve === 'stable' ? { amp } : {}), ...(curve === 'weighted' ? { weightA } : {}) });
      toast('success', t('Pool created on %1', name), r.txid ? t('Transaction %1…', r.txid.slice(0, 16)) : '');
      stale(`pools:${stem}`, `positions:${stem}`);
      void refreshSummary(stem, 0);
      go('trade', 'pools');
    } catch (e) {
      const code = e && typeof e === 'object' && 'code' in e ? (e as { code: number }).code : 0;
      error = code === ErrorCode.CANCELLED ? t('Declined in the dialog: nothing was sent.') : errorText(e);
    } finally {
      busy = false;
    }
  }
  const CURVES: { id: Curve; title: string; what: string }[] = [
    { id: 'constant-product', title: 'Constant product', what: 'x · y = k. For any two assets; the price moves with every trade.' },
    { id: 'stable', title: 'Stable', what: 'For two assets worth about the same (two dollars): low slippage near one to one, set by A.' },
    { id: 'weighted', title: 'Weighted', what: 'Two assets in other shares than half each (80/20): the larger side moves its price less.' },
  ];
</script>

<section class="panel wizard" aria-label={t('Create a pool')} data-create-pool={step}>
  <header class="head">
    <button type="button" class="btn ghost small" onclick={() => go('trade', 'pools')}><Icon name="chevron-left" size={16} /> {t('Pools')}</button>
    <h2>{t('Create a pool on %1', name)}</h2>
  </header>
  <ol class="steps">
    {#each STEPS as s, i (s.id)}
      <li class:done={i < at} class:now={i === at}><span class="n">{i + 1}</span>{t(s.label)}</li>
    {/each}
  </ol>

  {#if step === 'pair'}
    <div class="body">
      <p class="muted">{t('The two assets the pool trades. You put both in, from this wallet.')}</p>
      <div class="pair">
        <div class="field"><span class="label">{t('First asset')}</span><AssetPicker {stem} chainName={name} {assets} {balances} value={a} onpick={(id) => { if (id === b) b = a; a = id; }} label={t('First asset')} side="a" /></div>
        <span class="plus" aria-hidden="true">+</span>
        <div class="field"><span class="label">{t('Second asset')}</span><AssetPicker {stem} chainName={name} {assets} {balances} value={b} onpick={(id) => { if (id === a) a = b; b = id; }} label={t('Second asset')} side="b" /></div>
      </div>
    </div>
  {:else if step === 'kind'}
    <div class="body">
      <div class="curves" role="radiogroup" aria-label={t('Curve')}>
        {#each CURVES as c (c.id)}
          {@const off = c.id !== 'constant-product' && !curvesOn}
          <button type="button" role="radio" class="curve" class:on={curve === c.id} aria-checked={curve === c.id} aria-disabled={off} disabled={off} onclick={() => pickCurve(c.id)} data-curve={c.id}>
            <strong>{t(c.title)}</strong>
            <span class="muted small">{t(c.what)}</span>
            {#if off}<span class="badge">{t('Not on %1 yet', name)}</span>{/if}
          </button>
        {/each}
      </div>
      <div class="field">
        <span class="label">{t('Fee tier')}</span>
        <div class="chips" role="radiogroup" aria-label={t('Fee tier')}>
          {#each allTiers as f (f)}
            <button type="button" role="radio" class="chip" aria-checked={fee === f} disabled={!tiers.includes(f)} onclick={() => (fee = f)} data-fee-tier={f}><SafeText text={feeText(f)} /></button>
          {/each}
        </div>
        <span class="muted small">{t('What every trade pays the providers. Lower for assets that move together, higher for rare pairs.')}</span>
      </div>
      {#if curve === 'stable'}
        <div class="field">
          <label class="label" for="pool-amp">{t('Amplification (A)')}</label>
          <div class="range-row"><input id="pool-amp" type="range" min="1" max="1000" step="1" bind:value={amp} data-pool-amp /><input class="num amp" type="number" min="1" max="10000" bind:value={amp} aria-label={t('Amplification (A)')} /></div>
          <span class="muted small">{t('Higher: flatter near one to one, steeper away from it. 100 suits most pairs that are worth the same.')}</span>
        </div>
      {:else if curve === 'weighted'}
        <div class="field">
          <span class="label">{t('Weights')}</span>
          <div class="chips" role="radiogroup" aria-label={t('Weights')}>
            {#each weights as w (w)}<button type="button" role="radio" class="chip" aria-checked={weightA === w} onclick={() => (weightA = w)} data-pool-weight={w}><SafeText text={`${labelOf(a)} ${w} / ${100 - w}`} /></button>{/each}
          </div>
        </div>
      {/if}
      {#if !curvesOn}<p class="faint small" data-curves-off>{t('%1 makes constant-product pools at 0.30 % for now; the other kinds come with its pool kinds.', name)}</p>{/if}
    </div>
  {:else if step === 'amounts'}
    <div class="body">
      <p class="muted">{t('The first amounts set the pool\'s price. Put them in at the price the market has elsewhere, or traders will take the difference.')}</p>
      {#each [{ id: a, get: () => amountA, set: (v: string) => (amountA = v), p: problemA, k: 'a' }, { id: b, get: () => amountB, set: (v: string) => (amountB = v), p: problemB, k: 'b' }] as s (s.k)}
        <div class="field">
          <div class="lab"><label for="pool-amount-{s.k}"><SafeText text={labelOf(s.id)} /></label><span class="muted small">{t('Balance')} <ModuleAmount text={balances[s.id] ?? '0'} /></span></div>
          <input id="pool-amount-{s.k}" class="num" type="text" inputmode="decimal" placeholder="0.00" value={s.get()} oninput={(e) => s.set((e.currentTarget as HTMLInputElement).value)} data-pool-amount={s.k} />
          {#if s.get().trim() && s.p}<span class="error small">{t(s.p, String(decimals(s.id)))}</span>{/if}
        </div>
      {/each}
      {#if price}<p class="num" data-pool-first-price>{t('First price')}: 1 <SafeText text={labelOf(a)} /> = <SafeText text={price} /> <SafeText text={labelOf(b)} /></p>{/if}
    </div>
  {:else}
    <div class="body">
      <dl class="facts" data-pool-review>
        <dt>{t('Chain')}</dt><dd><SafeText text={name} /></dd>
        <dt>{t('Pair')}</dt><dd><SafeText text={`${labelOf(a)} / ${labelOf(b)}`} /></dd>
        <dt>{t('Curve')}</dt><dd><SafeText text={kindWords} /></dd>
        <dt>{t('Fee tier')}</dt><dd><SafeText text={feeText(fee)} /></dd>
        <dt>{t('You put in')}</dt><dd class="num"><ModuleAmount text={amountA} unit={labelOf(a)} /></dd>
        <dt></dt><dd class="num"><ModuleAmount text={amountB} unit={labelOf(b)} /></dd>
        <dt>{t('First price')}</dt><dd class="num">1 <SafeText text={labelOf(a)} /> = <SafeText text={price ?? '–'} /> <SafeText text={labelOf(b)} /></dd>
      </dl>
      {#if error}<p class="error" role="alert" data-pool-error><SafeText text={error} /></p>{/if}
    </div>
  {/if}

  {#if existing && (step === 'pair' || step === 'kind')}
    <p class="warn small" data-pool-exists>{t('A pool of this pair and kind exists: add liquidity to it instead.')} <button type="button" class="btn ghost small" onclick={() => go('trade', poolTab(stem, existing.pool))}>{t('Open it')}</button></p>
  {/if}

  <footer class="actions">
    {#if at > 0}<button type="button" class="btn ghost" onclick={() => (step = STEPS[at - 1].id)} data-pool-back-step>{t('Back')}</button>{/if}
    <span class="grow"></span>
    {#if step === 'review'}
      <span class="muted small">{t('You confirm the details in the next step.')}</span>
      <button type="button" class="btn primary" disabled={busy} onclick={create} data-pool-go><Icon name="plus" size={16} /> {busy ? t('Working…') : t('Create the pool…')}</button>
    {:else}
      <button type="button" class="btn primary" disabled={!canNext} onclick={() => (step = STEPS[at + 1].id)} data-pool-next>{t('Next')} <Icon name="chevron-right" size={16} /></button>
    {/if}
  </footer>
</section>

<style>
  .wizard {
    display: flex;
    flex-direction: column;
    gap: 1rem;
    padding: 1.1rem 1.3rem 1.2rem;
    max-width: 52rem;
  }
  .head {
    display: flex;
    align-items: center;
    gap: 0.7rem;
  }
  h2 {
    margin: 0;
    font-size: 1.2rem;
  }
  .steps {
    display: flex;
    gap: 0.4rem;
    list-style: none;
    margin: 0;
    padding: 0 0 0.8rem;
    border-bottom: 1px solid var(--line);
    flex-wrap: wrap;
  }
  .steps li {
    display: inline-flex;
    align-items: center;
    gap: 0.4rem;
    padding: 0.2rem 0.7rem 0.2rem 0.25rem;
    border-radius: 999px;
    color: var(--text-muted);
    font-weight: 600;
  }
  .steps li.now {
    background: color-mix(in srgb, var(--accent) 12%, transparent);
    color: var(--text);
  }
  .n {
    display: inline-grid;
    place-items: center;
    width: 1.4rem;
    height: 1.4rem;
    border-radius: 50%;
    background: var(--surface-2);
    font-size: 0.8rem;
  }
  .now .n,
  .done .n {
    background: var(--accent);
    color: var(--on-accent, #fff);
  }
  .body {
    display: flex;
    flex-direction: column;
    gap: 1rem;
    min-height: 11rem;
  }
  .pair {
    display: flex;
    align-items: flex-end;
    gap: 1rem;
    flex-wrap: wrap;
  }
  .plus {
    font-size: 1.4rem;
    color: var(--text-faint);
    padding-bottom: 0.35rem;
  }
  .field {
    display: flex;
    flex-direction: column;
    gap: 0.35rem;
  }
  .label {
    font-weight: 650;
  }
  .lab {
    display: flex;
    justify-content: space-between;
    gap: 0.6rem;
    font-weight: 600;
    max-width: 28rem;
  }
  .field input.num {
    max-width: 28rem;
  }
  .curves {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(13rem, 1fr));
    gap: 0.6rem;
  }
  .curve {
    display: flex;
    flex-direction: column;
    align-items: flex-start;
    gap: 0.35rem;
    padding: 0.8rem 0.9rem;
    border: 1px solid var(--line);
    border-radius: var(--radius-l);
    background: var(--surface-2);
    color: var(--text);
    font: inherit;
    text-align: left;
    cursor: pointer;
  }
  .curve.on {
    border-color: var(--accent);
    box-shadow: 0 0 0 1px var(--accent) inset;
    background: color-mix(in srgb, var(--accent) 10%, var(--surface-2));
  }
  .curve:disabled {
    opacity: 0.6;
    cursor: default;
  }
  .chips {
    display: flex;
    gap: 0.35rem;
    flex-wrap: wrap;
  }
  .chip {
    padding: 0.3rem 0.75rem;
    border: 1px solid var(--line);
    border-radius: 999px;
    background: var(--surface-2);
    color: var(--text);
    font: inherit;
    font-size: 0.88rem;
    cursor: pointer;
  }
  .chip[aria-checked='true'] {
    border-color: var(--accent);
    box-shadow: 0 0 0 1px var(--accent) inset;
  }
  .chip:disabled {
    opacity: 0.45;
    cursor: default;
  }
  .range-row {
    display: flex;
    gap: 0.8rem;
    align-items: center;
    max-width: 28rem;
  }
  .range-row input[type='range'] {
    flex: 1;
  }
  .amp {
    width: 6rem;
  }
  .facts {
    display: grid;
    grid-template-columns: 9rem 1fr;
    gap: 0.4rem 1rem;
    margin: 0;
    max-width: 30rem;
  }
  .facts dt {
    color: var(--text-muted);
  }
  .facts dd {
    margin: 0;
  }
  .actions {
    display: flex;
    gap: 0.8rem;
    align-items: center;
    padding-top: 0.8rem;
    border-top: 1px solid var(--line);
  }
  .grow {
    flex: 1;
  }
  .small {
    font-size: 0.85rem;
  }
  .warn {
    color: var(--warning);
    margin: 0;
  }
  p {
    margin: 0;
  }
</style>
