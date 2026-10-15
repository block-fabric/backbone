<script lang="ts">
  // Notices of new transactions of the wallets (BitcoinGUI::incomingTransaction): date, amount,
  // type, label or address. Many at once (a rescan) make one notice, as in chains-qt.
  import { host } from '../lib/rpc';
  import { node, prefs, toast, wallets } from '../lib/state.svelte';
  import { formatTime } from '../lib/format';
  import { formatAmount } from '../lib/units';
  import { addressText, typeText, walletDisplayName } from '../lib/wallet';
  import { recordsOf } from '../lib/walletdata.svelte';

  const MAX_NOTICES = 3;
  let seen = 0;

  $effect(() => {
    const total = wallets.newTxTotal;
    if (total <= seen) {
      seen = total;
      return;
    }
    const t = setTimeout(async () => {
      const count = wallets.newTxTotal - seen;
      // The ones kept of them (the latest 100): a count larger than that makes one notice anyway.
      const batch = wallets.newTxs.slice(-Math.min(count, wallets.newTxs.length));
      seen = wallets.newTxTotal;
      // No notices of the desktop while the node catches up (WalletView::processNewTransaction).
      const desktop = !node.tip?.ibd;
      if (count > MAX_NOTICES) {
        toast('info', `${count} new transactions`, 'See the transactions page.');
        if (desktop) host.notify(`${count} new transactions`, 'See the transactions page.').catch(() => undefined);
        return;
      }
      // The records of the new transactions of the current wallet, asked for in one call.
      let fresh: Awaited<ReturnType<typeof recordsOf>> = [];
      try {
        fresh = await recordsOf([...new Set(batch.filter((b) => b.wallet === wallets.current).map((b) => b.txid))]);
      } catch {
        // The wallet went away meanwhile.
      }
      for (const { wallet, txid } of batch) {
        if (wallet !== wallets.current) {
          toast('info', 'New transaction', `In wallet ${walletDisplayName(wallet)}`);
          if (desktop) host.notify('New transaction', `In wallet ${walletDisplayName(wallet)}`).catch(() => undefined);
          continue;
        }
        for (const r of fresh.filter((x) => x.txid === txid)) {
          const incoming = r.amount >= 0n;
          const amount = prefs.mask ? '••••••' : formatAmount(r.amount, prefs.unit, { withUnit: true, plus: true });
          const lines = [`Date: ${formatTime(r.time)}`, `Amount: ${amount}`, `Type: ${typeText(r.type) || 'Other'}`];
          if (wallets.list.length > 1) lines.push(`Wallet: ${walletDisplayName(wallet)}`);
          if (r.label) lines.push(`Label: ${r.label}`);
          else if (r.address) lines.push(`Address: ${addressText(r)}`);
          toast(incoming ? 'success' : 'info', incoming ? 'Incoming transaction' : 'Sent transaction', lines.join('\n'));
          if (desktop) host.notify(incoming ? 'Incoming transaction' : 'Sent transaction', lines.join('\n')).catch(() => undefined);
        }
      }
    }, 600);
    return () => clearTimeout(t);
  });
</script>
