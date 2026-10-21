<script lang="ts">
  // Limit orders and the order book (doc/unified-app.md §22, phase B of the DEX): the place of each part is set now,
  // nothing is wired. When a chain's node offers them (its `orders` feature), the book is read for the pair shown, the
  // form places an order as the chain's own question in the trusted dialog, and the wallet's orders can be cancelled.
  import { t } from '../../lib/i18n.svelte';
  import Icon from '../Icon.svelte';
  import SafeText from '../SafeText.svelte';

  let { name }: { name: string } = $props();
  let side = $state<'buy' | 'sell'>('buy');
</script>

<div class="orders" data-orders>
  <section class="panel coming" aria-label={t('Limit orders')}>
    <Icon name="clock" size={20} />
    <div>
      <strong>{t('Limit orders come with the order book of the DEX (phase B).')}</strong>
      <p class="muted"><SafeText text={t('Buy or sell at a price you set, filled when the market reaches it. The book of %1 and your open orders will be here; until then, swap at the pools\' price.', name)} /></p>
    </div>
  </section>

  <div class="grid">
    <section class="panel book" aria-label={t('Order book')}>
      <h2>{t('Order book')}</h2>
      <table class="table">
        <thead><tr><th class="num">{t('Price')}</th><th class="num">{t('Amount')}</th><th class="num">{t('Total')}</th></tr></thead>
        <tbody>
          {#each [0, 1, 2] as i (i)}<tr class="ghost-row ask"><td></td><td></td><td></td></tr>{/each}
          <tr class="mid"><td colspan="3"><span class="faint">{t('The pools\' price')}</span></td></tr>
          {#each [0, 1, 2] as i (i)}<tr class="ghost-row bid"><td></td><td></td><td></td></tr>{/each}
        </tbody>
      </table>
    </section>

    <section class="panel place" aria-label={t('Place a limit order')}>
      <h2>{t('Place a limit order')}</h2>
      <div class="sides" role="radiogroup" aria-label={t('Buy or sell')}>
        <button type="button" role="radio" aria-checked={side === 'buy'} onclick={() => (side = 'buy')}>{t('Buy')}</button>
        <button type="button" role="radio" aria-checked={side === 'sell'} onclick={() => (side = 'sell')}>{t('Sell')}</button>
      </div>
      <label class="field"><span>{t('Price')}</span><input type="text" disabled placeholder="0.00" /></label>
      <label class="field"><span>{t('Amount')}</span><input type="text" disabled placeholder="0.00" /></label>
      <label class="field"><span>{t('Good until')}</span><select disabled><option>{t('Cancelled')}</option></select></label>
      <button type="button" class="btn primary" disabled>{side === 'buy' ? t('Buy…') : t('Sell…')}</button>
    </section>
  </div>

  <section class="panel mine" aria-label={t('Your open orders')}>
    <h2>{t('Your open orders')}</h2>
    <table class="table">
      <thead><tr><th>{t('Pair')}</th><th>{t('Side')}</th><th class="num">{t('Price')}</th><th class="num">{t('Amount')}</th><th class="num">{t('Filled')}</th><th></th></tr></thead>
      <tbody><tr><td colspan="6" class="muted empty">{t('No open orders.')}</td></tr></tbody>
    </table>
  </section>
</div>

<style>
  .orders {
    display: flex;
    flex-direction: column;
    gap: 1.1rem;
  }
  .coming {
    display: flex;
    gap: 0.9rem;
    align-items: flex-start;
    padding: 1rem 1.3rem;
    border-left: 3px solid var(--accent);
  }
  .coming :global(svg) {
    color: var(--accent-strong);
    flex: none;
    margin-top: 0.15rem;
  }
  .coming p {
    margin: 0.3rem 0 0;
  }
  .grid {
    display: grid;
    grid-template-columns: minmax(0, 1fr) minmax(16rem, 22rem);
    gap: 1.1rem;
    align-items: start;
  }
  @media (max-width: 62rem) {
    .grid {
      grid-template-columns: minmax(0, 1fr);
    }
  }
  .book,
  .place,
  .mine {
    padding: 1.1rem 1.3rem;
    display: flex;
    flex-direction: column;
    gap: 0.7rem;
  }
  h2 {
    margin: 0;
    font-size: 1.2rem;
  }
  .table {
    width: 100%;
    border-collapse: collapse;
  }
  .table th,
  .table td {
    padding: 0.5rem 0.6rem;
    border-bottom: 1px solid var(--line);
    text-align: left;
  }
  .table th {
    color: var(--text-faint);
    font-size: 0.75rem;
    text-transform: uppercase;
    font-weight: 650;
  }
  .num {
    text-align: right !important;
  }
  .ghost-row td {
    height: 1.7rem;
  }
  .ghost-row.ask td:first-child {
    box-shadow: inset 3px 0 0 color-mix(in srgb, var(--negative) 35%, transparent);
  }
  .ghost-row.bid td:first-child {
    box-shadow: inset 3px 0 0 color-mix(in srgb, var(--positive) 35%, transparent);
  }
  .mid td {
    text-align: center;
    font-size: 0.85rem;
  }
  .sides {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 0.3rem;
    padding: 0.2rem;
    border: 1px solid var(--line);
    border-radius: 999px;
  }
  .sides button {
    padding: 0.35rem;
    border: 0;
    border-radius: 999px;
    background: none;
    color: var(--text-muted);
    font: inherit;
    font-weight: 650;
    cursor: pointer;
  }
  .sides button[aria-checked='true'] {
    background: var(--surface-2);
    color: var(--text);
  }
  .field {
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
    font-weight: 600;
  }
  .empty {
    padding: 1.2rem 0.6rem;
    text-align: center !important;
  }
</style>
