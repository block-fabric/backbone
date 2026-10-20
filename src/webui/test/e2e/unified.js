// The unified app (doc/unified-app.md), as a user drives it: Chains alone first (no sidechain, no RPC
// server); then Settings → Sidechains, Layers turned on through its wizard; its daemon starts wired to
// the node of the app; the chain switcher shows it in step with Chains; the first-start guide; Layers
// turned off: its daemon stops. Pictures in Midnight. The runner (run_unified.py) checks the processes
// and the ports at the checkpoints the script says (CHECKPOINT <name>), while the script waits.
(async () => {
  const { host, rpc, sleep, log, T, waitFor, check, snapshot, finish } = E2E;
  const $ = (selector, root = document) => root.querySelector(selector);
  const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];
  const openDialog = () => $$('dialog[open]').pop();
  const button = (label, root = document) => $$('button', root).find((b) => b.textContent.trim().replace(/\s+/g, ' ').startsWith(label));
  async function click(label, root) {
    const b = await waitFor(`the button "${label}"`, () => {
      const el = button(label, typeof root === 'function' ? root() : root);
      return el && !el.disabled ? el : null;
    });
    b.click();
    await sleep(80);
  }
  // A checkpoint of the runner: it checks the processes, then the script goes on.
  async function checkpoint(name, ms = 4000) {
    await log(`CHECKPOINT ${name}`);
    await sleep(ms);
  }
  // Pictures without the toasts of the moment (the notice of 135 new transactions over the page).
  async function shot(name) {
    $$('.toast button[aria-label="Dismiss"]').forEach((b) => b.click());
    await sleep(250);
    await snapshot(name);
  }
  const hubList = () => host('sidechains.list');
  const layersOf = async () => (await hubList()).chains.find((c) => c.stem === 'layers');
  try {
    await waitFor('the node to run', () => T() && T().app.phase === 'ready' && T().node.tip);
    T().savePrefs({ theme: 'midnight', layout: 'dashboard' });

    // 1. Chains alone: nothing of the sidechains runs.
    const first = await hubList();
    await check('standalone: no sidechain on', first.chains.every((c) => !c.enabled && c.pid === 0), JSON.stringify(first.chains.map((c) => [c.stem, c.state])));
    await check('standalone: no RPC server for sidechains', first.rpcServer === false);
    await check('standalone: no chain switcher', !$('.switcher'));
    await check('the catalog lists the sidechains', first.chains.map((c) => c.stem).join(',') === 'layers,fastblocks,thunder,bitnames,bitassets,zside,hivemind');
    await check('Layers is found next to the given folder', (first.chains[0].program || '').endsWith('/layersd'), first.chains[0].program);
    await checkpoint('standalone');

    // A chain with Layers activated in its slot (7).
    await rpc('createwallet', ['demo']);
    await waitFor('the wallet', () => T().wallets.current === 'demo');
    const mining = await rpc('getnewaddress', ['Mining'], 'demo');
    await rpc('generatetoaddress', [110, mining]);
    await rpc('createsidechainproposal', [7, 'Layers', 'Assets on a fast chain']);
    await rpc('generatetoaddress', [25, mining]);
    const active = await rpc('listactivesidechains');
    await check('Layers active in slot 7 on the mainchain', active.some((s) => s.slot === 7), JSON.stringify(active.map((s) => s.slot)));

    // 2. Settings → Sidechains.
    T().go('settings', 'sidechains');
    await waitFor('the sidechains of Settings', () => $$('.sidechain-settings ~ .cards .card').length === 7, 20000);
    await sleep(400);
    await shot('unified-settings-sidechains');

    // 3. Layers on, through its wizard.
    $('.card[data-stem="layers"] input[type="checkbox"]').click();
    const wizard = () => $('[data-wizard="enable"]');
    await waitFor('the enable wizard', () => wizard() && wizard().dataset.step === 'about');
    for (const [i, step] of ['about', 'storage', 'wallet', 'staking', 'review'].entries()) {
      await waitFor(`the step ${step}`, () => wizard().dataset.step === step);
      await sleep(250);
      await shot(`unified-enable-${i + 1}-${step}`);
      if (step !== 'review') $('[data-action="next"]').click();
    }
    $('[data-action="turn-on"]').click();
    await waitFor('Layers turned on', () => wizard() && wizard().dataset.step === 'done', 30000);
    await sleep(300);
    await shot('unified-enable-6-done');
    const on = await layersOf();
    await check('Layers enabled and its daemon started', on.enabled && on.pid > 0, JSON.stringify(on));
    await check('the RPC server of the node runs now, for the sidechains', (await hubList()).rpcServer === true);
    await click('Show it', openDialog);
    await waitFor('Layers shown', () => T().nav.page === 'sidechain', 10000).catch(() => null);

    // 4. The switcher: Layers, in step with Chains.
    await waitFor('the chain switcher', () => $$('.switcher .chain').length === 2, 20000);
    await check('the switcher: Chains, then Layers', $$('.switcher .chain').map((b) => b.dataset.chain).join(',') === 'chains,layers');
    await check('Layers shown', T().nav.page === 'sidechain' && window.__chainsHub.selected === 'layers');
    await waitFor('Layers running', async () => (await layersOf()).ready, 90000);
    await waitFor('Layers in step with Chains', () => $('[data-field="sync"]') && $('[data-field="sync"]').dataset.synced === 'yes', 120000);
    // The runner has Layers make a few blocks (merged mined by Chains), as a miner would.
    await checkpoint('bmm', 1000);
    await waitFor('the blocks of Layers', () => Number(($('[data-field="height"]').textContent || '0').replace(/\D/g, '')) >= 3, 60000);
    await waitFor('Layers in step again', () => $('[data-field="sync"]').dataset.synced === 'yes', 60000);
    const tip = await rpc('getblockcount');
    await check('the overview has the height of Layers', /^\d+$/.test($('[data-field="height"]').textContent.trim().replace(/,/g, '')), $('[data-field="height"]').textContent);
    await check('the overview says Chains is at its tip', $('.sidechain-view').textContent.includes(`Chains is at block ${tip}`), tip);
    await check('Layers has a wallet (made by the app)', /^\d+\.\d{8}$/.test($('[data-field="balance"]').textContent.trim()), $('[data-field="balance"]').textContent);
    await check('its recent blocks are listed', $$('.sidechain-view tbody tr').length >= 1);
    // A few more blocks of Chains: it keeps up.
    await rpc('generatetoaddress', [3, mining]);
    await waitFor('Layers keeps up', async () => {
      const o = await host('sidechains.overview', { stem: 'layers' });
      return o.synced && o.mainchainSeen === tip + 3;
    }, 90000);
    // Read-only: a method that spends is refused for the page.
    let refused = false;
    try {
      await host('sidechains.rpc', { stem: 'layers', method: 'sendtoaddress', params: ['x', 1] });
    } catch (e) {
      refused = true;
    }
    await check('the pages cannot spend on a sidechain (phase 1)', refused);
    const height = await host('sidechains.rpc', { stem: 'layers', method: 'getblockcount' });
    await check('the pages read a sidechain', typeof height === 'number');
    await sleep(3200);
    await shot('unified-switcher-layers');
    await checkpoint('enabled');

    // 5. The first-start guide, its screens.
    T().go('settings', 'sidechains');
    await waitFor('Settings → Sidechains', () => $('.sidechain-settings'));
    await click('Setup guide');
    const guide = () => $('[data-wizard="firstrun"]');
    await waitFor('the guide', () => guide());
    // Every screen to the last; FastBlocks picked when its program is there (then the storage screen comes).
    let n = 0;
    for (;;) {
      const step = guide().dataset.step;
      if (step === 'backup') [...guide().querySelectorAll('input[type="checkbox"]')][0].click();
      if (step === 'sidechains') {
        const pick = $('.pick[data-stem="fastblocks"] input');
        if (pick && !pick.disabled) pick.click();
      }
      await sleep(250);
      await shot(`unified-firstrun-${++n}-${step}`);
      if (step === 'sync' || n > 10) break;
      await click('Next', openDialog);
      await waitFor('the next screen of the guide', () => guide().dataset.step !== step);
    }
    await check('the guide ends on what happens next', guide().dataset.step === 'sync');
    await click('Skip for now', openDialog);
    await waitFor('the guide closed', () => !guide());
    await check('the guide turned nothing on when skipped', !(await hubList()).chains.find((c) => c.stem === 'fastblocks').enabled);

    // 6. Layers off: its daemon stops, its data stays.
    await waitFor('Layers in Settings', () => $('.card[data-stem="layers"] input[type="checkbox"]'));
    $('.card[data-stem="layers"] input[type="checkbox"]').click();
    await click('Turn off', openDialog);
    await waitFor('Layers stopped', async () => {
      const l = await layersOf();
      return !l.enabled && l.pid === 0 && l.state === 'disabled';
    }, 120000);
    const off = await layersOf();
    await check('Layers off: its data stays', off.hasData, JSON.stringify(off));
    await waitFor('no switcher', () => !$('.switcher'), 20000);
    await check('the switcher goes with the last sidechain', !$('.switcher'));
    await sleep(500);
    await shot('unified-settings-after-off');
    await checkpoint('disabled');
    await finish();
  } catch (e) {
    await finish(e);
  }
})();
