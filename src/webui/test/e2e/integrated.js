// The app's own pages over every chain it runs (doc/unified-app.md §18), as a user drives them: Layers (with a validator
// key of its own) and Thunder turned on; Overview shows every chain; coins moved into Layers from the Sidechains page
// (Chains' own question); an asset sent on Layers from the app's Send page, the chain picked in its picker, its
// question asked in the trusted dialog "On Layers" with the chain's mark, confirmed in a block; a wrong-chain address
// refused before any dialog; Receive on each chain; every chain's transactions in one list, with a filter; each chain's
// wallets; Staking with Layers' validator; Assets. The runner (run_integrated.py) merged-mines the sidechains' blocks at
// "CHECKPOINT bmm <stem> <count>". Pictures in Midnight.
//
// §19 (the UX round): one chain selector in the top bar for every page (All, or one chain), the chain of Send and
// Receive picked in the form's tiles (in step with it), sections that go for a chain they do not apply to, the Home
// chart one line per chain, every chain's lists on Overview; with BitAssets and Hivemind on (when the runner has them),
// their groups in the sidebar and a Hivemind position on Home ("CHECKPOINT hivemind trade 0": the runner trades).
(async () => {
  const { host, rpc, sleep, log, T, waitFor, check, snapshot, finish } = E2E;
  const $ = (selector, root = document) => root.querySelector(selector);
  const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];
  const desktop = (answer) => host('test.confirmAnswer', answer);
  const asked = async () => host('test.confirmed');
  const dialogWindow = (action, more = {}) => host('test.dialog', { action, ...more });
  async function inWindow(name, accept) {
    await waitFor(`${name}: the dialog window`, async () => (await dialogWindow('kind')) === 'themed', 60000);
    await waitFor(`${name}: its page`, async () => (await dialogWindow('eval', { script: `!document.body.classList.contains('loading') && !!document.getElementById('dialog-accept')` })) === true, 30000);
    await sleep(500);
    await dialogWindow('snapshot', { name });
    if (accept) await sleep(2600);
    await dialogWindow('eval', { script: `document.getElementById(${JSON.stringify(accept ? 'dialog-accept' : 'dialog-cancel')}).click(); true` });
    await waitFor(`${name}: the window gone`, async () => (await dialogWindow('kind')) === 'none', 20000);
  }
  async function shot(name) {
    $$('.toast button[aria-label="Dismiss"]').forEach((b) => b.click());
    if (T().toasts) T().toasts.list.splice(0);
    await sleep(500);
    await snapshot(name);
  }
  function type(el, value) {
    el.value = value;
    el.dispatchEvent(new Event('input', { bubbles: true }));
  }
  // Scroll a page (or the sidebar) so that an element is at its top: only that box, never the window.
  function scrollTo(el, box = $('#content')) {
    if (!el || !box) return;
    box.scrollTop += el.getBoundingClientRect().top - box.getBoundingClientRect().top - 8;
  }
  function choose(el, value) {
    el.value = value;
    el.dispatchEvent(new Event('change', { bubbles: true }));
  }
  const go = (page, tab = '') => T().go(page, tab);
  const chainOf = async (stem) => (await host('sidechains.list')).chains.find((c) => c.stem === stem);
  const heightOf = (stem) => host('sidechains.rpc', { stem, method: 'getblockcount' });
  async function bmm(stem, count) {
    const from = await heightOf(stem);
    await log(`CHECKPOINT bmm ${stem} ${count}`);
    await waitFor(`${count} blocks of ${stem}`, async () => (await heightOf(stem)) >= from + count, 180000);
  }
  const layersCall = (method, params = {}) => T().packs.route('layers', { rid: `e2e${Math.random()}`, kind: 'host', method, params });
  const confirmed = (method, params, more = {}) => layersCall('rpc.confirmed', { method, params, wallet: 'main', ...more });
  const summary = (stem) => host('chain.summary', { stem });
  const extra = (window.__INTEGRATED && window.__INTEGRATED.extra) || [];
  // The top bar's selector: open it, pick a chain (or All), and wait for every page to follow.
  async function selectChain(id) {
    if (T().prefs.chain === id && $('[data-global-chain]')?.dataset.globalChain === id) return;
    const b = await waitFor('the chain selector of the top bar', () => $('[data-global-chain]'), 20000);
    b.click();
    const o = await waitFor(`the option ${id}`, () => $(`[data-chain-option="${id}"]`), 10000);
    o.click();
    await waitFor(`${id} selected`, () => T().prefs.chain === id && $('[data-global-chain]')?.dataset.globalChain === id, 10000);
    await sleep(300);
  }

  try {
    await waitFor('the node to run', () => T() && T().app.phase === 'ready' && T().node.tip, 120000);
    T().savePrefs({ theme: 'midnight', layout: 'dashboard' });

    // A chain with Layers (slot 7) and Thunder (slot 2) active.
    await rpc('createwallet', ['demo']);
    await waitFor('the wallet', () => T().wallets.current === 'demo');
    const mining = await rpc('getnewaddress', ['Mining'], 'demo');
    await rpc('generatetoaddress', [110, mining]);
    await rpc('createsidechainproposal', [7, 'Layers', 'Assets on a fast chain']);
    await rpc('createsidechainproposal', [2, 'Thunder', 'Large blocks']);
    await rpc('generatetoaddress', [25, mining]);
    const active = (await rpc('listactivesidechains')).map((s) => s.slot);
    await check('Layers and Thunder active on Chains', active.includes(7) && active.includes(2), JSON.stringify(active));
    await host('sidechains.enable', { stem: 'layers', wallet: 'separate' });
    await host('sidechains.enable', { stem: 'thunder', wallet: 'separate' });
    for (const stem of extra) {
      await rpc('createsidechainproposal', [{ bitassets: 4, hivemind: 9 }[stem], { bitassets: 'BitAssets', hivemind: 'Hivemind' }[stem], 'e2e']);
    }
    if (extra.length) await rpc('generatetoaddress', [25, mining]);
    for (const stem of extra) await host('sidechains.enable', { stem, wallet: 'separate' });
    await waitFor('both running', async () => (await chainOf('layers')).ready && (await chainOf('thunder')).ready, 120000);
    for (const stem of extra) await waitFor(`${stem} running`, async () => (await chainOf(stem)).ready, 180000);
    await bmm('layers', 2);
    await bmm('thunder', 2);
    const caps = await host('chain.capabilities');
    await check('a table of what each chain does', caps.find((c) => c.stem === 'layers').assets && caps.find((c) => c.stem === 'layers').staking && !caps.find((c) => c.stem === 'thunder').assets, JSON.stringify(caps.map((c) => c.stem)));

    // 1. Coins into Layers from the app's Sidechains page: Chains' own question; the deposit noted on its way.
    go('sidechains');
    await waitFor('the peg panel of Layers', () => $('[data-chain-peg="layers"] [data-peg-bmm]'), 30000);
    await check('the Sidechains page has every chain this app runs', !!$('[data-chain-peg="thunder"]'));
    type($('[data-peg-deposit="layers"] [data-peg-deposit-amount]'), '20');
    await sleep(300);
    await desktop({ show: true });
    $('[data-peg-deposit="layers"] button[type="submit"]').click();
    await inWindow('integrated-dialog-deposit-into-layers', true);
    await waitFor('the deposit on its way', () => $('[data-peg-pending-deposit]'), 30000);
    let q = (await asked()).at(-1);
    await check('the deposit asked in Chains\' own dialog, for slot 7', q && q.title === 'Deposit to a sidechain' && JSON.stringify(q.rows).includes('Layers'), JSON.stringify(q && q.rows));
    await rpc('generatetoaddress', [1, mining]);
    await bmm('layers', 2);
    await waitFor('the deposit credited on Layers', async () => Number((await summary('layers')).balances.trusted) >= 19, 120000);
    // Thunder too, through the adapter (the same path).
    await desktop({ yes: true });
    const tdep = await host('chain.deposit', { stem: 'thunder', amount: '10' });
    await check('a deposit into Thunder by the adapter', /^[0-9a-f]{64}$/.test(tdep.txid || ''), JSON.stringify(tdep));
    await rpc('generatetoaddress', [1, mining]);
    await bmm('thunder', 2);
    await waitFor('the deposit credited on Thunder', async () => Number((await summary('thunder')).balances.trusted) >= 9, 120000);
    await waitFor('the Layers deposit no longer on its way', async () => {
      await host('chain.transactions', { stem: 'layers' }).then((r) => T().chains.creditDeposits('layers', r.rows));
      return T().chains.pendingDeposits('layers').length === 0;
    }, 60000);

    // An asset on Layers (as its own pages make it): reserved and registered, in one question each.
    await desktop({ always: true });
    await confirmed('reserveasset', ['GOLD'], { then: { method: 'registerasset', params: ['GOLD', 100000, 2] } });
    await bmm('layers', 4);
    await confirmed('registerasset', ['GOLD', 100000, 2], { authorizedOnly: true });
    await bmm('layers', 1);
    await desktop({ always: false });
    await desktop({ clear: true });
    const gold = (await summary('layers')).assets.find((a) => a.label === 'GOLD');
    await check('GOLD held on Layers', gold && Number(gold.balance) === 100000, JSON.stringify(gold));
    // A withdrawal from Layers, so that the Sidechains page shows one on its way back.
    const mainAddress = await rpc('getnewaddress', ['Back from Layers'], 'demo');
    await desktop({ yes: true });
    const wd = await host('chain.withdraw', { stem: 'layers', address: mainAddress, amount: '1' });
    await check('a withdrawal from Layers to an address of Chains, the chain\'s own question', /^[0-9a-f]{64}$/.test(wd.txid || ''), JSON.stringify(wd));
    q = (await asked()).at(-1);
    await check('the withdrawal asked "On Layers"', q && q.chain === 'Layers' && q.rows[0].label === 'Chain' && q.rows[0].value === 'Layers', JSON.stringify(q));
    await bmm('layers', 1);
    const wrongWd = await host('chain.withdraw', { stem: 'layers', address: (await host('chain.receive', { stem: 'thunder' })).address, amount: '1' }).then(() => null, (e) => e);
    await check('a withdrawal to an address of a sidechain refused', wrongWd && wrongWd.code === -5, JSON.stringify(wrongWd));

    // 2. Overview: every chain at once, with a filter.
    go('home');
    await waitFor('every chain on Home', () => $('[data-chain-card="layers"] [data-field="available"]') && $('[data-chain-card="thunder"]') && $('[data-chain-card="chains"]'), 30000);
    await waitFor('the balances of Layers', () => /\d/.test($('[data-chain-card="layers"] [data-field="available"]').textContent) && $('[data-chain-card="layers"] [data-card-assets]'), 30000);
    await check('Home shows every chain, with Layers\' assets', $('[data-chain-card="layers"] [data-card-assets]').textContent.includes('GOLD'));
    await shot('integrated-overview');
    await check('no chips of chains in a section: one selector in the top bar', !$('[data-chain-chips]') && !$('[data-chain-picker]') && !!$('[data-global-chain="all"]'));
    // The selector open, with All and each chain.
    $('[data-global-chain]').click();
    await waitFor('the list of chains', () => $('[data-global-chain-list] [data-chain-option="thunder"]'), 10000);
    await check('the selector: All, then each chain with its mark', $$('[data-chain-option]').map((o) => o.dataset.chainOption).join(',') === ['all', 'chains', 'layers', 'thunder', ...extra].join(','), $$('[data-chain-option]').map((o) => o.dataset.chainOption).join(','));
    await shot('ux-topbar-all');
    $('[data-chain-option="layers"]').click();
    await waitFor('Home follows the selection', () => !$('[data-chain-card="thunder"]') && $('[data-chain-card="layers"]') && !$('[data-chain-card="chains"]'), 20000);
    await check('the selection remembered, for every page', T().prefs.chain === 'layers');
    await check('Home with Layers: this app\'s own cards (mining, merged mining) gone', !$('[data-testid="mining-rate"]') && !$('[data-testid="bmm-line"]'));
    await waitFor('the chart of Layers', () => $('[data-chain-chart="layers"]'), 30000);
    await shot('ux-topbar-layers');
    go('overview');
    await waitFor('Overview with Layers only', () => $('[data-chains-overview="overview"] [data-chain-card="layers"]') && !$('[data-chains-overview="overview"] [data-chain-card="thunder"]') && !$('#balances-title'), 30000);
    // Ctrl+] goes on to the next chain (Thunder), then All after the last.
    window.dispatchEvent(new KeyboardEvent('keydown', { key: ']', ctrlKey: true, bubbles: true }));
    await waitFor('Ctrl+] to Thunder', () => T().prefs.chain === 'thunder', 5000);
    await selectChain('all');
    await waitFor('every chain on Overview', () => $('[data-chains-overview="overview"] [data-chain-card="thunder"]'), 30000);
    await waitFor('every chain\'s latest blocks on Overview', () => $('[data-many-blocks="blocks"] [data-chain-column="layers"] li button') && $('[data-many-blocks="blocks"] [data-chain-column="thunder"] li button') && $('[data-many-blocks="blocks"] [data-chain-column="chains"] li button'), 30000);
    await waitFor('every chain\'s recent transactions on Overview', () => $('[data-many-recent] [data-recent-tx="layers"]') && $('[data-many-recent] [data-recent-tx="chains"]'), 30000);
    await check('Overview: the mempool of each chain', !!$('[data-many-blocks="mempool"] [data-chain-column="thunder"]'));
    await shot('ux-overview-all');
    scrollTo($('[data-many-recent]').closest('section'));
    await sleep(300);
    await shot('ux-overview-all-lists');
    $('#content').scrollTo(0, 0);

    // 3. Send on Layers: the chain picked in the picker; a wrong-chain address refused before any dialog; GOLD sent.
    const layersTo = (await host('chain.receive', { stem: 'layers', label: 'GOLD to self' })).address;
    const thunderOwn = (await host('chain.receive', { stem: 'thunder' })).address;
    const chainsOwn = await rpc('getnewaddress', ['Not for Layers'], 'demo');
    go('send');
    await waitFor('the chain tiles of Send', () => $('[data-chain-tiles="send"] [data-chain-tile="layers"]'), 30000);
    await check('All selected: Send waits for a chain picked in its form', !!$('[data-send-wait]') && !!$('[data-chain-needed]') && !$('[data-sidechain-send]'));
    $('[data-chain-tile="layers"]').click();
    await waitFor('the Send form of Layers', () => $('[data-sidechain-send="layers"] [data-send-asset]'), 30000);
    await check('the chain picked in the form is the top bar\'s', T().prefs.chain === 'layers' && $('[data-global-chain]').dataset.globalChain === 'layers');
    const before = (await asked()).length;
    type($('[data-send-amount]'), '2');
    type($('[data-send-address]'), chainsOwn);
    await waitFor('a Chains address refused', () => $('[data-address-error]') && /not of Layers/.test($('[data-address-error]').textContent), 20000);
    await check('a Chains address refused on Layers before any dialog', $('[data-send-go]').disabled && (await asked()).length === before, $('[data-address-error]').textContent);
    type($('[data-send-address]'), thunderOwn);
    await waitFor('a Thunder address refused', () => $('[data-address-error]') && /Thunder/.test($('[data-address-error]').textContent), 20000);
    await shot('integrated-send-wrong-chain');
    const direct = await host('chain.send', { stem: 'layers', address: thunderOwn, amount: '1' }).then(() => null, (e) => e);
    await check('the host refuses it too, before anything is prepared or asked', direct && direct.code === -5 && (await asked()).length === before, JSON.stringify(direct));
    choose($('[data-send-asset]'), gold.id);
    type($('[data-send-amount]'), '25');
    type($('[data-send-address]'), layersTo);
    await waitFor('the address of Layers taken', () => $('[data-address-ok]'), 20000);
    await sleep(400);
    await shot('integrated-send-layers-asset');
    await shot('ux-send-layers-asset');
    await desktop({ show: true });
    $('[data-send-go]').click();
    await inWindow('integrated-dialog-layers-asset-send', true);
    await waitFor('the send done', () => $('[data-send-done]') || $('[data-send-error]'), 60000);
    await check('the asset sent', !!$('[data-send-done]'), $('[data-send-error]') ? $('[data-send-error]').textContent : '');
    q = (await asked()).at(-1);
    await check('asked in the trusted dialog "On Layers", with its mark, the chain first', q && q.chain === 'Layers' && q.chainLogo === true && q.rows[0].label === 'Chain' && q.rows[0].value === 'Layers', JSON.stringify(q));
    await check('its question names the destination and the asset', q.title === 'Send an asset' && q.rows.some((r) => r.value === layersTo), JSON.stringify(q.rows));
    const sentTx = $('[data-send-done] .mono') ? $('[data-send-done] .mono').textContent.trim() : '';
    await bmm('layers', 1);
    const rows = (await host('chain.transactions', { stem: 'layers' })).rows;
    await check('the asset send confirmed in a block of Layers', rows.some((r) => r.txid === sentTx && r.confirmations >= 1), sentTx);
    // A coin send on Thunder through the same page (its question "On Thunder"): picked with the top bar's selector,
    // the form's tiles follow.
    await selectChain('thunder');
    await waitFor('the Send form of Thunder', () => $('[data-sidechain-send="thunder"] [data-send-amount]'), 30000);
    await check('the form\'s tiles follow the top bar', $('[data-chain-tile="thunder"]').getAttribute('aria-checked') === 'true');
    await check('Thunder has no asset picker', !$('[data-sidechain-send="thunder"] [data-send-asset]'));
    type($('[data-send-amount]'), '1.5');
    type($('[data-send-address]'), thunderOwn);
    await waitFor('the address of Thunder taken', () => $('[data-address-ok]'), 20000);
    await shot('ux-send-thunder-coin');
    await desktop({ yes: true });
    $('[data-send-go]').click();
    await waitFor('the Thunder send done', () => $('[data-send-done]') || $('[data-send-error]'), 60000);
    q = (await asked()).at(-1);
    await check('a coin send on Thunder asked "On Thunder"', !!$('[data-send-done]') && q.chain === 'Thunder' && q.rows.some((r) => /^1\.50000000 /.test(r.value)), JSON.stringify(q));
    await check('the chain of Send is the selection, remembered', T().prefs.chain === 'thunder');
    await bmm('thunder', 1);

    // 4. Receive on each chain.
    go('receive');
    await waitFor('the chain tiles of Receive', () => $('[data-chain-tiles="receive"] [data-chain-tile="layers"]'), 30000);
    $('[data-chain-tile="layers"]').click();
    await waitFor('Receive on Layers', () => $('[data-sidechain-receive="layers"] [data-receive-new]'), 30000);
    $('[data-receive-new]').click();
    await waitFor('a new address of Layers', () => $('[data-receive-address]'), 30000);
    const shown = $('[data-receive-address]').dataset.receiveAddress;
    const valid = await host('sidechains.rpc', { stem: 'layers', method: 'getblockcount' }).then(() => host('chain.validate', { stem: 'layers', address: shown }));
    await check('an address of Layers\' wallet', valid.own === true && /^rsc1/.test(shown), shown);
    $('[data-receive-deposit-address]').click();
    await waitFor('its deposit address', () => /s7_/.test($('[data-sidechain-receive="layers"]').textContent), 20000);
    await shot('integrated-receive');
    await shot('ux-receive');
    $('[data-chain-tile="thunder"]').click();
    await waitFor('Receive on Thunder', () => $('[data-sidechain-receive="thunder"] [data-receive-new]'), 30000);
    $('[data-receive-new]').click();
    await waitFor('a new address of Thunder', () => $('[data-sidechain-receive="thunder"] [data-receive-address]'), 30000);
    const tshown = $('[data-sidechain-receive="thunder"] [data-receive-address]').dataset.receiveAddress;
    await check('an address of Thunder\'s wallet, refused on Layers', !!(await host('chain.validate', { stem: 'thunder', address: tshown })).own && (await host('chain.validate', { stem: 'layers', address: tshown }).then(() => false, (e) => e.code === -5)), tshown);

    // 5. Transactions: every chain in one list, with a filter; a row's details in the app.
    go('transactions');
    await selectChain('all');
    await waitFor('every chain\'s transactions', () => $('tr[data-chain-tx="layers"]') && $('tr[data-chain-tx="thunder"]') && $('tr[data-chain-tx="chains"]'), 30000);
    await check('no selector of chains in the page: the top bar\'s', !$('[data-chain-filter]') && !$('[data-chain-chips]'));
    await shot('integrated-transactions');
    await shot('ux-transactions');
    await selectChain('layers');
    await waitFor('only Layers', () => $('tr[data-chain-tx="layers"]') && !$('tr[data-chain-tx="thunder"]') && !$('tr[data-chain-tx="chains"]'), 20000);
    $(`tr[data-chain-tx="layers"][data-txid="${sentTx}"]`)?.click() ?? $('tr[data-chain-tx="layers"]').click();
    await waitFor('its details in the app', () => $('[data-tx-chain="layers"]') && $('dialog[open] .details'), 30000);
    await check('a sidechain transaction\'s details shown by the app, not its module', T().nav.page === 'transactions');
    await shot('integrated-transaction-details');
    $$('dialog[open] button').find((b) => b.textContent.trim() === 'Close').click();
    await selectChain('chains');
    await waitFor('the app\'s own history', () => !$('[data-chain-activity]'), 20000);
    await selectChain('all');

    // 6. Wallets: each chain's.
    go('wallets');
    await waitFor('the wallets of each chain', () => $('[data-chain-wallets="layers"] [data-chain-wallet="main"]') && $('[data-chain-wallets="thunder"] [data-chain-wallet="main"]'), 30000);
    await check('a separate wallet said so', $('[data-chain-wallets="thunder"]').textContent.includes('Its own backup'));
    const made = await host('chain.wallet.create', { stem: 'layers', name: 'savings' });
    await check('a wallet made on Layers from the app', made && made.name === 'savings', JSON.stringify(made));
    go('home');
    go('wallets');
    await waitFor('the new wallet listed', () => $('[data-chain-wallets="layers"] [data-chain-wallet="savings"]'), 30000);
    await shot('integrated-wallets');

    // 7. Sidechains page: the deposit, the withdrawal on its way, the merged mining of each.
    go('sidechains');
    await sleep(2000);
    await waitFor('the withdrawal on its way', () => $('[data-chain-peg="layers"] [data-peg-withdrawal]'), 30000);
    await shot('integrated-sidechains');

    // 8. Staking: Layers' validator.
    go('staking');
    await waitFor('the staking of Layers', () => $('[data-staking-chain="layers"] [data-staking-keys="layers"] [data-node-key]'), 30000);
    await check('Staking shows the validator key of Layers\' node', $('[data-staking-chain="layers"]').textContent.includes('Validator'));
    await check('Staking only for the chains that stake', !$('[data-staking-chain="thunder"]'));
    await shot('integrated-staking');
    await selectChain('thunder');
    await waitFor('Staking with Thunder: said, not shown', () => $('[data-not-on-chain="thunder"]') && !$('[data-staking-chain]'), 20000);
    await check('a page of what Thunder has not: its sections gone, the chains that have it a click away', !!$('[data-not-on-chain="thunder"] [data-show-chain="layers"]'));
    await shot('ux-sections-staking-thunder');
    await selectChain('all');

    // 9. Assets: every chain that has them; Trade, Layers' own page inside the app's.
    go('assets');
    await waitFor('the assets of Layers', () => $(`[data-asset-row="layers"][data-asset="${gold.id}"]`), 30000);
    await shot('integrated-assets');
    go('assets', 'trade');
    if (extra.includes('bitassets')) {
      // Two chains with a market of assets and All shown: Trade asks which (one chain's market).
      await waitFor('Trade asks which chain', () => $('[data-trade-pick] [data-trade-chain="bitassets"]'), 20000);
      $('[data-trade-pick] [data-trade-chain="layers"]').click();
    } else {
      await selectChain('layers');
    }
    await waitFor('Trade inside the page', () => $('[data-module-embed="layers"]') && $('iframe.module-frame.embedded:not([hidden])'), 60000);
    await sleep(3000);
    const fr = $('iframe.module-frame.embedded');
    await check('the module\'s frame inside the app\'s page, on its own origin', fr.dataset.stem === 'layers' && fr.getAttribute('src').startsWith('chains-pack://layers/') && T().modules.embed.page === 'trade');
    await shot('integrated-assets-trade');
    go('home');
    await waitFor('the frame no longer laid over the page', () => !$('iframe.module-frame.embedded:not([hidden])'), 20000);

    // The sidebar's Chain section: one group per sidechain, its features in it; no switcher, no top-level Staking/Assets.
    await check('a group per sidechain in the sidebar', ['layers', 'thunder', ...extra].every((s) => $(`[data-chain-group="${s}"]`)) && !$('.switcher'));
    await check('Layers\' group: Assets, Trade, Staking, its deposits, its own pages', ['assets', 'trade', 'staking', 'move', 'own'].every((i) => $(`[data-chain-item="layers:${i}"]`)));
    $('[data-chain-item="layers:trade"]').click();
    await waitFor('Layers: Trade from its group', () => T().nav.page === 'assets' && T().nav.tab === 'trade' && T().prefs.chain === 'layers', 20000);
    go('home');
    await selectChain('all');

    // Hivemind (when the runner has it): coins in, a market traded by its own CLI, the open position on Home.
    if (extra.includes('hivemind')) {
      await bmm('hivemind', 2);
      await desktop({ yes: true });
      const hdep = await host('chain.deposit', { stem: 'hivemind', amount: '30' });
      await check('a deposit into Hivemind', /^[0-9a-f]{64}$/.test(hdep.txid || ''), JSON.stringify(hdep));
      await rpc('generatetoaddress', [1, mining]);
      await bmm('hivemind', 2);
      await waitFor('the deposit credited on Hivemind', async () => Number((await summary('hivemind')).balances.trusted) >= 29, 180000);
      await log('CHECKPOINT hivemind trade 0');
      await waitFor('a position of the wallet', async () => ((await host('chain.positions', { stem: 'hivemind' })).positions || []).length > 0, 180000);
      go('home');
      await waitFor('the open positions on Home', () => $('[data-market-positions] [data-position]'), 60000);
      const row = $('[data-market-positions] [data-position]').textContent;
      await check('the position: its market, Yes, 3 shares, its price and worth now', /Rain tomorrow\?/.test(row) && /Yes/.test(row) && /\b3\b|3\.0/.test(row) && /0\.\d{8}/.test(row), row);
      await waitFor('the chart of every chain', () => $$('[data-chart-series]').length >= 3, 60000);
      await check('the chart: a line per chain, a legend in each one\'s coin', $$('svg path.line').length >= 3);
      scrollTo($('[data-chains-overview="home"]'));
      await sleep(300);
      await shot('ux-home-every-chain');
      scrollTo($('[data-market-positions]'));
      await sleep(300);
      await shot('ux-home-chart-positions');
      $('#content').scrollTo(0, 0);
      {
        T().savePrefs({ mask: true });
        await sleep(600);
        const masked = $('[data-market-positions]').textContent;
        await check('the positions and the chart hidden in privacy mode', !/0\.\d{8}/.test(masked) && !$('[data-chain-chart] svg') && masked.includes('••••••'), masked);
        T().savePrefs({ mask: false });
      }
      $('[data-open-markets]').click();
      await waitFor('Markets → Positions', () => T().nav.page === 'markets' && T().nav.tab === 'positions' && $('[data-position]'), 30000);
      await shot('ux-markets-positions');
      await selectChain('all');
    } else {
      go('home');
      await waitFor('the chart of every chain', () => $$('[data-chart-series]').length >= 2, 60000);
      await shot('ux-home-chart-positions');
    }

    // The sidebar with the groups open, in Advanced and in Simple.
    go('home');
    T().savePrefs({ chainGroupsFolded: ['thunder'] });
    await sleep(800);
    scrollTo($('[data-chain-group="layers"]'), $('.sidebar .scroll'));
    await sleep(300);
    await shot('ux-sidebar-advanced');
    scrollTo($('[data-chain-group="bitassets"]'), $('.sidebar .scroll'));
    await sleep(300);
    await shot('ux-sidebar-advanced-bitassets-hivemind');
    T().savePrefs({ uiMode: 'simple' });
    await sleep(800);
    scrollTo($('[data-chain-group="layers"]'), $('.sidebar .scroll'));
    await sleep(300);
    await shot('ux-sidebar-simple');
    T().savePrefs({ uiMode: 'advanced', chainGroupsFolded: [] });
    await sleep(400);

    // Off again: nothing left running.
    await host('sidechains.disable', { stem: 'layers' });
    await host('sidechains.disable', { stem: 'thunder' });
    for (const stem of extra) await host('sidechains.disable', { stem });
    await waitFor('both stopped', async () => (await host('sidechains.list')).chains.every((c) => c.pid === 0), 180000);
    await finish();
  } catch (e) {
    await finish(e instanceof Error ? e : new Error(JSON.stringify(e)));
  }
})();

