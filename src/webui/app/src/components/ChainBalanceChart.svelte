<script lang="ts">
  // The balance over time of each chain shown (doc/unified-app.md §19, Home): one line per chain, in its own colour and
  // mark, each in its own coin. Never added up: chains' coins are not the same coin. One chain: its line on its own
  // scale with its amounts at the right. Several: each line on its own scale (where it was lowest at the foot, highest
  // at the top), no shared axis of amounts (there is none), and the legend says what each holds now, in its own unit; the
  // crosshair says what each held then. Privacy mode: not drawn (the page says it is hidden).
  import ModuleAmount from './ModuleAmount.svelte';
  import ChainBadge from './ChainBadge.svelte';
  import { css } from '../lib/actions';
  import { t } from '../lib/i18n.svelte';
  import { formatTime } from '../lib/format';
  import { formatUnits } from '../lib/chains.svelte';
  import { prefs } from '../lib/state.svelte';
  import type { ChartSeries as Series } from '../lib/manychains.svelte';
  import { decimate } from '../lib/chartdata';

  let { series, height = 210, from = undefined }: { series: Series[]; height?: number; from?: number } = $props();

  let box: HTMLDivElement | undefined = $state();
  let width = $state(480);
  let hoverT: number | null = $state(null);

  $effect(() => {
    if (!box || typeof ResizeObserver === 'undefined') return;
    const o = new ResizeObserver(() => box && (width = box.clientWidth));
    o.observe(box);
    width = box.clientWidth || width;
    return () => o.disconnect();
  });

  const now = Math.floor(Date.now() / 1000);
  /** Each series since `from` (its balance then first), and up to now. */
  function clip(points: Series['points']): Series['points'] {
    if (!points.length) return [];
    let shown = points;
    if (from !== undefined && points[0].time < from) {
      let before = points[0];
      const after: Series['points'] = [];
      for (const p of points) {
        if (p.time < from) before = p;
        else after.push(p);
      }
      shown = [{ time: from, balance: before.balance }, ...after];
    }
    const last = shown[shown.length - 1];
    // Never more points than the chart has pixels (lib/chartdata.ts).
    const thin = decimate(shown, shown[0].time, Math.max(now, last.time), Math.max(60, width - 20));
    return last.time < now ? [...thin, { time: now, balance: last.balance }] : thin;
  }
  const drawn = $derived(series.map((s) => ({ ...s, data: clip(s.points) })).filter((s) => s.data.length >= 2));
  const single = $derived(drawn.length === 1);
  const PAD = $derived({ l: 6, r: single ? 64 : 12, t: 12, b: 24 });
  const t0 = $derived(drawn.length ? Math.min(...drawn.map((s) => s.data[0].time)) : 0);
  const t1 = $derived(drawn.length ? Math.max(t0 + 1, ...drawn.map((s) => s.data[s.data.length - 1].time)) : 1);
  /** Each series' own range: its lowest at the foot (zero if it never went below), its highest at the top. */
  const ranges = $derived(
    drawn.map((s) => {
      let lo = 0n;
      let hi = 1n;
      for (const p of s.data) {
        if (p.balance < lo) lo = p.balance;
        if (p.balance > hi) hi = p.balance;
      }
      return { lo, hi };
    }),
  );
  const x = (tm: number) => PAD.l + ((tm - t0) / (t1 - t0)) * (width - PAD.l - PAD.r);
  const y = (b: bigint, i: number) => {
    const { lo, hi } = ranges[i];
    return PAD.t + (1 - Number(b - lo) / Number(hi - lo || 1n)) * (height - PAD.t - PAD.b);
  };
  function path(s: (typeof drawn)[number], i: number): string {
    let d = `M${x(s.data[0].time)} ${y(s.data[0].balance, i)}`;
    for (let k = 1; k < s.data.length; k++) d += `H${x(s.data[k].time)}V${y(s.data[k].balance, i)}`;
    return d;
  }
  const at = (s: (typeof drawn)[number], tm: number) => {
    let b = s.data[0].balance;
    for (const p of s.data) {
      if (p.time > tm) break;
      b = p.balance;
    }
    return b;
  };
  const xTicks = $derived(drawn.length ? [0, 0.25, 0.5, 0.75, 1].map((f) => Math.round(t0 + f * (t1 - t0))) : []);
  function shortDate(tm: number): string {
    const span = t1 - t0;
    const d = new Date(tm * 1000);
    if (span < 2 * 86400) return `${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`;
    return d.toLocaleDateString('en', { month: 'short', day: 'numeric', ...(span > 300 * 86400 ? { year: '2-digit' } : {}) });
  }
  function shortAmount(v: bigint): string {
    const coins = Number(v) / 1e8;
    if (Math.abs(coins) >= 1e6) return `${(coins / 1e6).toFixed(1)}M`;
    if (Math.abs(coins) >= 1e3) return `${(coins / 1e3).toFixed(1)}k`;
    return Math.abs(coins) >= 10 ? coins.toFixed(0) : coins.toFixed(2);
  }
  function onmove(e: PointerEvent) {
    if (!box || !drawn.length) return;
    const r = box.getBoundingClientRect();
    hoverT = Math.min(t1, Math.max(t0, t0 + ((e.clientX - r.left - PAD.l) / (width - PAD.l - PAD.r)) * (t1 - t0)));
  }
  const label = $derived(t('Balance over time of each chain, each in its own coin: %1', drawn.map((s) => s.name).join(', ')));
</script>

<div class="chain-chart" data-chain-chart={drawn.map((s) => s.id).join(',')}>
  {#if series.length > 1}
    <ul class="legend" aria-label={t('Chains of the chart')}>
      {#each series as s (s.id)}
        {@const last = s.points[s.points.length - 1]}
        <li data-chart-series={s.id}>
          <span class="swatch" style:background={s.color} aria-hidden="true"></span>
          <ChainBadge id={s.id} name={s.name} small />
          <strong><ModuleAmount text={last ? formatUnits(last.balance, 8) : null} unit={s.unit} coins /></strong>
        </li>
      {/each}
    </ul>
  {/if}
  <div class="plot" bind:this={box} onpointermove={onmove} onpointerleave={() => (hoverT = null)} role="img" aria-label={label}>
    {#if prefs.mask}
      <p class="muted empty">{t('Hidden: privacy mode.')}</p>
    {:else if drawn.length === 0}
      <p class="muted empty">{t('The chart starts with the first transaction.')}</p>
    {:else}
      <svg {width} {height} viewBox="0 0 {width} {height}" aria-hidden="true">
        {#if single}
          {@const r = ranges[0]}
          {#each [r.lo, r.lo + (r.hi - r.lo) / 2n, r.hi] as v, i (i)}
            <line class="grid" x1={PAD.l} x2={width - PAD.r} y1={y(v, 0)} y2={y(v, 0)} />
            <text class="tick" x={width - PAD.r + 10} y={y(v, 0) + 4}>{shortAmount(v)}</text>
          {/each}
        {:else}
          {#each [0, 0.5, 1] as f (f)}
            <line class="grid" x1={PAD.l} x2={width - PAD.r} y1={PAD.t + f * (height - PAD.t - PAD.b)} y2={PAD.t + f * (height - PAD.t - PAD.b)} />
          {/each}
        {/if}
        {#each drawn as s, i (s.id)}
          {@const last = s.data[s.data.length - 1]}
          <path class="line" d={path(s, i)} style:stroke={s.color} />
          <circle class="now" cx={x(last.time)} cy={y(last.balance, i)} r="4" style:fill={s.color} />
        {/each}
        {#each xTicks as tm, i (i)}
          <text class="tick" class:end={i === xTicks.length - 1} class:mid={i > 0 && i < xTicks.length - 1} x={x(tm)} y={height - 6}>{i === xTicks.length - 1 ? t('now') : shortDate(tm)}</text>
        {/each}
        {#if hoverT !== null}
          <line class="cross" x1={x(hoverT)} x2={x(hoverT)} y1={PAD.t} y2={height - PAD.b} />
          {#each drawn as s, i (s.id)}
            <circle class="dot" cx={x(hoverT)} cy={y(at(s, hoverT), i)} r="4" style:fill={s.color} />
          {/each}
        {/if}
      </svg>
      {#if hoverT !== null}
        <div class="tip" use:css={{ left: `${Math.min(Math.max(x(hoverT), 90), width - 90)}px` }}>
          <span class="muted">{formatTime(Math.round(hoverT))}</span>
          {#each drawn as s (s.id)}
            <span class="tip-row"><span class="swatch" style:background={s.color} aria-hidden="true"></span><ModuleAmount text={formatUnits(at(s, hoverT), 8)} unit={s.unit} coins /></span>
          {/each}
        </div>
      {/if}
      {#if !single}<p class="faint small note">{t('Each line on its own scale, in its own coin: chains\' coins are never added up.')}</p>{/if}
    {/if}
  </div>
</div>

<style>
  .chain-chart {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
  }
  .legend {
    display: flex;
    flex-wrap: wrap;
    gap: 0.4rem 1.1rem;
    margin: 0;
    padding: 0;
    list-style: none;
    font-size: 0.88rem;
  }
  .legend li {
    display: inline-flex;
    align-items: center;
    gap: 0.4rem;
  }
  .swatch {
    width: 0.8rem;
    height: 0.2rem;
    border-radius: 2px;
  }
  .plot {
    position: relative;
    width: 100%;
    min-height: 4rem;
  }
  svg {
    display: block;
  }
  .grid {
    stroke: var(--line);
    stroke-width: 1;
    stroke-dasharray: 2 4;
  }
  .line {
    fill: none;
    stroke-width: 2;
    stroke-linejoin: round;
    stroke-linecap: round;
  }
  .now {
    stroke: var(--surface);
    stroke-width: 2;
  }
  .cross {
    stroke: var(--line-strong);
    stroke-width: 1;
    stroke-dasharray: 3 3;
  }
  .dot {
    stroke: var(--surface);
    stroke-width: 2;
  }
  .tick {
    fill: var(--text-faint);
    font-size: 11px;
  }
  .tick.end {
    text-anchor: end;
  }
  .tick.mid {
    text-anchor: middle;
  }
  .tip {
    position: absolute;
    top: -0.4rem;
    transform: translate(-50%, -100%);
    display: flex;
    flex-direction: column;
    gap: 0.15rem;
    padding: 0.35rem 0.6rem;
    border: 1px solid var(--line-strong);
    border-radius: var(--radius-s);
    background: var(--surface);
    box-shadow: var(--shadow);
    font-size: 0.85rem;
    white-space: nowrap;
    pointer-events: none;
  }
  .tip-row {
    display: inline-flex;
    align-items: center;
    gap: 0.4rem;
  }
  .empty {
    margin: 0;
    padding: 1rem 0;
  }
  .note {
    margin: 0.2rem 0 0;
  }
  .small {
    font-size: 0.8rem;
  }
</style>
