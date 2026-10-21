<script lang="ts">
  // The history of the wallet (TransactionView, TransactionTableModel, TransactionFilterProxy): filters, a long sortable
  // table, details, fee bumps, abandoning, copying, the CSV export. Over every chain the top bar's selector shows
  // (doc/unified-app.md §21.4, user 2026-10-21: "a standard view, with the transactions of all coins selected and a
  // column to know which chain it is from, the same search and features"): this app's own records and each sidechain's
  // (lib/alltxs.svelte.ts: read from its node, kept up to date by its latest rows), one table with a Chain column when
  // more than one chain is shown; search, filters, sorting, totals and the export over all of them; a row's details in
  // the app; what only this app's wallet can do (fee bumps, abandoning, raw transactions, its address book) on its rows.
  import { t } from '../lib/i18n.svelte';
  import { simpleMode } from '../lib/uimode.svelte';
  import { thirdPartyLinks } from '../lib/tools';
  import Amount from '../components/Amount.svelte';
  import ChainBadge from '../components/ChainBadge.svelte';
  import ContextMenu, { type ContextItem } from '../components/ContextMenu.svelte';
  import DataTable from '../components/DataTable.svelte';
  import Dialog from '../components/Dialog.svelte';
  import EditAddressDialog, { type EditMode } from '../components/EditAddressDialog.svelte';
  import Icon from '../components/Icon.svelte';
  import SafeText from '../components/SafeText.svelte';
  import TxDetailsDialog from '../components/TxDetailsDialog.svelte';
  import TxDetailsView from '../components/TxDetailsView.svelte';
  import TxStatusIcon from '../components/TxStatusIcon.svelte';
  import { errorText, inform } from '../lib/dialogs.svelte';
  import { FILTERS, saveTextAs } from '../lib/files';
  import { formatTime } from '../lib/format';
  import { go, nav } from '../lib/nav.svelte';
  import { host } from '../lib/rpc';
  import { prefs, savePrefs, toast, wallets } from '../lib/state.svelte';
  import type { Column } from '../lib/table';
  import { abandon, bumpFee } from '../lib/txactions';
  import { isDropped, makeDepositAgain } from '../lib/deposits.svelte';
  import { formatAmount, parseAmount } from '../lib/units';
  import {
    addressText,
    amountText,
    DATE_FILTERS,
    dateRange,
    filterRecords,
    plainText,
    sortNewestFirst,
    statusText,
    transactionsCsv,
    TYPE_FILTERS,
    typeText,
    walletApi,
    type DateFilter,
    type TxDetails,
    type TxRecord,
    type TypeFilter,
  } from '../lib/wallet';
  import { current, refreshAddresses, refreshRecords } from '../lib/walletdata.svelte';
  import { node } from '../lib/state.svelte';
  import { detail, showDetail } from '../lib/detail.svelte';
  import { ALL, chainEntries, chainHost, chainWallet, HOST, selectedChain, setSelectedChain, slotLabel } from '../lib/chains.svelte';
  import { openModule } from '../lib/modules.svelte';
  import { chainOf, followSides, sideTxs } from '../lib/alltxs.svelte';
  import { untrack } from 'svelte';

  const wallet = $derived(wallets.current);

  // --- The chains shown: the top bar's selection ---
  const entries = $derived(chainEntries());
  const sel = $derived(selectedChain(entries));
  /** This app's own history shown (All, or this app's chain). */
  const withHost = $derived(sel === ALL || sel === HOST);
  /** The sidechains shown whose node runs, as one text: what follows them changes only when the set does. */
  const sidesKey = $derived(entries.filter((e) => !e.host && e.running && (sel === ALL || sel === e.id)).map((e) => e.id).join(','));
  const sides = $derived(sidesKey ? sidesKey.split(',') : []);
  /** A sidechain shown whose node does not run (said above the table). */
  const sidesDown = $derived(entries.filter((e) => !e.host && !e.running && (sel === ALL || sel === e.id)));
  /** More than one chain in the table: a Chain column, and in the export. */
  const several = $derived((withHost ? 1 : 0) + sides.length > 1);
  const nameOf = (id: string) => entries.find((e) => e.id === id)?.name ?? id;
  const rowChain = (r: TxRecord) => chainOf(r, HOST);

  // Each sidechain shown: read once, then its latest rows every few seconds while the page shows it (not at a block of
  // this app's chain: its history is its own).
  $effect(() => {
    const list = sidesKey ? sidesKey.split(',') : [];
    return followSides(list);
  });

  let dateFilter: DateFilter = $state('all');
  let rangeFrom = $state('');
  let rangeTo = $state('');
  let typeFilter: TypeFilter = $state('all');
  let search = $state('');
  let minText = $state('');
  let selected: string | null = $state(null);
  let sortKey: string | null = $state('date');
  let sortDir: 'asc' | 'desc' = $state('desc');
  let details: Pick<TxRecord, 'txid' | 'idx' | 'address'> | null = $state(null);
  /** A sidechain's row whose details are shown (read from its node by the host). */
  let sideDetails: TxRecord | null = $state(null);
  let sideOpen = $state(false);
  let edit: { mode: EditMode; address: string; label: string } | null = $state(null);
  let menu = $state({ open: false, x: 0, y: 0 });
  let actions = $state({ txid: '', canAbandon: false, canBump: false, hex: '' });

  // This app's own: again on new transactions, new blocks, a new label.
  $effect(() => {
    void wallets.txVersion;
    void node.tip?.hash;
    void wallets.addressVersion;
    if (wallet !== null && withHost) refreshRecords();
  });
  $effect(() => {
    void wallets.addressVersion;
    if (wallet !== null && withHost) refreshAddresses();
  });

  /** Every row of the chains shown: this app's records as they are, each sidechain's after them (no copy of a record). */
  const all = $derived.by((): TxRecord[] => {
    const lists = sideTxs.lists;
    const side: TxRecord[] = [];
    for (const s of sides) for (const r of lists[s] ?? []) side.push(r);
    if (!withHost) return side;
    return side.length ? current.records.concat(side) : current.records;
  });
  const loaded = $derived((!withHost || current.recordsLoaded) && sides.every((s) => sideTxs.loaded[s]));

  const minAmount = $derived.by(() => {
    if (!minText.trim()) return 0n;
    const p = parseAmount(minText, prefs.unit);
    return p.ok ? p.sats : 0n;
  });
  const minInvalid = $derived(minText.trim() !== '' && !parseAmount(minText, prefs.unit).ok);
  const range = $derived(dateRange(dateFilter, new Date(), rangeFrom ? new Date(`${rangeFrom}T00:00`) : undefined, rangeTo ? new Date(`${rangeTo}T00:00`) : undefined));
  // The search waits for a pause in the typing when the history is long (TransactionView waits too).
  let searchApplied = $state('');
  $effect(() => {
    const text = search;
    if (untrack(() => all.length) < 2000) {
      searchApplied = text;
      return;
    }
    const t = setTimeout(() => (searchApplied = text), 250);
    return () => clearTimeout(t);
  });
  const shown = $derived(filterRecords(all, { ...range, type: typeFilter, search: searchApplied, minAmount, showInactive: true }));
  const chosen = $derived(shown.find((r) => r.key === selected) ?? all.find((r) => r.key === selected) ?? null);
  const chosenSide = $derived(chosen !== null && rowChain(chosen) !== HOST);

  // Totals of what the filters show (an addition to chains-qt).
  const totals = $derived.by(() => {
    let received = 0n;
    let sent = 0n;
    for (const r of shown) {
      if (r.amount > 0n) received += r.amount;
      else sent += r.amount;
    }
    return { received, sent, count: shown.length };
  });

  // After a send, or from the overview: that transaction selected.
  $effect(() => {
    const txid = nav.highlightTx;
    if (!txid || !loaded) return;
    const r = all.find((x) => x.txid === txid);
    if (r) {
      selected = r.key;
      nav.highlightTx = null;
    }
  });

  /** A row's details: this app's in its dialog (or the detail panel of the workspace), a sidechain's read from its node. */
  function open(r: TxRecord) {
    if (rowChain(r) !== HOST) {
      sideDetails = r;
      sideOpen = true;
    } else if (detail.follow) inPanel(r);
    else details = r;
  }
  // The workspace layout shows the selected transaction of this app's wallet in its detail panel.
  function inPanel(r: Pick<TxRecord, 'txid' | 'idx' | 'address'>) {
    if (wallet !== null) showDetail({ kind: 'tx', wallet, txid: r.txid, idx: r.idx, address: r.address });
  }
  const chosenKey = $derived(chosen && !chosenSide ? chosen.key : null);
  $effect(() => {
    if (chosenKey !== null && detail.follow) untrack(() => chosen && inPanel(chosen));
  });
  const sideLoad = (r: TxRecord) => () => chainHost.transaction(rowChain(r), r.txid, chainWallet(rowChain(r)), r.idx, r.address) as Promise<TxDetails>;

  // A narrow window: the type goes in front of the label, and the columns are tighter, so that the
  // amount stays in view.
  let pageWidth = $state(1000);
  let pageEl: HTMLDivElement | undefined = $state();
  $effect(() => {
    if (!pageEl || typeof ResizeObserver === 'undefined') return;
    const o = new ResizeObserver(() => pageEl && (pageWidth = pageEl.clientWidth));
    o.observe(pageEl);
    pageWidth = pageEl.clientWidth;
    return () => o.disconnect();
  });
  const narrow = $derived(pageWidth < 760);
  const simple = $derived(simpleMode());

  /** What a row is: the app's words of chains-qt; a payment of this app's wallet on the drivechain said as what it does. */
  function typeLabel(r: TxRecord): string {
    if (r.drivechain?.kind === 'deposit') {
      const side = slotLabel(r.drivechain.slot);
      return side ? t('Deposit into %1', side) : t('Deposit into a sidechain');
    }
    if (r.drivechain?.kind === 'bmm') return t('Merged mining bid');
    return typeText(r.type) || t('Other');
  }

  const chainColumn = $derived<Column<TxRecord>[]>(several ? [{ key: 'chain', label: 'Chain', value: (r) => nameOf(rowChain(r)), width: narrow ? '2.6rem' : '8.6rem' }] : []);
  const columns: Column<TxRecord>[] = $derived(
    narrow
      ? [
          { key: 'status', label: '', value: (r) => r.sortKey, width: '2.2rem', align: 'center', noFilter: true },
          { key: 'date', label: 'Date', value: (r) => r.time, width: '8.6rem' },
          ...chainColumn,
          { key: 'label', label: 'Label', value: (r) => addressText(r, true), width: 'minmax(0, 1fr)' },
          { key: 'amount', label: `Amount (${prefs.unit})`, value: (r) => r.amount, width: '9.5rem', align: 'end' },
        ]
      : [
          // Simple mode: the status in plain words beside its ring (waiting for the next block, confirmed, final).
          simple
            ? { key: 'status', label: 'Status', value: (r) => r.sortKey, width: '15.5rem', noFilter: true }
            : { key: 'status', label: '', value: (r) => r.sortKey, width: '2.6rem', align: 'center', noFilter: true },
          // With a Chain column, the others a little tighter: the amount stays in view in a window of 1180 px, the
          // words of a type ("Merged mining bid", "Deposit into BitAssets") whole.
          { key: 'date', label: 'Date', value: (r) => r.time, width: several ? '10.2rem' : '11.5rem' },
          ...chainColumn,
          { key: 'type', label: 'Type', value: (r) => typeLabel(r), width: several ? '12.5rem' : '11rem' },
          { key: 'label', label: 'Label', value: (r) => addressText(r, true), width: several ? 'minmax(8rem, 1fr)' : 'minmax(14rem, 1fr)' },
          { key: 'amount', label: `Amount (${prefs.unit})`, value: (r) => r.amount, width: several ? '11rem' : '12rem', align: 'end' },
        ],
  );

  async function copy(text: string, what: string) {
    try {
      await host.copy(text);
      toast('success', t('Copied'), what);
    } catch (e) {
      toast('error', t('Could not copy'), errorText(e));
    }
  }

  async function openMenu(r: TxRecord, e: MouseEvent) {
    selected = r.key;
    menu = { open: false, x: e.clientX, y: e.clientY };
    actions = { txid: r.txid, canAbandon: false, canBump: false, hex: '' };
    // What only this app's wallet does (bump, abandon, the raw transaction): asked of it for its own rows.
    if (wallet !== null && rowChain(r) === HOST) {
      try {
        actions = { txid: r.txid, ...(await walletApi(wallet).txActions(r.txid)) };
      } catch {
        // Gone meanwhile.
      }
    }
    menu = { open: true, x: e.clientX, y: e.clientY };
  }

  function editLabel(r: TxRecord) {
    if (!r.address) return;
    // In the address book: edit it; otherwise add it as a sending address (TransactionView::editLabel).
    const entry = current.addresses.find((a) => a.address === r.address);
    if (entry) edit = { mode: entry.purpose === 'receive' ? 'edit-receive' : 'edit-send', address: entry.address, label: entry.label };
    else edit = { mode: 'new-send', address: r.address, label: '' };
  }

  /** The explorer at a transaction, on its chain (the explorer follows the selector: a sidechain's own explorer). */
  function inExplorer(r: TxRecord) {
    const c = rowChain(r);
    if (c !== HOST && sel !== c) setSelectedChain(c);
    go('explorer', `search:${r.txid}`);
  }

  const menuItems: ContextItem[] = $derived.by(() => {
    if (!chosen) return [];
    const c = chosen;
    const copies: ContextItem[] = [
      { label: 'Copy address', action: () => copy(c.address, 'Address'), disabled: !c.address },
      { label: 'Copy label', action: () => copy(c.label, 'Label'), disabled: !c.label },
      { label: 'Copy amount', action: () => copy(amountText(c, prefs.unit, false, false), 'Amount') },
      { label: 'Copy transaction ID', action: () => copy(c.txid, 'Transaction ID') },
    ];
    if (chosenSide) {
      const stem = rowChain(c);
      return [
        ...copies,
        { label: 'Copy full transaction details', action: () => copy(plainText(c, prefs.unit), 'Transaction details') },
        { label: 'Show transaction details', action: () => open(c), separator: true },
        { label: 'Show in block explorer', action: () => inExplorer(c), separator: true },
        { label: t('Open in %1', nameOf(stem)), action: () => openModule(stem, 'transactions') },
      ];
    }
    return [
      ...copies,
      { label: 'Copy raw transaction', action: () => copy(actions.hex, 'Raw transaction'), disabled: !actions.hex },
      { label: 'Copy full transaction details', action: () => copy(plainText(c, prefs.unit), 'Transaction details') },
      { label: 'Show transaction details', action: () => (details = c), separator: true },
      { label: 'Increase transaction fee', action: () => wallet !== null && bump(c.txid), disabled: !actions.canBump, separator: true },
      { label: 'Abandon transaction', action: () => wallet !== null && abandon(wallet, c.txid), disabled: !actions.canAbandon, danger: true },
      // A deposit to a sidechain the mempool dropped: made again in its place, confirmed on the desktop.
      ...(isDropped(c) ? [{ label: 'Make the deposit again', action: () => wallet !== null && makeDepositAgain(wallet, c.txid).catch((e) => inform(t('Sidechains'), errorText(e), 'error')) }] : []),
      { label: 'Edit address label', action: () => editLabel(c), disabled: !c.address, separator: true },
      { label: 'Show in block explorer', action: () => inExplorer(c), separator: true },
      ...thirdPartyLinks(prefs.thirdPartyTxUrls, c.txid).map((l) => ({ label: l.label, action: () => host.open('url', l.url).catch(() => undefined) })),
    ];
  });

  async function bump(txid: string) {
    if (wallet === null) return;
    const newTxid = await bumpFee(wallet, txid);
    if (newTxid) nav.highlightTx = newTxid;
  }

  /** The export: the rows as shown (filtered, newest first), with their chain when they are not all this app's. */
  export function csvText(): string {
    return transactionsCsv(sortNewestFirst(shown), prefs.unit, several || !withHost ? (r) => nameOf(rowChain(r)) : undefined, typeLabel);
  }

  async function exportCsv() {
    try {
      const path = await saveTextAs({ title: 'Export Transaction History', name: 'transactions.csv', filters: [FILTERS.csv] }, csvText());
      if (path) toast('success', t('Exporting Successful'), t('The transaction history was successfully saved to %1.', path));
    } catch (e) {
      await inform(t('Exporting Failed'), t('There was an error trying to save the transaction history. %1', errorText(e)), 'error');
    }
  }

  function rowClass(r: TxRecord): string {
    if (r.status === 'abandoned' || r.status === 'dropped') return 'danger';
    if (!r.countsForBalance && r.status !== 'immature') return 'unconfirmed';
    return '';
  }
</script>

<div class="page transactions" bind:this={pageEl} data-tx-chains={[...(withHost ? [HOST] : []), ...sides].join(',')}>
  <header class="page-head">
    <h1>{t('Transactions')}</h1>
    <div class="row">
      <span class="muted count" data-tx-count>{t('%1 of %2', totals.count.toLocaleString('en'), all.length.toLocaleString('en'))}</span>
      <button type="button" class="btn small" onclick={exportCsv} disabled={prefs.mask} title={t('Export the data in the current tab to a file')}><Icon name="download" size={16} /> {t('Export')}</button>
    </div>
  </header>

  {#if prefs.mask}
    <div class="panel masked">
      <Icon name="eyeoff" size={28} />
      <div>
        <p><strong>{t('The history is hidden while values are masked.')}</strong></p>
        <p class="muted">{t('As in chains-qt, the transactions tab is unavailable in privacy mode.')}</p>
        <button type="button" class="btn" onclick={() => savePrefs({ mask: false })}>{t('Show values')}</button>
      </div>
    </div>
  {:else}
    <div class="filters" role="search">
      <select aria-label={t('Date')} bind:value={dateFilter}>
        {#each DATE_FILTERS as f (f.id)}<option value={f.id}>{t(f.label)}</option>{/each}
      </select>
      {#if dateFilter === 'range'}
        <label class="range">{t('From')} <input type="date" bind:value={rangeFrom} /></label>
        <label class="range">{t('to')} <input type="date" bind:value={rangeTo} /></label>
      {/if}
      <select aria-label={t('Type')} bind:value={typeFilter}>
        {#each TYPE_FILTERS as f (f.id)}<option value={f.id}>{t(f.label)}</option>{/each}
      </select>
      <input type="search" class="grow search" placeholder={several ? t('Address, transaction id, label or amount, on every chain shown') : t('Enter address, transaction id, or label to search')} aria-label={t('Search')} bind:value={search} />
      <input type="text" class="min num" placeholder={t('Min amount')} aria-label={t('Minimum amount')} inputmode="decimal" bind:value={minText} aria-invalid={minInvalid ? 'true' : undefined} />
    </div>

    <div class="totals">
      <span>{t('Received')} <Amount sats={totals.received} colored /></span>
      <span>{t('Sent')} <Amount sats={totals.sent} colored /></span>
      <span>{t('Net')} <Amount sats={totals.received + totals.sent} colored plus /></span>
    </div>

    {#each sides as s (s)}
      {#if sideTxs.errors[s]}<p class="error side-error" role="alert"><SafeText text={nameOf(s)} />: <SafeText text={sideTxs.errors[s]} /></p>{/if}
    {/each}
    {#each sidesDown as e (e.id)}
      <p class="muted side-error" data-tx-chain-down={e.id}>{t('%1 is not running: its transactions are not in the list.', e.name)}</p>
    {/each}
    {#if withHost && wallet === null}<p class="muted side-error">{t('No wallet is open: the transactions of this app\'s wallet are not in the list.')}</p>{/if}
    <!-- What this app's wallet could not give: said, the other chains' rows still listed. -->
    {#if withHost && current.recordsError && sides.length}<p class="error side-error" role="alert"><SafeText text={current.recordsError} /></p>{/if}

    <div class="holder">
      {#if withHost && current.recordsError && sides.length === 0}
        <p class="error"><SafeText text={current.recordsError} /></p>
      {:else}
        <DataTable
          rows={shown}
          {columns}
          key={(r) => r.key}
          bind:selected
          bind:sortKey
          bind:sortDir
          label="Transactions"
          fit={narrow}
          empty={loaded ? 'No transactions yet' : 'Loading…'}
          onactivate={(r) => open(r)}
          oncontext={(r, e) => openMenu(r, e)}
        >
          {#snippet cell(row, column)}
            <span class="cellwrap {rowClass(row)}" data-row-chain={rowChain(row)} data-txid={column.key === 'label' ? row.txid : undefined} title={column.key === 'label' ? `${statusText(row)}\n${typeLabel(row)} ${addressText(row, true)}` : undefined}>
              {#if column.key === 'status'}<TxStatusIcon record={row} />{#if simple && !narrow}<span class="status-word">{statusText(row)}</span>{/if}
              {:else if column.key === 'date'}{narrow ? formatTime(row.time, true) : formatTime(row.time)}
              {:else if column.key === 'chain'}<ChainBadge id={rowChain(row)} name={nameOf(rowChain(row))} small markOnly={narrow} />
              {:else if column.key === 'type'}<span class="type {row.type}"><Icon name={row.type === 'mined' ? 'mine' : row.amount < 0n ? 'send' : 'receive'} size={14} /> {typeLabel(row)}</span>
              {:else if column.key === 'label'}{#if narrow}<span class="type {row.type}" title={typeLabel(row)}><Icon name={row.type === 'mined' ? 'mine' : row.amount < 0n ? 'send' : 'receive'} size={14} /></span>{' '}{/if}<span class:bare={!row.label} translate="no">{addressText(row)}</span>
              {:else if column.key === 'amount'}
                {#if prefs.mask}<Amount sats={row.amount} showUnit={false} />
                {:else}<span class="amt" class:neg={row.amount < 0n} translate="no">{amountText(row, prefs.unit)}</span>{/if}
              {/if}
            </span>
          {/snippet}
        </DataTable>
      {/if}
    </div>
    <p class="hint">{t('Double-click a transaction for its details, right-click for more.')}</p>
  {/if}
</div>

{#if wallet !== null}
  <TxDetailsDialog {wallet} bind:record={details} />
  <EditAddressDialog {wallet} bind:edit />
{/if}
<ContextMenu items={menuItems} x={menu.x} y={menu.y} bind:open={menu.open} label="Transaction" />

<!-- A sidechain's transaction: its details read from its node by the host (the same view as this app's), in the app. -->
<Dialog bind:open={sideOpen} title={t('Transaction details')} wide onclose={() => (sideDetails = null)}>
  {#if sideDetails}
    {@const stem = rowChain(sideDetails)}
    <p class="on" data-tx-chain={stem}>{t('On')} <ChainBadge id={stem} name={nameOf(stem)} /></p>
    <TxDetailsView wallet="" record={{ txid: sideDetails.txid, idx: sideDetails.idx, address: sideDetails.address }} load={sideLoad(sideDetails)} />
  {/if}
  {#snippet actions()}
    {#if sideDetails}
      {@const stem = rowChain(sideDetails)}
      <button type="button" class="btn" onclick={() => { sideOpen = false; openModule(stem, 'transactions'); }}>{t('Open in %1', nameOf(stem))}</button>
    {/if}
    <button type="button" class="btn primary" onclick={() => (sideOpen = false)}>{t('Close')}</button>
  {/snippet}
</Dialog>

<style>
  .status-word {
    margin-left: 0.45rem;
    color: var(--text-muted);
    font-size: 0.9rem;
  }
  .transactions {
    display: flex;
    flex-direction: column;
    gap: 0.7rem;
    height: 100%;
  }
  .filters {
    display: flex;
    flex-wrap: wrap;
    gap: 0.5rem;
    align-items: center;
  }
  .filters select {
    width: auto;
  }
  .search {
    min-width: 16rem;
  }
  .min {
    width: 9rem;
    text-align: right;
  }
  .range {
    display: inline-flex;
    align-items: center;
    gap: 0.35rem;
    color: var(--text-muted);
  }
  .range input {
    width: auto;
  }
  .totals {
    display: flex;
    flex-wrap: wrap;
    gap: 1.4rem;
    color: var(--text-muted);
    font-size: 0.9rem;
  }
  .holder {
    flex: 1;
    min-height: 16rem;
  }
  .cellwrap {
    display: inline-flex;
    align-items: center;
    gap: 0.35rem;
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .cellwrap.unconfirmed {
    color: var(--text-muted);
  }
  .cellwrap.danger {
    color: var(--negative);
  }
  .bare {
    color: var(--text-muted);
  }
  .amt.neg {
    color: var(--negative);
  }
  .unconfirmed .amt.neg {
    color: inherit;
  }
  .type {
    display: inline-flex;
    align-items: center;
    gap: 0.3rem;
  }
  .type.mined {
    color: var(--warning);
  }
  .count {
    font-size: 0.9rem;
  }
  .hint {
    margin: 0;
    color: var(--text-faint);
    font-size: 0.85rem;
  }
  .masked {
    display: flex;
    gap: 1rem;
    align-items: flex-start;
    padding: 1.4rem;
    color: var(--accent-fg);
  }
  .masked div {
    color: var(--text);
  }
  .side-error {
    margin: 0;
    font-size: 0.88rem;
  }
  .on {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    margin: 0 0 0.6rem;
  }
</style>
