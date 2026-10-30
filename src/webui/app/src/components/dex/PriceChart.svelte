<script lang="ts">
  // A pool's price over the time it keeps (its time-weighted averages, chain.dex.chart): one line, still (no animation),
  // its lowest and highest price at the right, the time it covers below. A price is public: not hidden in privacy mode.
  import { t } from '../../lib/i18n.svelte';
  import SafeText from '../SafeText.svelte';

  let { points, unit, base }: { points: { t: number; price: number }[]; unit: string; base: string } = $props();

  const W = 600;
  const H = 180;
  const shape = $derived.by(() => {
    if (points.length < 2) return null;
    const ts = points.map((p) => p.t);
    const ps = points.map((p) => p.price);
    const t0 = Math.min(...ts);
    const t1 = Math.max(...ts);
    let lo = Math.min(...ps);
    let hi = Math.max(...ps);
    if (hi - lo < hi * 1e-9) {
      lo = lo * 0.98;
      hi = hi * 1.02;
    }
    const pad = (hi - lo) * 0.08;
    lo -= pad;
    hi += pad;
    const x = (v: number) => ((v - t0) / Math.max(1, t1 - t0)) * W;
    const y = (v: number) => H - ((v - lo) / (hi - lo)) * H;
    const line = points.map((p, i) => `${i ? 'L' : 'M'}${x(p.t).toFixed(1)},${y(p.price).toFixed(1)}`).join('');
    return { line, area: `${line}L${W},${H}L0,${H}Z`, lo: Math.min(...ps), hi: Math.max(...ps), t0, t1, last: ps[ps.length - 1], grid: [0.25, 0.5, 0.75].map((f) => f * H) };
  });
  /** The axis's two prices with as many digits as tell them apart. */
  function axis(n: number, lo: number, hi: number): string {
    const spread = hi > lo ? (hi - lo) / hi : 0;
    const digits = spread > 0 ? Math.min(12, Math.max(4, Math.ceil(-Math.log10(spread)) + 2)) : 6;
    return Number(n.toPrecision(digits)).toString();
  }
  const when = (s: number) => new Date(s * 1000).toLocaleString(undefined, { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit' });
</script>

{#if shape}
  <figure class="chart" data-price-chart={points.length}>
    <div class="plot">
      <svg viewBox="0 0 {W} {H}" preserveAspectRatio="none" role="img" aria-label={t('Price of %1 in %2 over time', base, unit)}>
        {#each shape.grid as g (g)}<line x1="0" x2={W} y1={g} y2={g} class="grid" />{/each}
        <path d={shape.area} class="area" />
        <path d={shape.line} class="line" vector-effect="non-scaling-stroke" />
      </svg>
      <div class="axis">
        <span class="num"><SafeText text={axis(shape.hi, shape.lo, shape.hi)} /></span>
        <span class="num"><SafeText text={axis(shape.lo, shape.lo, shape.hi)} /></span>
      </div>
    </div>
    <figcaption class="faint small">
      <span><SafeText text={when(shape.t0)} /></span>
      <span>{t('Averages over time of the pool, in %1 per %2', unit, base)}</span>
      <span><SafeText text={when(shape.t1)} /></span>
    </figcaption>
  </figure>
{:else}
  <p class="muted empty" data-price-chart="0">{t('No price history yet: the chart starts once a block has traded in this pool.')}</p>
{/if}

<style>
  .chart {
    margin: 0;
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
  }
  .plot {
    display: grid;
    grid-template-columns: minmax(0, 1fr) auto;
    gap: 0.5rem;
  }
  svg {
    width: 100%;
    height: 11rem;
    display: block;
  }
  .grid {
    stroke: var(--line);
    stroke-width: 1;
    vector-effect: non-scaling-stroke;
  }
  .line {
    fill: none;
    stroke: var(--accent);
    stroke-width: 2;
  }
  .area {
    fill: color-mix(in srgb, var(--accent) 12%, transparent);
  }
  .axis {
    display: flex;
    flex-direction: column;
    justify-content: space-between;
    color: var(--text-muted);
    font-size: 0.8rem;
    padding: 0.2rem 0;
  }
  figcaption {
    display: flex;
    justify-content: space-between;
    gap: 1rem;
    flex-wrap: wrap;
  }
  .small {
    font-size: 0.8rem;
  }
  .empty {
    margin: 0;
    padding: 2rem 0;
    text-align: center;
  }
</style>
