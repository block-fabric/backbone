<script lang="ts">
  // Proof of funds (ProofOfFundsDialog of chains-qt): a statement signed with every address of the
  // wallet that holds coins, and the check of such a proof against the chain as it is now.
  import CopyButton from '../components/CopyButton.svelte';
  import { BridgeError } from '../lib/bridge';
  import { errorText, inform, withWalletKeys } from '../lib/dialogs.svelte';
  import { WalletError } from '../lib/wallet';
  import { nav } from '../lib/nav.svelte';
  import { rpc } from '../lib/rpc';
  import { wallets } from '../lib/state.svelte';
  import { formatProof, parseProof, proofVerdict, simplified } from '../lib/tools';

  const tab = $derived(nav.tab === 'verify' ? 'verify' : 'prove');
  const today = new Date().toLocaleDateString(undefined, { year: 'numeric', month: 'long', day: 'numeric' });

  let statement = $state(`These funds are mine. ${today}`);
  let proof = $state('');
  let proving = $state(false);
  let input = $state('');
  let verdict = $state('');
  let verdictKind = $state<'valid' | 'partly' | 'invalid' | ''>('');
  let checking = $state(false);

  async function prove() {
    const text = simplified(statement);
    if (!text) {
      await inform('Proof of Funds', 'Enter the statement to sign.');
      return;
    }
    const wallet = wallets.current;
    if (wallet === null) {
      await inform('Proof of Funds', 'Open or create a wallet first.');
      return;
    }
    proving = true;
    try {
      // Whoever checks the proof looks the coins up on the chain: unconfirmed ones count here.
      const unspent = await rpc.raw<{ address?: string }[]>('listunspent', [0], { wallet });
      const addresses = [...new Set(unspent.map((c) => c.address).filter((a): a is string => !!a))].sort();
      let failed = 0;
      let lastError = '';
      const signed = await withWalletKeys(wallet, async () => {
        const out: { address: string; signature: string }[] = [];
        failed = 0;
        for (const address of addresses) {
          try {
            out.push({ address, signature: await rpc.raw<string>('signmessage', [address, text], { wallet }) });
          } catch (e) {
            // A locked wallet: the passphrase is asked for, and all of it signed again.
            if (BridgeError.from(e).code === WalletError.UNLOCK_NEEDED) throw e;
            failed++;
            lastError = errorText(e);
          }
        }
        return out;
      });
      if (signed === null) return;
      const signatures = signed;
      if (signatures.length === 0) {
        proof = '';
        await inform('Proof of Funds', addresses.length === 0 ? 'The wallet holds no coins on an address.' : `None of the addresses could sign: ${lastError}`, 'warning');
        return;
      }
      proof = formatProof(text, signatures);
      if (failed > 0) {
        await inform('Proof of Funds', `${failed} address(es) holding coins are left out of the proof, because they cannot sign a message (only addresses that stand for a single key can): ${lastError}`);
      }
    } catch (e) {
      await inform('Proof of Funds', errorText(e), 'warning');
    } finally {
      proving = false;
    }
  }

  async function verify() {
    const parsed = parseProof(input);
    if (!parsed) {
      verdict = 'This is not a proof of funds.';
      verdictKind = 'invalid';
      return;
    }
    checking = true;
    try {
      const valid = new Set<string>();
      let invalid = 0;
      let report = '';
      for (const entry of parsed.entries) {
        let ok = false;
        if ('address' in entry) {
          ok = await rpc.raw<boolean>('verifymessage', [entry.address, entry.signature, parsed.statement]).catch(() => false);
        }
        if (ok && 'address' in entry) valid.add(entry.address);
        else {
          invalid++;
          report += `INVALID signature for ${'address' in entry ? entry.address : entry.invalid}\n`;
        }
      }
      let total = '0';
      if (valid.size) {
        // Looking through all coins can take a while on a long chain.
        const scan = await rpc.raw<{ height: number; total_amount: number; unspents: unknown[] }>('scantxoutset', ['start', [...valid].map((a) => `addr(${a})`)]);
        total = scan.total_amount.toFixed(8);
        report += `Coins on these addresses now, at block ${scan.height}: ${total} in ${scan.unspents.length} output(s)\n`;
      }
      verdict = `${proofVerdict(valid.size, invalid, total)}\n\n    ${parsed.statement}\n\n${report}`;
      verdictKind = invalid === 0 && valid.size > 0 ? 'valid' : valid.size === 0 ? 'invalid' : 'partly';
    } catch (e) {
      verdict = errorText(e);
      verdictKind = 'invalid';
    } finally {
      checking = false;
    }
  }
</script>

<div class="page proof">
  <header class="page-head"><h1>Proof of funds</h1></header>
  <div class="tabs" role="tablist" aria-label="Prove or verify">
    <button type="button" role="tab" aria-selected={tab === 'prove'} onclick={() => (nav.tab = 'prove')}>Prove</button>
    <button type="button" role="tab" aria-selected={tab === 'verify'} onclick={() => (nav.tab = 'verify')}>Verify</button>
  </div>

  {#if tab === 'prove'}
    <section class="panel">
      <form class="panel-body form" onsubmit={(e) => { e.preventDefault(); prove(); }}>
        <p class="muted">
          Sign a statement with every address of the open wallet that holds coins. Whoever you give the proof to can check that the holder of those coins made the statement. It moves no coins and costs nothing,
          but it reveals which addresses are yours.
        </p>
        <div class="field">
          <label for="proof-statement">Statement</label>
          <div class="row">
            <input id="proof-statement" type="text" class="grow" bind:value={statement} />
            <button type="submit" class="btn primary" disabled={proving || wallets.current === null}>{proving ? 'Signing…' : 'Create proof'}</button>
          </div>
        </div>
        <div class="field">
          <label for="proof-output">Proof</label>
          <textarea id="proof-output" class="mono" rows="10" readonly value={proof}></textarea>
        </div>
        <div class="row"><CopyButton text={proof} label="Copy proof" small={false} /></div>
      </form>
    </section>
  {:else}
    <section class="panel">
      <form class="panel-body form" onsubmit={(e) => { e.preventDefault(); verify(); }}>
        <div class="field">
          <label for="proof-input">Paste a proof to check it against the chain as it is now:</label>
          <textarea id="proof-input" class="mono" rows="10" bind:value={input}></textarea>
        </div>
        <div class="row"><button type="submit" class="btn primary" disabled={checking}>{checking ? 'Checking…' : 'Check proof'}</button></div>
        {#if verdict}
          <pre id="proof-verdict" class="verdict {verdictKind}" aria-live="polite">{verdict}</pre>
        {/if}
      </form>
    </section>
  {/if}
</div>

<style>
  .form {
    display: flex;
    flex-direction: column;
    gap: 1rem;
    padding-top: 1.1rem;
  }
  .verdict {
    margin: 0;
    padding: 0.9rem 1rem;
    border-radius: var(--radius-s);
    background: var(--bg);
    border-left: 4px solid var(--line-strong);
    white-space: pre-wrap;
    overflow-wrap: anywhere;
    font-family: var(--font-ui);
  }
  .verdict.valid {
    border-left-color: var(--positive);
  }
  .verdict.partly {
    border-left-color: var(--warning);
  }
  .verdict.invalid {
    border-left-color: var(--negative);
  }
</style>
