<script lang="ts">
  // Move coins (doc/unified-app.md §20.4): the one form that moves coins between this app's chain and a sidechain.
  // First the direction (into a sidechain, or back to this app's chain), then the sidechain (those this app runs, with
  // what you hold there; with Advanced, any other active sidechain, by its deposit address), the amount, where it goes
  // (your wallet on that chain: its deposit address from its node, never typed; or an address of this app's chain for a
  // withdrawal), the miners' fee of a withdrawal, and "Choose coins": only the coins ticked pay, from the wallet the coins
  // leave (this app's for a deposit, the sidechain's for a withdrawal). The spend is asked in the trusted dialog: this
  // app's own question for a deposit, the chain's own (named "On <chain>") for a withdrawal.
  import { t } from '../lib/i18n.svelte';
  import Icon from './Icon.svelte';
  import SafeText from './SafeText.svelte';
  import ChainBadge from './ChainBadge.svelte';
  import ModuleAmount from './ModuleAmount.svelte';
  import Amount from './Amount.svelte';
  import CoinChooser, { coinKey, type ChoosableCoin } from './CoinChooser.svelte';
  import { errorText, walletRpc } from '../lib/dialogs.svelte';
  import { ErrorCode } from '../lib/bridge';
  import { host } from '../lib/rpc';
  import { node, toast, wallets } from '../lib/state.svelte';
  import { formatUnits as fmtSats, ownWallet } from '../lib/chains.svelte';
  import { walletApi } from '../lib/wallet';
  import { productName, productUnit } from '../lib/modules.svelte';
  import { simpleMode } from '../lib/uimode.svelte';
  import { seed } from '../lib/seed.svelte';
  import { sidechainsToRun } from '../lib/sidechains.svelte';
  import {
    ALL,
    amountProblem,
    chainEntries,
    chainHost,
    chainLink,
    chainState,
    chainWallet,
    HOST,
    noteDeposit,
    refreshSummary,
    selectedChain,
    setSelectedChain,
    type Outpoint,
  } from '../lib/chains.svelte';
  import type { DrivechainState } from '../lib/chain';
  import { depositAgain, isDropped, makeDepositAgain } from '../lib/deposits.svelte';
  import { current } from '../lib/walletdata.svelte';
  import { shortHash } from '../lib/format';

  let { dc = null, onmoved = () => {} }: { dc?: DrivechainState | null; onmoved?: () => void } = $props();

  type Direction = 'in' | 'out';
  let direction = $state<Direction>('in');
  const simple = $derived(simpleMode());
  const product = $derived(productName());
  const unit = $derived(productUnit());

  // The sidechains: those this app runs (by their slot), then the other active ones of the drivechain (a deposit only,
  // to a typed deposit address; Advanced).
  // A drop-in moves its coins on its own Drivechains page (its wallet is its own): by its slot here, as another sidechain.
  const run = $derived(chainEntries().filter((e) => !e.host && !ownWallet(e)));
  const slotOf = (stem: string) => run.find((e) => e.id === stem)?.entry?.slot;
  const others = $derived((dc?.sidechains ?? []).filter((s) => !run.some((e) => e.entry?.slot === s.slot)));
  /** The choice: a chain this app runs (its id), or "slot:<n>" of another active sidechain. */
  let target = $state('');
  // In step with the top bar: a sidechain selected there is the one here; a link from a chain's card too.
  $effect(() => {
    const asked = chainLink.peg;
    if (asked) {
      chainLink.peg = null;
      if (run.some((e) => e.id === asked)) target = asked;
      return;
    }
    const sel = selectedChain();
    if (sel !== ALL && sel !== HOST && run.some((e) => e.id === sel)) target = sel;
    else if (!target || (!target.startsWith('slot:') && !run.some((e) => e.id === target))) target = run[0]?.id ?? (others[0] ? `slot:${others[0].slot}` : '');
  });
  const isRun = $derived(!!target && !target.startsWith('slot:'));
  const entry = $derived(isRun ? (run.find((e) => e.id === target) ?? null) : null);
  const otherSlot = $derived(target.startsWith('slot:') ? Number(target.slice(5)) : null);
  const otherTitle = $derived(otherSlot === null ? '' : (others.find((s) => s.slot === otherSlot)?.title ?? ''));
  // A withdrawal is made on a chain this app runs (its wallet signs it).
  $effect(() => {
    if (direction === 'out' && !isRun && run[0]) target = run[0].id;
  });
  function pick(value: string) {
    target = value;
    if (!value.startsWith('slot:')) setSelectedChain(value);
  }

  // What the side the coins leave holds.
  const own = $derived(wallets.current !== null ? wallets.balances[wallets.current] : undefined);
  const summary = $derived(entry ? chainState.summaries[entry.id] : undefined);
  $effect(() => {
    if (entry?.running) void refreshSummary(entry.id, 4000);
  });

  let amount = $state('');
  let destination = $state('');
  let address = $state('');
  let fee = $state('0.0001');
  let busy = $state(false);
  let error = $state('');
  let destCheck = $state<{ kind: 'plain' | 'valid' | 'invalid'; slot?: number } | null>(null);
  $effect(() => {
    const text = destination.trim();
    if (!text || isRun) {
      destCheck = null;
      return;
    }
    const timer = setTimeout(() => {
      host.depositAddress(text).then(
        (c) => (destCheck = c),
        () => (destCheck = null),
      );
    }, 250);
    return () => clearTimeout(timer);
  });

  // ---- A deposit the mempool dropped, made again (lib/deposits.svelte.ts): to the very destination it had, replacing it ----
  let replaces = $state<null | { txid: string; slot: number; destination: string }>(null);
  $effect(() => {
    const again = depositAgain.next;
    if (!again) return;
    depositAgain.next = null;
    replaces = { txid: again.replaces, slot: again.slot, destination: again.destination };
    direction = 'in';
    amount = again.amount === null ? '' : fmtSats(again.amount, 8);
  });
  const dropped = $derived(current.recordsLoaded ? current.records.filter((r) => isDropped(r) && r.amount < 0n) : []);
  async function again(txid: string) {
    if (wallets.current === null) return;
    try {
      await makeDepositAgain(wallets.current, txid);
    } catch (e) {
      error = errorText(e);
    }
  }

  // ---- Choose coins: the coins of the wallet the coins leave ----
  let choosing = $state(false);
  let selected = $state<string[]>([]);
  let coins = $state<ChoosableCoin[]>([]);
  let coinsLoading = $state(false);
  let coinsError = $state('');
  const coinSource = $derived(direction === 'in' ? `host:${wallets.current ?? ''}` : `${target}:${entry ? (chainWallet(entry.id) ?? '') : ''}`);
  $effect(() => {
    // Again on a new block and each transaction of the wallet, while shown.
    void node.tip?.height;
    void wallets.txVersion;
    const source = coinSource;
    if (!choosing) return;
    coinsLoading = true;
    coinsError = '';
    const load =
      direction === 'in'
        ? wallets.current === null
          ? Promise.resolve([] as ChoosableCoin[])
          : walletApi(wallets.current)
              .coins()
              .then((groups) =>
                groups
                  .flatMap((g) => g.coins)
                  .filter((c) => !c.locked)
                  .sort((a, b) => (b.sats > a.sats ? 1 : b.sats < a.sats ? -1 : 0))
                  .map((c) => ({ txid: c.txid, vout: c.vout, amount: fmtSats(c.sats, 8), confirmations: c.depth, address: c.address, label: c.change ? '' : c.label })),
              )
        : entry
          ? chainHost.coins(entry.id, chainWallet(entry.id)).then((r) => r.coins)
          : Promise.resolve([] as ChoosableCoin[]);
    load.then(
      (list) => {
        if (source === coinSource) coins = list;
        coinsLoading = false;
      },
      (e) => {
        coinsError = errorText(e);
        coinsLoading = false;
      },
    );
  });
  // Another side or direction: the coins chosen were of the other wallet.
  $effect(() => {
    void coinSource;
    selected = [];
  });
  const inputs = $derived<Outpoint[]>(
    selected.map((k) => {
      const i = k.lastIndexOf(':');
      return { txid: k.slice(0, i), vout: Number(k.slice(i + 1)) };
    }),
  );

  // ---- Checks, as you type ----
  const available = $derived(direction === 'in' ? null : (summary?.balances?.trusted ?? null));
  const amountError = $derived(amount.trim() ? amountProblem(amount, 8, available) : null);
  // The miners' fee may be zero; anything else an amount.
  const feeError = $derived.by(() => {
    if (direction !== 'out') return null;
    const p = amountProblem(fee, 8);
    return p === 'More than zero' ? null : p;
  });
  const destError = $derived.by(() => {
    if (direction !== 'in' || isRun || !destination.trim() || !destCheck) return '';
    if (destCheck.kind === 'invalid') return t('This deposit address has a typing error in it.');
    if (destCheck.kind === 'plain') return t('Not a deposit address: copy the one the sidechain\'s wallet gives (s<slot>_…).');
    if (destCheck.slot !== otherSlot) return t('This deposit address is for slot %1, not slot %2.', String(destCheck.slot), String(otherSlot));
    return '';
  });
  const ready = $derived(
    (replaces !== null && !!amount.trim() && !amountError && !busy && wallets.current !== null) ||
    !!target &&
      !!amount.trim() &&
      !amountError &&
      !busy &&
      (direction === 'in' ? wallets.current !== null && (isRun ? !!entry?.running : destCheck?.kind === 'valid' && destCheck.slot === otherSlot) : !!entry?.running && !!address.trim() && !feeError),
  );

  async function mineAddress() {
    if (wallets.current === null) return;
    try {
      address = await walletApi(wallets.current).newAddress(t('Back from %1', entry?.name ?? ''), '');
    } catch (e) {
      error = errorText(e);
    }
  }

  function said(e: unknown): string {
    const code = e && typeof e === 'object' && 'code' in e ? (e as { code: number }).code : 0;
    return code === ErrorCode.CANCELLED ? t('You declined: nothing was sent.') : errorText(e);
  }

  async function submit() {
    if (!ready) return;
    busy = true;
    error = '';
    const chosen = inputs.length ? inputs : undefined;
    try {
      if (replaces && wallets.current !== null) {
        await walletRpc<{ txid: string }>(wallets.current, 'createsidechaindeposit', [replaces.slot, replaces.destination, amount.trim(), replaces.txid]);
        toast('success', t('Deposit made again'), t('Only one of the two can ever be mined.'));
        replaces = null;
      } else if (direction === 'in' && entry) {
        const r = await chainHost.deposit(entry.id, amount.trim(), chainWallet(entry.id), chosen);
        if (r.txid) noteDeposit({ stem: entry.id, txid: r.txid, address: r.address, amount: amount.trim() });
        toast('success', t('Deposit into %1 sent', entry.name), t('It shows on %1 after the next blocks of both chains.', entry.name));
      } else if (direction === 'in' && otherSlot !== null && wallets.current !== null) {
        const params = chosen ? [otherSlot, destination.trim(), amount.trim(), null, chosen] : [otherSlot, destination.trim(), amount.trim()];
        await walletRpc<{ txid: string }>(wallets.current, 'createsidechaindeposit', params);
        toast('success', t('Deposit into %1 sent', otherTitle || t('slot %1', String(otherSlot))), t('It shows there after the next blocks of both chains.'));
        destination = '';
      } else if (direction === 'out' && entry) {
        await chainHost.withdraw(entry.id, address.trim(), amount.trim(), fee.trim(), chainWallet(entry.id), chosen);
        toast('success', t('Withdrawal from %1 made', entry.name), t('It is paid on %1 once the miners vote its bundle through: that takes a while.', product));
        void refreshSummary(entry.id, 0);
      }
      amount = '';
      selected = [];
      onmoved();
    } catch (e) {
      error = said(e);
    } finally {
      busy = false;
    }
  }
</script>

<section class="panel move" aria-labelledby="move-title" data-move-panel>
  <div class="panel-head">
    <h2 id="move-title">{t('Move coins')}</h2>
    {#if sidechainsToRun()}<button type="button" class="btn small ghost" onclick={() => (seed.open = 'move')} data-action="move-guide"><Icon name="book" size={14} /> {t('Step by step…')}</button>{/if}
  </div>
  <form class="panel-body form" onsubmit={(e) => { e.preventDefault(); void submit(); }}>
    {#if dropped.length}
      <div class="dropped" role="alert" aria-label={t('Dropped deposits')}>
        {#each dropped as r (r.key)}
          <p><Icon name="alert" size={15} /> <span class="grow">{t('A deposit was dropped: it was not sent.')} <Amount sats={-r.amount} /> <span class="mono faint" translate="no">{shortHash(r.txid, 6)}</span></span> <button type="button" class="btn small" onclick={() => again(r.txid)}>{t('Make it again')}</button></p>
        {/each}
      </div>
    {/if}
    {#if replaces}
      <div class="replacing" data-replacing>
        <p>{t('Makes again the dropped deposit %1, to the same destination: only one of the two can ever be mined.', shortHash(replaces.txid, 6))}</p>
        <p class="mono small" translate="no"><SafeText text={replaces.destination} /></p>
        <button type="button" class="link" onclick={() => (replaces = null)}>{t('Make a new deposit instead')}</button>
      </div>
    {:else}
    <div class="directions" role="radiogroup" aria-label={t('Direction')}>
      <button type="button" role="radio" aria-checked={direction === 'in'} class="dir" class:on={direction === 'in'} onclick={() => (direction = 'in')} data-direction="in">
        <Icon name="down" size={17} />
        <span><strong>{t('Into a sidechain')}</strong><small class="muted">{t('From your %1 wallet', product)}</small></span>
      </button>
      <button type="button" role="radio" aria-checked={direction === 'out'} class="dir" class:on={direction === 'out'} disabled={!run.length} onclick={() => (direction = 'out')} data-direction="out">
        <Icon name="up" size={17} />
        <span><strong>{t('Back to %1', product)}</strong><small class="muted">{t('Slow: the miners vote on it')}</small></span>
      </button>
    </div>

    <div class="field">
      <label for="move-chain">{t('Sidechain')}</label>
      <select id="move-chain" value={target} onchange={(e) => pick((e.currentTarget as HTMLSelectElement).value)} data-move-chain>
        {#if !target}<option value="" disabled>{t('Choose a sidechain')}</option>{/if}
        {#each run as e (e.id)}
          <option value={e.id} disabled={!e.running} translate="no">{e.name}{e.running ? '' : ` — ${t('not running')}`}</option>
        {/each}
        {#if direction === 'in' && !simple && others.length}
          <optgroup label={t('Other sidechains (by their deposit address)')}>
            {#each others as s (s.slot)}<option value={`slot:${s.slot}`} translate="no">{t('Slot %1', String(s.slot))}: {s.title}</option>{/each}
          </optgroup>
        {/if}
      </select>
    </div>

    {/if}

    <div class="field">
      <label for="move-amount">{t('Amount')}</label>
      <div class="with-unit">
        <input id="move-amount" type="text" class="num" inputmode="decimal" autocomplete="off" placeholder="0.00000000" bind:value={amount} aria-invalid={amountError ? 'true' : undefined} data-move-amount />
        <span class="unit">{entry && direction === 'out' ? entry.unit : unit}</span>
      </div>
      {#if amountError}<span class="error small">{t(amountError, '8')}</span>
      {:else if direction === 'in'}<span class="hint">{t('Available on %1', product)} <Amount sats={own?.trusted} /></span>
      {:else if entry}<span class="hint">{t('Available on %1', entry.name)} <ModuleAmount text={summary?.balances?.trusted ?? null} unit={entry.unit} coins /></span>{/if}
    </div>

    {#if !replaces}
    <div class="field">
      <span class="label" id="move-to">{t('To')}</span>
      {#if direction === 'in' && entry}
        <p class="to" aria-labelledby="move-to"><ChainBadge id={entry.id} name={entry.name} small /> <span class="muted">{t('your wallet there')}{#if summary?.wallet}<span class="dot-sep">{' · '}</span><span translate="no"><SafeText text={summary.wallet} /></span>{/if}</span></p>
      {:else if direction === 'in'}
        <input type="text" class="mono" maxlength="100" spellcheck="false" autocomplete="off" placeholder={t('The deposit address its wallet gives: s<slot>_…')} bind:value={destination} aria-labelledby="move-to" aria-invalid={destError ? 'true' : undefined} />
        {#if destError}<span class="error small">{destError}</span>{:else if destCheck?.kind === 'valid'}<span class="hint ok">{t('A deposit address of slot %1.', String(destCheck.slot))}</span>{/if}
      {:else}
        <div class="row">
          <input type="text" class="mono grow" spellcheck="false" autocomplete="off" placeholder={t('An address of %1', product)} bind:value={address} aria-labelledby="move-to" data-move-address />
          <button type="button" class="btn small" disabled={wallets.current === null} onclick={mineAddress} title={t('A new address of your %1 wallet', product)}>{t('My wallet')}</button>
        </div>
      {/if}
    </div>

    {/if}

    {#if direction === 'out' && !replaces}
      <div class="field">
        <label for="move-fee">{t('Fee for the miners of %1', product)}</label>
        <div class="with-unit">
          <input id="move-fee" type="text" class="num" inputmode="decimal" autocomplete="off" bind:value={fee} />
          <span class="unit">{unit}</span>
        </div>
        {#if feeError}<span class="error small">{t(feeError, '8')}</span>{:else}<span class="hint">{t('A higher fee is bundled first.')}</span>{/if}
      </div>
    {/if}

    {#if !replaces}
    <div class="coins" class:simple>
      <button type="button" class="disclose" aria-expanded={choosing} onclick={() => (choosing = !choosing)} data-choose-coins>
        <Icon name="chevron-right" size={14} />
        {t('Choose coins')}
        {#if selected.length}<span class="badge accent">{selected.length}</span>{/if}
      </button>
      {#if choosing}
        <CoinChooser {coins} bind:selected need={amount.trim() && !amountError ? amount.trim() : null} unit={direction === 'out' && entry ? entry.unit : unit} loading={coinsLoading} error={coinsError} />
      {/if}
    </div>
    {/if}

    {#if error}<p class="error" role="alert"><SafeText text={error} /></p>{/if}
    <div class="row submit">
      <button type="submit" class="btn primary" disabled={!ready} data-move-submit>
        <Icon name={direction === 'in' ? 'down' : 'up'} size={16} />
        {busy ? t('Working…') : direction === 'in' ? t('Deposit…') : t('Withdraw…')}
      </button>
      <span class="muted small">{t('You confirm the details in the next step.')}</span>
    </div>
  </form>
</section>

<style>
  .move .form {
    display: flex;
    flex-direction: column;
    gap: 0.9rem;
  }
  .directions {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 0.6rem;
  }
  .dir {
    display: flex;
    align-items: center;
    gap: 0.7rem;
    padding: 0.65rem 0.85rem;
    border: 1px solid var(--line);
    border-radius: var(--radius-m);
    background: var(--surface-2);
    color: var(--text);
    font: inherit;
    text-align: left;
    cursor: pointer;
  }
  .dir span {
    display: flex;
    flex-direction: column;
    min-width: 0;
  }
  .dir small {
    font-size: 0.8rem;
  }
  .dir.on {
    border-color: var(--accent);
    background: color-mix(in srgb, var(--accent) 10%, var(--surface-2));
  }
  .dir:disabled {
    opacity: 0.5;
    cursor: default;
  }
  .field {
    display: flex;
    flex-direction: column;
    gap: 0.3rem;
  }
  .field label,
  .label {
    font-weight: 600;
    font-size: 0.9rem;
  }
  .with-unit {
    display: flex;
    align-items: center;
    gap: 0.5rem;
  }
  .with-unit input {
    flex: 1;
  }
  .unit {
    color: var(--text-muted);
    font-weight: 600;
  }
  .to {
    display: flex;
    align-items: center;
    gap: 0.45rem;
    margin: 0;
  }
  .hint {
    color: var(--text-muted);
    font-size: 0.82rem;
  }
  .hint.ok {
    color: var(--positive);
  }
  .small {
    font-size: 0.82rem;
  }
  .coins {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
    padding-top: 0.6rem;
    border-top: 1px solid var(--line);
  }
  .disclose {
    display: inline-flex;
    align-items: center;
    gap: 0.4rem;
    align-self: flex-start;
    padding: 0.2rem 0;
    border: 0;
    background: none;
    color: var(--text);
    font: inherit;
    font-weight: 600;
    cursor: pointer;
  }
  .disclose :global(svg) {
    transition: transform 120ms ease-out;
  }
  .disclose[aria-expanded='true'] :global(svg) {
    transform: rotate(90deg);
  }
  .coins.simple .disclose {
    color: var(--text-muted);
    font-weight: 500;
    font-size: 0.9rem;
  }
  .submit {
    gap: 0.8rem;
  }
  .dropped p {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    margin: 0;
    color: var(--warning);
  }
  .replacing {
    padding: 0.6rem 0.8rem;
    border-left: 3px solid var(--warning);
    background: color-mix(in srgb, var(--warning) 8%, transparent);
  }
  .replacing p {
    margin: 0 0 0.3rem;
  }
  .link {
    padding: 0;
    border: 0;
    background: none;
    color: var(--link);
    font: inherit;
    cursor: pointer;
  }
  .error {
    margin: 0;
  }
  @container (max-width: 30rem) {
    .directions {
      grid-template-columns: 1fr;
    }
  }
</style>
