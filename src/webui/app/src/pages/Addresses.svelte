<script lang="ts">
  // The address book (AddressBookPage): sending addresses to add, edit and delete; receiving
  // addresses whose label can be edited. Both searchable and exported as CSV.
  import ContextMenu, { type ContextItem } from '../components/ContextMenu.svelte';
  import DataTable from '../components/DataTable.svelte';
  import EditAddressDialog, { type EditMode } from '../components/EditAddressDialog.svelte';
  import Icon from '../components/Icon.svelte';
  import { confirm, errorText, inform } from '../lib/dialogs.svelte';
  import { FILTERS, saveTextAs } from '../lib/files';
  import { go, nav } from '../lib/nav.svelte';
  import { host } from '../lib/rpc';
  import { toast, wallets } from '../lib/state.svelte';
  import type { Column } from '../lib/table';
  import { addressesCsv, walletApi, walletDisplayName, type AddressEntry } from '../lib/wallet';
  import { current, refreshAddresses } from '../lib/walletdata.svelte';

  const wallet = $derived(wallets.current);
  const tab = $derived(nav.tab === 'receive' ? 'receive' : 'send');
  let filter = $state('');
  let selected: string | null = $state(null);
  let sortKey: string | null = $state('label');
  let sortDir: 'asc' | 'desc' = $state('asc');
  let edit: { mode: EditMode; address: string; label: string } | null = $state(null);
  let menu = $state({ open: false, x: 0, y: 0 });

  $effect(() => {
    void wallets.addressVersion;
    if (wallet !== null) refreshAddresses();
  });

  const rows = $derived(current.addresses.filter((a) => a.purpose === tab));
  const chosen = $derived(rows.find((r) => r.address === selected) ?? null);
  const columns: Column<AddressEntry>[] = [
    { key: 'label', label: 'Label', value: (r) => r.label || '(no label)', width: 'minmax(12rem, 1fr)' },
    { key: 'address', label: 'Address', value: (r) => r.address, width: 'minmax(22rem, 2fr)', mono: true },
  ];

  function setTab(t: 'send' | 'receive') {
    nav.tab = t;
    selected = null;
  }

  async function copy(text: string, what: string) {
    await host.copy(text).catch(() => undefined);
    toast('success', 'Copied', what);
  }

  function editChosen() {
    if (!chosen) return;
    edit = { mode: tab === 'send' ? 'edit-send' : 'edit-receive', address: chosen.address, label: chosen.label };
  }

  async function deleteChosen() {
    if (!chosen || wallet === null || tab !== 'send') return;
    const yes = await confirm('Delete address', `Delete ${chosen.label ? `"${chosen.label}" ` : ''}${chosen.address} from the list?`, { yes: 'Delete', danger: true });
    if (!yes) return;
    try {
      await walletApi(wallet).deleteAddress(chosen.address);
      selected = null;
      await refreshAddresses(true);
    } catch (e) {
      await inform('Delete address', errorText(e), 'error');
    }
  }

  async function exportCsv() {
    try {
      const path = await saveTextAs({ title: 'Export Address List', name: `${tab === 'send' ? 'sending' : 'receiving'}-addresses.csv`, filters: [FILTERS.csv] }, addressesCsv(rows));
      if (path) toast('success', 'Exported', path);
    } catch (e) {
      await inform('Exporting Failed', `There was an error trying to save the address list. Please try again. ${errorText(e)}`, 'error');
    }
  }

  const menuItems: ContextItem[] = $derived(
    chosen
      ? [
          { label: 'Copy Address', action: () => copy(chosen.address, 'Address') },
          { label: 'Copy Label', action: () => copy(chosen.label, 'Label') },
          { label: 'Edit', action: editChosen },
          ...(tab === 'send'
            ? [
                { label: 'Send to this address', action: () => {
                  nav.pendingUri = `chains:${chosen.address}${chosen.label ? `?label=${encodeURIComponent(chosen.label)}` : ''}`;
                  nav.uriSeq++;
                  go('send');
                }, separator: true },
                { label: 'Delete', action: deleteChosen, danger: true },
              ]
            : [{ label: 'Sign a message with this address', action: () => go('messages', `sign:${chosen.address}`), separator: true }]),
        ]
      : [],
  );
</script>

<div class="page addresses">
  <header class="page-head">
    <h1>{tab === 'send' ? 'Sending addresses' : 'Receiving addresses'}{wallet !== null && wallets.list.length > 1 ? ` - ${walletDisplayName(wallet)}` : ''}</h1>
  </header>
  <div class="tabs" role="tablist" aria-label="Addresses">
    <button type="button" role="tab" aria-selected={tab === 'send'} onclick={() => setTab('send')}>Sending</button>
    <button type="button" role="tab" aria-selected={tab === 'receive'} onclick={() => setTab('receive')}>Receiving</button>
  </div>
  <p class="muted explain">
    {#if tab === 'send'}
      These are your Chains addresses for sending payments. Always check the amount and the receiving address before sending coins.
    {:else}
      These are your Chains addresses for receiving payments. Use the 'Create new receiving address' button in the receive tab to create new addresses. Signing is only possible with addresses of the types 'legacy' and 'bech32' (P2WPKH).
    {/if}
  </p>
  <div class="row toolbar">
    <input type="search" class="grow" placeholder="Enter address or label to search" aria-label="Search" bind:value={filter} />
    {#if tab === 'send'}
      <button type="button" class="btn small" onclick={() => (edit = { mode: 'new-send', address: '', label: '' })} title="Create a new address"><Icon name="plus" size={15} /> New</button>
    {/if}
    <button type="button" class="btn small" disabled={!chosen} onclick={() => chosen && copy(chosen.address, 'Address')} title="Copy the currently selected address to the system clipboard"><Icon name="copy" size={15} /> Copy</button>
    <button type="button" class="btn small" disabled={!chosen} onclick={editChosen}><Icon name="edit" size={15} /> Edit</button>
    {#if tab === 'send'}
      <button type="button" class="btn small" disabled={!chosen} onclick={deleteChosen} title="Delete the currently selected address from the list"><Icon name="trash" size={15} /> Delete</button>
    {/if}
    <button type="button" class="btn small" onclick={exportCsv} title="Export the data in the current tab to a file"><Icon name="download" size={15} /> Export</button>
  </div>
  <div class="holder">
    <DataTable
      {rows}
      {columns}
      key={(r) => r.address}
      {filter}
      bind:selected
      bind:sortKey
      bind:sortDir
      label={tab === 'send' ? 'Sending addresses' : 'Receiving addresses'}
      empty={tab === 'send' ? 'No sending addresses yet' : 'No receiving addresses yet'}
      onactivate={() => editChosen()}
      oncontext={(r, e) => (menu = { open: true, x: e.clientX, y: e.clientY })}
    >
      {#snippet cell(row, column)}
        {#if column.key === 'label'}<span class:faint={!row.label}>{row.label || '(no label)'}</span>{:else}{row.address}{/if}
      {/snippet}
    </DataTable>
  </div>
</div>

{#if wallet !== null}<EditAddressDialog {wallet} bind:edit />{/if}
<ContextMenu items={menuItems} x={menu.x} y={menu.y} bind:open={menu.open} label="Address" />

<style>
  .addresses {
    display: flex;
    flex-direction: column;
    gap: 0.6rem;
    height: 100%;
  }
  .tabs {
    margin: 0.3rem 0 0;
  }
  .explain {
    margin: 0;
  }
  .toolbar {
    flex-wrap: wrap;
  }
  .toolbar input {
    min-width: 14rem;
  }
  .holder {
    flex: 1;
    min-height: 14rem;
  }
</style>
