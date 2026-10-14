<script lang="ts">
  // Notices of new transactions of the wallets (BitcoinGUI::incomingTransaction): date, amount,
  // type, label or address. Many at once (a rescan) make one notice, as in chains-qt.
  import { prefs, toast, wallets } from '../lib/state.svelte';
  import { formatTime } from '../lib/format';
  import { formatAmount } from '../lib/units';
  import { addressText, typeText, walletDisplayName } from '../lib/wallet';
  import { current, refreshRecords } from '../lib/walletdata.svelte';

  const MAX_NOTICES = 3;
  let seen = 0;

  $effect(() => {
    const total = wallets.newTxs.length;
    if (total <= seen) {
      seen = total;
      return;
    }
    const t = setTimeout(async () => {
      const batch = wallets.newTxs.slice(seen);
      seen = wallets.newTxs.length;
      if (batch.length > MAX_NOTICES) {
        toast('info', `${batch.length} new transactions`, 'See the transactions page.');
        return;
      }
      for (const { wallet, txid } of batch) {
        if (wallet !== wallets.current) {
          toast('info', 'New transaction', `In wallet ${walletDisplayName(wallet)}`);
          continue;
        }
        await refreshRecords(true);
        for (const r of current.records.filter((x) => x.txid === txid)) {
          const incoming = r.amount >= 0n;
          const amount = prefs.mask ? '••••••' : formatAmount(r.amount, prefs.unit, { withUnit: true, plus: true });
          const lines = [`Date: ${formatTime(r.time)}`, `Amount: ${amount}`, `Type: ${typeText(r.type) || 'Other'}`];
          if (wallets.list.length > 1) lines.push(`Wallet: ${walletDisplayName(wallet)}`);
          if (r.label) lines.push(`Label: ${r.label}`);
          else if (r.address) lines.push(`Address: ${addressText(r)}`);
          toast(incoming ? 'success' : 'info', incoming ? 'Incoming transaction' : 'Sent transaction', lines.join('\n'));
        }
      }
    }, 600);
    return () => clearTimeout(t);
  });
</script>
