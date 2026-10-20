<script lang="ts">
  // Staking, on the app's own page (doc/unified-app.md §18): every chain that stakes (FastBlocks, Layers) together, or
  // one, with a filter: this node's validator keys and what they do, the validator set of the epoch, the bonds of the
  // wallet, the fee pot and the fee split. Bonding, unbonding and turning staking on or off are the chain's own pages,
  // shown here inside the page (the module's frame); their questions are asked in the app's trusted dialog.
  import { t } from '../lib/i18n.svelte';
  import Icon from '../components/Icon.svelte';
  import SafeText from '../components/SafeText.svelte';
  import ChainBadge from '../components/ChainBadge.svelte';
  import ChainChips from '../components/ChainChips.svelte';
  import ModuleAmount from '../components/ModuleAmount.svelte';
  import ModuleEmbed from '../components/ModuleEmbed.svelte';
  import { errorText } from '../lib/dialogs.svelte';
  import { canEmbed, chainHost, chainsWith, chainWallet, filtered, loadCapabilities, viewFilter, type ChainStaking } from '../lib/chains.svelte';

  const chains = $derived(chainsWith('staking'));
  const shown = $derived(filtered(chains, viewFilter('staking', chains)));
  let data = $state<Record<string, ChainStaking>>({});
  let errors = $state<Record<string, string>>({});
  let manage = $state<string | null>(null);

  $effect(() => {
    void loadCapabilities();
  });
  $effect(() => {
    const stems = shown.filter((e) => e.running).map((e) => e.id);
    const load = () => {
      for (const s of stems) {
        chainHost.staking(s, chainWallet(s)).then(
          (d) => {
            data[s] = d;
            errors[s] = '';
          },
          (e) => (errors[s] = errorText(e)),
        );
      }
    };
    load();
    const timer = setInterval(() => {
      if (!document.hidden) load();
    }, 8000);
    return () => clearInterval(timer);
  });

  const short = (k: string) => (k.length > 20 ? `${k.slice(0, 10)}…${k.slice(-6)}` : k);
  const bps = (n: number | undefined) => (typeof n === 'number' ? `${(n / 100).toFixed(2)} %` : '–');
</script>

<div class="page staking">
  <header class="page-head">
    <h1>{t('Staking')}</h1>
    <ChainChips page="staking" entries={chains} />
  </header>

  {#if chains.length === 0}
    <p class="muted">{t('No chain that stakes is turned on. Turn on FastBlocks or Layers in Settings → Sidechains.')}</p>
  {/if}

  {#each shown as e (e.id)}
    {@const d = data[e.id]}
    <section class="panel chain" aria-label={t('Staking on %1', e.name)} data-staking-chain={e.id}>
      <div class="panel-head">
        <h2><ChainBadge id={e.id} name={e.name} /></h2>
        {#if d?.chain}
          <span class="badge" class:accent={d.chain.active}>{d.chain.active ? t('Blocks of stake: on') : t('Blocks of stake: waiting for validators')}</span>
          {#if d.chain.epoch !== undefined}<span class="muted small">{t('Epoch %1, slot %2', String(d.chain.epoch), String(d.chain.slot ?? '–'))}</span>{/if}
        {/if}
        <span class="grow"></span>
        {#if canEmbed(e.id, 'staking')}
          <button type="button" class="btn small" onclick={() => (manage = manage === e.id ? null : e.id)} data-staking-manage={e.id}><Icon name="key" size={14} /> {manage === e.id ? t('Hide') : t('Bond, unbond, turn on…')}</button>
        {/if}
      </div>
      {#if !e.running}
        <p class="panel-body muted">{t('Its node is not running: turn it on in Settings → Sidechains.')}</p>
      {:else if errors[e.id] && !d}
        <p class="panel-body error"><SafeText text={errors[e.id]} /></p>
      {:else if !d}
        <p class="panel-body muted">{t('Loading…')}</p>
      {:else}
        <div class="panel-body facts">
          <div class="fact"><span class="muted small">{t('This node')}</span><strong>{d.node?.validator ? (d.node.enabled ? t('Validator, staking') : t('Validator, not staking')) : t('Not a validator')}</strong></div>
          <div class="fact"><span class="muted small">{t('Its stake / the set')}</span><strong>{d.node?.stake ?? 0} / {d.node?.totalstake ?? d.set?.totalstake ?? 0}</strong></div>
          <div class="fact"><span class="muted small">{t('Its share of the pool')}</span><strong><ModuleAmount text={d.node?.poolshare ?? null} coins /></strong></div>
          <div class="fact"><span class="muted small">{t('Blocks made')}</span><strong>{d.node?.blocksmade ?? 0}</strong></div>
          <div class="fact"><span class="muted small">{t('Bonded on the chain')}</span><strong><ModuleAmount text={d.chain?.staking?.bonded ?? null} coins /></strong></div>
          <div class="fact"><span class="muted small">{t('Fee split: validators')}</span><strong>{bps((d.node?.feesplit ?? d.chain?.feesplit)?.validatorsbps)}</strong></div>
          <div class="fact"><span class="muted small">{t('Fee pot paid out')}</span><strong><ModuleAmount text={d.chain?.pot?.paid ?? null} coins /></strong></div>
        </div>
        {#if d.node?.producererror}<p class="panel-body warn-box"><SafeText text={d.node.producererror} /></p>{/if}
        <div class="panel-body tables">
          {#if d.node?.keys?.length}
            <div>
              <h3>{t('Validator keys of this node')}</h3>
              <table class="table" data-staking-keys={e.id}>
                <thead><tr><th>{t('Key')}</th><th class="num">{t('Stake')}</th><th class="num">{t('Its share of the pool')}</th><th></th></tr></thead>
                <tbody>
                  {#each d.node.keys as k (k.pubkey)}
                    <tr data-node-key={k.pubkey}><td class="mono" translate="no"><SafeText text={short(k.pubkey)} /></td><td class="num">{k.stake ?? 0}</td><td class="num"><ModuleAmount text={k.poolshare ?? null} coins /></td><td>{#if k.inset}<span class="badge accent">{t('in the set')}</span>{:else}<span class="badge">{t('not in the set yet')}</span>{/if}</td></tr>
                  {/each}
                </tbody>
              </table>
            </div>
          {/if}
          <div>
            <h3>{t('Validators of the epoch')}</h3>
            <table class="table" data-staking-validators={e.id}>
              <thead><tr><th>{t('Key')}</th><th class="num">{t('Stake')}</th><th class="num">{t('Slots')}</th><th></th></tr></thead>
              <tbody>
                {#each (d.set?.validators ?? []).slice(0, 25) as v (v.pubkey)}
                  <tr data-validator={v.pubkey}><td class="mono" translate="no"><SafeText text={short(v.pubkey)} /></td><td class="num">{v.stake}</td><td class="num">{v.slots ?? '–'}</td><td>{#if v.local}<span class="badge accent">{t('this node')}</span>{/if}{#if v.banned}<span class="badge warning">{t('banned')}</span>{/if}</td></tr>
                {:else}
                  <tr><td colspan="4" class="muted">{t('No validators yet')}</td></tr>
                {/each}
              </tbody>
            </table>
          </div>
          <div>
            <h3>{t('Bonds of the wallet')} <span class="muted small"><SafeText text={d.wallet} /></span></h3>
            <table class="table" data-staking-bonds={e.id}>
              <thead><tr><th>{t('Validator')}</th><th class="num">{t('Bonded')}</th><th class="num">{t('Unbonding')}</th><th></th></tr></thead>
              <tbody>
                {#each d.bonds ?? [] as b (b.validator)}
                  <tr><td class="mono" translate="no"><SafeText text={short(b.validator)} /></td><td class="num"><ModuleAmount text={b.bonded ?? null} coins /></td><td class="num"><ModuleAmount text={b.unbonding ?? null} coins /></td><td>{#if b.inset}<span class="badge accent">{t('in the set')}</span>{/if}</td></tr>
                {:else}
                  <tr><td colspan="4" class="muted">{t('No bonds')}</td></tr>
                {/each}
              </tbody>
            </table>
          </div>
        </div>
      {/if}
      {#if manage === e.id}
        <div class="panel-body"><ModuleEmbed stem={e.id} page="staking" name={e.name} /></div>
      {/if}
    </section>
  {/each}
</div>

<style>
  .staking {
    display: flex;
    flex-direction: column;
    gap: 1rem;
  }
  .page-head {
    display: flex;
    align-items: center;
    gap: 1rem;
    flex-wrap: wrap;
  }
  .panel-head {
    display: flex;
    align-items: center;
    gap: 0.6rem;
    flex-wrap: wrap;
  }
  .panel-head h2 {
    margin: 0;
    font-size: 1rem;
  }
  .grow {
    flex: 1;
  }
  .facts {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(11rem, 1fr));
    gap: 0.8rem;
  }
  .fact {
    display: flex;
    flex-direction: column;
    gap: 0.15rem;
  }
  .tables {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(20rem, 1fr));
    gap: 1.2rem;
  }
  h3 {
    margin: 0 0 0.4rem;
    font-size: 0.92rem;
  }
  .table {
    width: 100%;
    border-collapse: collapse;
  }
  .table th,
  .table td {
    padding: 0.35rem 0.45rem;
    border-bottom: 1px solid var(--line);
    text-align: left;
  }
  .table th {
    color: var(--text-faint);
    font-size: 0.75rem;
    text-transform: uppercase;
  }
  .num {
    text-align: right !important;
    font-variant-numeric: tabular-nums;
  }
  .small {
    font-size: 0.82rem;
  }
  .table :global(.badge) {
    white-space: nowrap;
  }
</style>
