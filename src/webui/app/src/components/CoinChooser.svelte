<script lang="ts" module>
  /** A coin to choose (doc/unified-app.md §20.4): its outpoint, its amount as exact text in coins, where it is. */
  export interface ChoosableCoin {
    txid: string;
    vout: number;
    /** In coins, exact text ("1.50000000"). */
    amount: string;
    confirmations: number;
    address: string;
    label: string;
  }
  export const coinKey = (c: { txid: string; vout: number }) => `${c.txid}:${c.vout}`;
</script>

<script lang="ts">
  // "Choose coins" of Move coins (doc/unified-app.md §20.4, §20.7): the coins of the wallet the coins leave (this app's
  // wallet for a deposit, the sidechain's for a withdrawal), the largest first, to tick; only those pay, nothing is added
  // to them. Says what the chosen coins hold against what is moved. The same for every chain: the host checks again that
  // each is an unspent coin of that wallet, and the node too.
  import { t } from '../lib/i18n.svelte';
  import ModuleAmount from './ModuleAmount.svelte';
  import SafeText from './SafeText.svelte';
  import { formatCount, shortHash } from '../lib/format';
  import { formatUnits, parseUnits } from '../lib/chains.svelte';

  let {
    coins,
    selected = $bindable([]),
    need = null,
    unit = '',
    loading = false,
    error = '',
  }: { coins: ChoosableCoin[]; selected?: string[]; need?: string | null; unit?: string; loading?: boolean; error?: string } = $props();

  const chosen = $derived(new Set(selected));
  const chosenUnits = $derived(coins.reduce((n, c) => (chosen.has(coinKey(c)) ? n + (parseUnits(c.amount, 8) ?? 0n) : n), 0n));
  const needUnits = $derived(need ? parseUnits(need, 8) : null);
  const short = $derived(selected.length > 0 && needUnits !== null && chosenUnits < needUnits);

  // Coins spent meanwhile are no longer chosen.
  $effect(() => {
    const present = new Set(coins.map(coinKey));
    if (selected.some((k) => !present.has(k))) selected = selected.filter((k) => present.has(k));
  });

  function toggle(c: ChoosableCoin, on: boolean) {
    const k = coinKey(c);
    selected = on ? [...selected.filter((x) => x !== k), k] : selected.filter((x) => x !== k);
  }
</script>

<div class="chooser" data-coin-chooser>
  <div class="bar">
    <span class="muted small">
      {#if selected.length === 0}{t('No coin chosen: the wallet picks them.')}{:else}{selected.length === 1 ? t('1 coin chosen') : t('%1 coins chosen', String(selected.length))}<span class="dot-sep">{' · '}</span><ModuleAmount text={formatUnits(chosenUnits, 8)} {unit} coins />{/if}
    </span>
    <span class="grow"></span>
    <button type="button" class="btn ghost small" disabled={!coins.length} onclick={() => (selected = coins.map(coinKey))}>{t('All')}</button>
    <button type="button" class="btn ghost small" disabled={!selected.length} onclick={() => (selected = [])}>{t('None')}</button>
  </div>
  {#if short}<p class="warn small" role="status">{t('The coins chosen hold less than what you move, with its fee: choose more.')}</p>{/if}
  {#if error}<p class="error small"><SafeText text={error} /></p>{/if}
  <div class="list" role="group" aria-label={t('Coins of the wallet')}>
    <table class="rows">
      <tbody>
        {#each coins as c (coinKey(c))}
          {@const k = coinKey(c)}
          <tr class:on={chosen.has(k)}>
            <td class="pick"><input type="checkbox" checked={chosen.has(k)} aria-label={t('Use the coin of %1', c.amount)} onchange={(e) => toggle(c, (e.currentTarget as HTMLInputElement).checked)} data-coin={k} /></td>
            <td class="end amount"><ModuleAmount text={c.amount} coins /></td>
            <td class="where">
              {#if c.label}<span class="label"><SafeText text={c.label} /></span>{/if}
              <span class="mono faint" translate="no" title={c.address || k}>{shortHash(c.address || k, 7)}</span>
            </td>
            <td class="end faint conf">{c.confirmations === 0 ? t('waiting') : t('%1 conf.', formatCount(c.confirmations))}</td>
          </tr>
        {:else}
          <tr><td colspan="4" class="muted">{loading ? t('Loading…') : t('No coins in this wallet.')}</td></tr>
        {/each}
      </tbody>
    </table>
  </div>
</div>

<style>
  .chooser {
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
  }
  .bar {
    display: flex;
    align-items: center;
    gap: 0.4rem;
  }
  .grow {
    flex: 1;
  }
  .list {
    max-height: 14rem;
    overflow: auto;
    border-top: 1px solid var(--line);
  }
  .pick {
    width: 1.8rem;
  }
  .pick input {
    width: 1rem;
    height: 1rem;
  }
  .amount {
    font-weight: 600;
    white-space: nowrap;
  }
  .where {
    min-width: 0;
    max-width: 14rem;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .label {
    margin-right: 0.4rem;
  }
  .conf {
    white-space: nowrap;
    font-size: 0.8rem;
  }
  tr.on td {
    background: color-mix(in srgb, var(--accent) 8%, transparent);
  }
  .small {
    font-size: 0.82rem;
  }
  .warn {
    margin: 0;
    color: var(--warning);
  }
  .error {
    margin: 0;
  }
</style>
