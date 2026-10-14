<script lang="ts">
  // Coin control: choose the coins a transaction spends (CoinControlDialog), by address or in a list.
  import Amount from './Amount.svelte';
  import ContextMenu, { type ContextItem } from './ContextMenu.svelte';
  import Dialog from './Dialog.svelte';
  import Icon from './Icon.svelte';
  import CoinSummary from './CoinSummary.svelte';
  import { formatTime } from '../lib/format';
  import { host } from '../lib/rpc';
  import { prefs, savePrefs, toast } from '../lib/state.svelte';
  import { formatAmount } from '../lib/units';
  import { walletApi, type Coin, type CoinGroup, type CoinSummary as Summary, type FeeChoice, type Outpoint } from '../lib/wallet';

  let {
    open = $bindable(false),
    wallet,
    selected = $bindable([]),
    amounts,
    subtractFee,
    fee,
  }: { open?: boolean; wallet: string; selected?: Outpoint[]; amounts: bigint[]; subtractFee: boolean; fee: FeeChoice } = $props();

  type SortKey = 'amount' | 'label' | 'address' | 'date' | 'depth';
  let groups: CoinGroup[] = $state([]);
  let loading = $state(false);
  let error = $state('');
  let chosen = $state(new Set<string>());
  let expanded = $state(new Set<string>());
  let summary: Summary | null = $state(null);
  let sortKey: SortKey = $state('amount');
  let sortAsc = $state(false);
  let menu = $state({ open: false, x: 0, y: 0, coin: null as Coin | null, group: null as CoinGroup | null });

  const keyOf = (o: Outpoint) => `${o.txid}:${o.vout}`;
  const api = $derived(walletApi(wallet));

  async function load() {
    loading = true;
    error = '';
    try {
      groups = await api.coins();
      const exists = new Set(groups.flatMap((g) => g.coins.filter((c) => !c.locked).map(keyOf)));
      // Locked coins, and coins spent meanwhile, are no longer selected.
      chosen = new Set(selected.map(keyOf).filter((k) => exists.has(k)));
      // Addresses with some coins chosen are open.
      expanded = new Set(groups.filter((g) => g.coins.some((c) => chosen.has(keyOf(c)))).map((g) => g.address));
    } catch (e) {
      error = e instanceof Error ? e.message : String(e);
    }
    loading = false;
  }

  $effect(() => {
    if (open) load();
  });

  function outpoints(): Outpoint[] {
    return [...chosen].map((k) => {
      const i = k.lastIndexOf(':');
      return { txid: k.slice(0, i), vout: Number(k.slice(i + 1)) };
    });
  }

  // The estimate of the transaction with the coins chosen.
  $effect(() => {
    const list = outpoints();
    const a = amounts;
    const s = subtractFee;
    const f = fee;
    if (!open) return;
    if (list.length === 0) {
      summary = null;
      return;
    }
    const t = setTimeout(() => {
      api
        .coinSummary({ selected: list }, a, s, f)
        .then((r) => (summary = r))
        .catch(() => (summary = null));
    }, 120);
    return () => clearTimeout(t);
  });

  function compare(a: Coin, b: Coin): number {
    switch (sortKey) {
      case 'amount':
        return a.sats < b.sats ? -1 : a.sats > b.sats ? 1 : 0;
      case 'label':
        return labelOf(a).localeCompare(labelOf(b));
      case 'address':
        return a.address.localeCompare(b.address);
      case 'date':
        return a.time - b.time;
      case 'depth':
        return a.depth - b.depth;
    }
  }
  const total = (g: CoinGroup) => g.coins.reduce((s, c) => s + c.sats, 0n);

  const sorted = $derived.by(() => {
    const factor = sortAsc ? 1 : -1;
    const gs = groups.map((g) => ({ ...g, coins: [...g.coins].sort((a, b) => compare(a, b) * factor) }));
    gs.sort((a, b) => {
      if (sortKey === 'amount') return (total(a) < total(b) ? -1 : total(a) > total(b) ? 1 : 0) * factor;
      if (sortKey === 'label') return (a.label || '').localeCompare(b.label || '') * factor;
      return a.address.localeCompare(b.address) * factor;
    });
    return gs;
  });
  const flat = $derived.by(() => {
    const factor = sortAsc ? 1 : -1;
    return groups.flatMap((g) => g.coins.map((coin) => ({ coin, group: g }))).sort((x, y) => compare(x.coin, y.coin) * factor);
  });
  const lockedCount = $derived(groups.reduce((n, g) => n + g.coins.filter((c) => c.locked).length, 0));

  function labelOf(c: Coin): string {
    return c.change ? '(change)' : c.label || '(no label)';
  }

  function sortBy(key: SortKey) {
    if (sortKey === key) sortAsc = !sortAsc;
    else {
      sortKey = key;
      sortAsc = key === 'label' || key === 'address';
    }
  }

  function toggle(c: Coin) {
    if (c.locked) return;
    const k = keyOf(c);
    const next = new Set(chosen);
    if (next.has(k)) next.delete(k);
    else next.add(k);
    chosen = next;
  }

  function groupState(g: CoinGroup): 'all' | 'some' | 'none' {
    const free = g.coins.filter((c) => !c.locked);
    const n = free.filter((c) => chosen.has(keyOf(c))).length;
    return n === 0 ? 'none' : n === free.length ? 'all' : 'some';
  }

  function toggleGroup(g: CoinGroup) {
    const next = new Set(chosen);
    const all = groupState(g) !== 'all';
    for (const c of g.coins) {
      if (c.locked) continue;
      if (all) next.add(keyOf(c));
      else next.delete(keyOf(c));
    }
    chosen = next;
  }

  /** (Un)select all: unselect if anything is chosen, else choose every free coin. */
  function toggleAll() {
    if (chosen.size > 0) chosen = new Set();
    else chosen = new Set(groups.flatMap((g) => g.coins.filter((c) => !c.locked).map(keyOf)));
  }

  function toggleOpen(g: CoinGroup) {
    const next = new Set(expanded);
    if (next.has(g.address)) next.delete(g.address);
    else next.add(g.address);
    expanded = next;
  }

  async function copy(text: string) {
    await host.copy(text).catch(() => undefined);
    toast('success', 'Copied', text.length > 80 ? undefined : text);
  }

  async function setLocked(c: Coin, lock: boolean) {
    try {
      await api.lockCoin(c, lock);
      if (lock) {
        const next = new Set(chosen);
        next.delete(keyOf(c));
        chosen = next;
      }
      await load();
    } catch (e) {
      toast('error', 'Coin control', e instanceof Error ? e.message : String(e));
    }
  }

  function showMenu(e: MouseEvent, coin: Coin | null, group: CoinGroup) {
    e.preventDefault();
    menu = { open: true, x: e.clientX, y: e.clientY, coin, group };
  }

  const menuItems: ContextItem[] = $derived.by(() => {
    const c = menu.coin;
    const g = menu.group;
    return [
      { label: 'Copy address', action: () => copy(c?.address || g?.address || '') },
      { label: 'Copy label', action: () => copy(c ? (c.change ? g?.label ?? '' : c.label) : g?.label ?? '') },
      { label: 'Copy amount', action: () => copy(formatAmount(c ? c.sats : g ? g.coins.reduce((s, x) => s + x.sats, 0n) : 0n, prefs.unit, { separators: false })) },
      { label: 'Copy transaction ID and output index', action: () => c && copy(keyOf(c)), disabled: !c, separator: true },
      { label: 'Lock unspent', action: () => c && setLocked(c, true), disabled: !c || c.locked, separator: true },
      { label: 'Unlock unspent', action: () => c && setLocked(c, false), disabled: !c || !c.locked },
    ];
  });

  function done() {
    selected = outpoints();
    open = false;
  }
</script>

{#snippet head(key: SortKey, label: string)}
  <th scope="col" aria-sort={sortKey === key ? (sortAsc ? 'ascending' : 'descending') : 'none'}>
    <button type="button" onclick={() => sortBy(key)}>{label}<span class="arrow" aria-hidden="true">{sortKey === key ? (sortAsc ? '▲' : '▼') : ''}</span></button>
  </th>
{/snippet}

{#snippet coinRow(c: Coin, g: CoinGroup, child: boolean)}
  <tr class:locked={c.locked} class:child oncontextmenu={(e) => showMenu(e, c, g)}>
    <td class="check-cell">
      {#if c.locked}
        <span title="Locked" class="lock"><Icon name="lock" size={15} label="Locked" /></span>
      {:else}
        <input type="checkbox" aria-label="Spend this coin" checked={chosen.has(keyOf(c))} onchange={() => toggle(c)} />
      {/if}
    </td>
    <td class="num end"><Amount sats={c.sats} showUnit={false} /></td>
    <td title={c.change ? `change from ${g.label || '(no label)'} (${g.address})` : undefined}>{child && !c.change ? '' : labelOf(c)}</td>
    <td class="mono addr">{child && !c.change ? '' : c.address}</td>
    <td>{formatTime(c.time)}</td>
    <td class="num end">{c.depth}</td>
  </tr>
{/snippet}

<Dialog bind:open title="Coin selection" wide>
  <div class="cc">
    <CoinSummary {summary} />
    <div class="row toolbar">
      <button type="button" class="btn small" onclick={toggleAll}>(un)select all</button>
      <div class="seg" role="radiogroup" aria-label="View">
        <label><input type="radio" name="cc-mode" checked={prefs.coinTree} onchange={() => savePrefs({ coinTree: true })} /> Tree mode</label>
        <label><input type="radio" name="cc-mode" checked={!prefs.coinTree} onchange={() => savePrefs({ coinTree: false })} /> List mode</label>
      </div>
      <span class="grow"></span>
      {#if lockedCount}<span class="muted">({lockedCount} locked)</span>{/if}
    </div>
    {#if error}<p class="error">{error}</p>{/if}
    <div class="scroller">
      <table class="coins">
        <thead>
          <tr>
            <th scope="col" class="check-cell"><span class="sr-only">Spend</span></th>
            {@render head('amount', `Amount (${prefs.unit})`)}
            {@render head('label', 'Received with label')}
            {@render head('address', 'Received with address')}
            {@render head('date', 'Date')}
            {@render head('depth', 'Confirmations')}
          </tr>
        </thead>
        <tbody>
          {#if loading && groups.length === 0}
            <tr><td colspan="6" class="muted center">Loading…</td></tr>
          {:else if groups.length === 0}
            <tr><td colspan="6" class="muted center">No coins to spend</td></tr>
          {:else if prefs.coinTree}
            {#each sorted as g (g.address)}
              {@const state = groupState(g)}
              <tr class="group" oncontextmenu={(e) => showMenu(e, null, g)}>
                <td class="check-cell">
                  <input type="checkbox" aria-label="Spend the coins of this address" checked={state === 'all'} indeterminate={state === 'some'} onchange={() => toggleGroup(g)} />
                </td>
                <td class="num end"><Amount sats={g.coins.reduce((s, c) => s + c.sats, 0n)} showUnit={false} /></td>
                <td>{g.label || '(no label)'}</td>
                <td class="mono addr">
                  <button type="button" class="expander" aria-expanded={expanded.has(g.address)} onclick={() => toggleOpen(g)}>
                    <span aria-hidden="true">{expanded.has(g.address) ? '▾' : '▸'}</span> {g.address} <span class="muted">({g.coins.length})</span>
                  </button>
                </td>
                <td></td>
                <td></td>
              </tr>
              {#if expanded.has(g.address)}
                {#each g.coins as c (keyOf(c))}{@render coinRow(c, g, true)}{/each}
              {/if}
            {/each}
          {:else}
            {#each flat as { coin, group } (keyOf(coin))}{@render coinRow(coin, group, false)}{/each}
          {/if}
        </tbody>
      </table>
    </div>
  </div>
  {#snippet actions()}
    <button type="button" class="btn primary" onclick={done}>OK</button>
  {/snippet}
</Dialog>

<ContextMenu items={menuItems} x={menu.x} y={menu.y} bind:open={menu.open} label="Coin" />

<style>
  .cc {
    display: flex;
    flex-direction: column;
    gap: 0.7rem;
  }
  .toolbar {
    flex-wrap: wrap;
  }
  .seg {
    display: flex;
    gap: 0.9rem;
  }
  .seg label {
    display: inline-flex;
    align-items: center;
    gap: 0.35rem;
    cursor: pointer;
  }
  .scroller {
    max-height: 22rem;
    overflow: auto;
    border: 1px solid var(--line);
    border-radius: var(--radius-m);
  }
  table {
    width: 100%;
    border-collapse: collapse;
    font-variant-numeric: tabular-nums;
  }
  th {
    position: sticky;
    top: 0;
    background: var(--surface-2);
    text-align: left;
    font-size: 0.85rem;
    color: var(--text-muted);
    padding: 0.4rem 0.6rem;
    white-space: nowrap;
  }
  th button {
    border: 0;
    background: none;
    color: inherit;
    font: inherit;
    font-weight: 600;
    cursor: pointer;
    padding: 0;
  }
  .arrow {
    font-size: 0.65rem;
    margin-left: 0.25rem;
  }
  td {
    padding: 0.32rem 0.5rem;
    border-top: 1px solid var(--line);
    white-space: nowrap;
  }
  .end {
    text-align: right;
  }
  .center {
    text-align: center;
  }
  .check-cell {
    width: 2.2rem;
  }
  tr.group {
    background: var(--surface-2);
    font-weight: 550;
  }
  tr.child td:nth-child(2) {
    padding-left: 1.4rem;
  }
  tr.locked {
    color: var(--text-faint);
  }
  .lock {
    color: var(--text-muted);
  }
  .addr {
    max-width: 17rem;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .expander {
    border: 0;
    background: none;
    color: inherit;
    font: inherit;
    cursor: pointer;
    padding: 0;
  }
</style>
