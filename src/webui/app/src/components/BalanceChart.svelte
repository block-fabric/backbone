<script lang="ts">
  // The balance of the wallet over time, as a step line (an addition to the overview of chains-qt).
  // One series: no legend; a crosshair with the date and the balance under the pointer.
  import Amount from './Amount.svelte';
  import { css } from '../lib/actions';
  import { formatTime } from '../lib/format';
  import { prefs } from '../lib/state.svelte';
  import { formatAmount } from '../lib/units';

  let { points, height = 150 }: { points: { time: number; balance: bigint }[]; height?: number } = $props();

  let box: HTMLDivElement | undefined = $state();
  let width = $state(480);
  let hover: number | null = $state(null);

  $effect(() => {
    if (!box || typeof ResizeObserver === 'undefined') return;
    const o = new ResizeObserver(() => box && (width = box.clientWidth));
    o.observe(box);
    width = box.clientWidth || width;
    return () => o.disconnect();
  });

  const PAD = { l: 6, r: 6, t: 10, b: 20 };
  const now = Math.floor(Date.now() / 1000);
  const data = $derived(points.length ? [...points, { time: Math.max(now, points[points.length - 1].time), balance: points[points.length - 1].balance }] : []);
  const t0 = $derived(data.length ? data[0].time : 0);
  const t1 = $derived(data.length ? Math.max(data[data.length - 1].time, t0 + 1) : 1);
  const maxB = $derived(data.reduce((m, p) => (p.balance > m ? p.balance : m), 1n));
  const minB = $derived(data.reduce((m, p) => (p.balance < m ? p.balance : m), 0n));
  const x = (t: number) => PAD.l + ((t - t0) / (t1 - t0)) * (width - PAD.l - PAD.r);
  const y = (b: bigint) => PAD.t + (1 - Number(b - minB) / Number(maxB - minB || 1n)) * (height - PAD.t - PAD.b);

  const line = $derived.by(() => {
    if (!data.length) return '';
    let d = `M${x(data[0].time)} ${y(data[0].balance)}`;
    for (let i = 1; i < data.length; i++) d += `H${x(data[i].time)}V${y(data[i].balance)}`;
    return d;
  });
  const area = $derived(data.length ? `${line}V${y(minB)}H${x(data[0].time)}Z` : '');

  function onmove(e: PointerEvent) {
    if (!box || !data.length) return;
    const r = box.getBoundingClientRect();
    const t = t0 + ((e.clientX - r.left - PAD.l) / (width - PAD.l - PAD.r)) * (t1 - t0);
    let i = 0;
    while (i + 1 < data.length && data[i + 1].time <= t) i++;
    hover = i;
  }

  const hovered = $derived(hover !== null ? data[hover] : null);
  const label = $derived(data.length ? `Balance over time, from ${formatTime(t0)} to now: ${prefs.mask ? 'hidden' : formatAmount(data[data.length - 1].balance, prefs.unit, { withUnit: true })}` : 'No balance history');
</script>

<div class="chart" bind:this={box} onpointermove={onmove} onpointerleave={() => (hover = null)} role="img" aria-label={label}>
  {#if data.length < 2}
    <p class="muted empty">The chart starts with the first transaction.</p>
  {:else}
    <svg {width} {height} viewBox="0 0 {width} {height}" aria-hidden="true">
      <line class="base" x1={PAD.l} x2={width - PAD.r} y1={y(minB)} y2={y(minB)} />
      <path class="area" d={area} />
      <path class="line" d={line} />
      <text class="tick" x={PAD.l} y={height - 5}>{formatTime(t0).slice(0, 10)}</text>
      <text class="tick end" x={width - PAD.r} y={height - 5}>now</text>
      {#if hovered}
        <line class="cross" x1={x(hovered.time)} x2={x(hovered.time)} y1={PAD.t} y2={height - PAD.b} />
        <circle class="dot" cx={x(hovered.time)} cy={y(hovered.balance)} r="4" />
      {/if}
    </svg>
    {#if hovered}
      <div class="tip" use:css={{ left: `${Math.min(Math.max(x(hovered.time), 70), width - 70)}px` }}>
        <span class="muted">{formatTime(hovered.time)}</span>
        <strong><Amount sats={hovered.balance} /></strong>
      </div>
    {/if}
  {/if}
</div>

<style>
  .chart {
    position: relative;
    width: 100%;
    min-height: 4rem;
  }
  svg {
    display: block;
  }
  .base {
    stroke: var(--line);
    stroke-width: 1;
  }
  .area {
    fill: var(--accent-soft);
  }
  .line {
    fill: none;
    stroke: var(--accent);
    stroke-width: 2;
    stroke-linejoin: round;
  }
  .cross {
    stroke: var(--line-strong);
    stroke-width: 1;
    stroke-dasharray: 3 3;
  }
  .dot {
    fill: var(--accent);
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
  .tip {
    position: absolute;
    top: -0.4rem;
    transform: translate(-50%, -100%);
    display: flex;
    flex-direction: column;
    align-items: center;
    padding: 0.3rem 0.55rem;
    border: 1px solid var(--line-strong);
    border-radius: var(--radius-s);
    background: var(--surface);
    box-shadow: var(--shadow);
    font-size: 0.85rem;
    white-space: nowrap;
    pointer-events: none;
  }
  .empty {
    margin: 0;
    padding: 1rem 0;
  }
</style>
