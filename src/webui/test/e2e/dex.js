// The Trade page (doc/unified-app.md §22) as a user drives it, on Layers with its DEX's rules: coins moved into Layers,
// two assets registered (as Layers' own pages do), a pool created with the wizard and a second one, a swap whose route
// goes through both pools (quoted, its route shown hop by hop, asked in the trusted dialog "On Layers" with the route the
// app checked first and Layers' own rows, cleared in a block), liquidity added to a pool and half of it taken out from
// the pool's page, the positions, the market index, the place of limit orders, Simple mode's swap, and only the chain
// shown read. The runner (run_dex.py) merged-mines Layers' blocks at "CHECKPOINT bmm layers <count>". Pictures in
// Midnight, in ~/skydoge/media/unified-app/dex/.
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
    await sleep(600);
    await snapshot(name);
  }
  function type(el, value) {
    el.value = value;
    el.dispatchEvent(new Event('input', { bubbles: true }));
  }
  const go = (page, tab = '') => T().go(page, tab);
  const chainOf = async (stem) => (await host('sidechains.list')).chains.find((c) => c.stem === stem);
  const heightOf = (stem) => host('sidechains.rpc', { stem, method: 'getblockcount' });
  async function bmm(count) {
    const from = await heightOf('layers');
    await log(`CHECKPOINT bmm layers ${count}`);
    await waitFor(`${count} blocks of Layers`, async () => (await heightOf('layers')) >= from + count, 180000);
  }
  const layersCall = (method, params = {}) => T().packs.route('layers', { rid: `e2e${Math.random()}`, kind: 'host', method, params });
  const confirmed = (method, params, more = {}) => layersCall('rpc.confirmed', { method, params, wallet: 'main', ...more });
  const summary = (stem) => host('chain.summary', { stem });
  const held = async (label) => (await summary('layers')).assets?.find((a) => a.label === label);
  async function pickAsset(side, id) {
    $(`[data-asset-picker="${side}"] [data-asset-picked]`).click();
    const o = await waitFor(`the asset ${id.slice(0, 8)} offered (${side})`, () => $(`[data-asset-picker="${side}"] [data-asset-option="${id}"]`), 30000);
    o.click();
    await sleep(150);
  }
  const COIN = '0'.repeat(64);

  try {
    await waitFor('the node to run', () => T() && T().app.phase === 'ready' && T().node.tip, 120000);
    T().savePrefs({ theme: 'midnight', layout: 'dashboard' });

    // Layers (slot 7) active, turned on, its DEX's rules from its first blocks.
    await rpc('createwallet', ['demo']);
    await waitFor('the wallet', () => T().wallets.current === 'demo');
    const mining = await rpc('getnewaddress', ['Mining'], 'demo');
    await rpc('generatetoaddress', [110, mining]);
    await rpc('createsidechainproposal', [7, 'Layers', 'Assets on a fast chain']);
    await rpc('generatetoaddress', [25, mining]);
    await host('sidechains.enable', { stem: 'layers', wallet: 'separate' });
    await waitFor('Layers running', async () => (await chainOf('layers')).ready, 120000);
    await waitFor('Layers\' wallet', async () => ((await host('chain.wallets', { stem: 'layers' }).catch(() => null))?.wallets ?? []).some((w) => w.name === 'main' && w.loaded), 120000);
    await bmm(2);
    const caps = await host('chain.capabilities');
    const lc = caps.find((c) => c.stem === 'layers');
    await check('Layers\' DEX in the table: routes, prices over time', lc.dex && lc.dexFeatures && lc.dexFeatures.route && lc.dexFeatures.twap && !lc.dexFeatures.orders, JSON.stringify(lc.dexFeatures));
    const info = await host('chain.dex.info', { stem: 'layers' });
    await check('its node says the DEX\'s rules apply, routes of up to 4 pools', info.active && info.features.route && info.maxHops === 4, JSON.stringify(info));
    await check('no pool kinds yet: constant product at 0.30 % only', !info.curvesActive && info.curves.length === 1 && info.feeTiers[0] === 3000, JSON.stringify(info));

    // Coins into Layers (Chains' own question), then two assets (Layers' own questions, as its pages make them).
    await desktop({ yes: true });
    await host('chain.deposit', { stem: 'layers', amount: '60' });
    await rpc('generatetoaddress', [1, mining]);
    await bmm(2);
    await waitFor('the deposit credited on Layers', async () => Number((await summary('layers')).balances.trusted) >= 59, 120000);
    await desktop({ always: true });
    await confirmed('reserveasset', ['GOLD'], { then: { method: 'registerasset', params: ['GOLD', 100000, 2] } });
    await confirmed('reserveasset', ['SILV'], { then: { method: 'registerasset', params: ['SILV', 500000, 4] } });
    await bmm(4);
    await confirmed('registerasset', ['GOLD', 100000, 2], { authorizedOnly: true });
    await confirmed('registerasset', ['SILV', 500000, 4], { authorizedOnly: true });
    await bmm(1);
    await desktop({ always: false });
    await desktop({ clear: true });
    const gold = await held('GOLD');
    const silv = await held('SILV');
    await check('GOLD and SILV held on Layers', gold && silv && Number(gold.balance) === 100000 && Number(silv.balance) === 500000, JSON.stringify({ gold, silv }));

    // 1. Trade in the Wallet section, Layers selected in the top bar: the Pools tab, empty; a pool made with the wizard.
    T().savePrefs({ chain: 'layers' });
    const wallet = await waitFor('the Wallet section of the sidebar', () => $('nav.sidebar [role="group"][aria-label="Wallet"]'), 20000);
    const names = $$('.entry .grow', wallet).map((e) => e.textContent.trim());
    await check('Trade in the Wallet section, after Receive', names.indexOf('Trade') === names.indexOf('Receive') + 1, names.join(', '));
    go('trade', 'pools');
    await waitFor('the pools of Layers (none yet)', () => $('[data-pools-list]') && /No pool yet/.test($('[data-pools-list]').textContent), 30000);
    $('[data-pool-create]').click();
    await waitFor('the wizard', () => $('[data-create-pool="pair"]'), 10000);
    await pickAsset('a', gold.id);
    await pickAsset('b', COIN);
    await shot('dex-create-pool-pair');
    $('[data-pool-next]').click();
    await waitFor('the kind step', () => $('[data-create-pool="kind"]'), 10000);
    await check('the other kinds shown, not offered, until Layers\' pool kinds apply', $('[data-curve="stable"]').disabled && $('[data-curve="weighted"]').disabled && $('[data-fee-tier="500"]').disabled && !$('[data-fee-tier="3000"]').disabled && !!$('[data-curves-off]'));
    await shot('dex-create-pool-kind');
    $('[data-pool-next]').click();
    await waitFor('the amounts step', () => $('[data-pool-amount="a"]'), 10000);
    type($('[data-pool-amount="a"]'), '10000');
    type($('[data-pool-amount="b"]'), '20');
    await waitFor('the first price', () => /0\.002/.test($('[data-pool-first-price]')?.textContent ?? ''), 10000);
    $('[data-pool-next]').click();
    await waitFor('the review', () => $('[data-pool-review]'), 10000);
    await shot('dex-create-pool-review');
    await desktop({ show: true });
    let before = (await asked()).length;
    $('[data-pool-go]').click();
    await inWindow('dex-dialog-create-pool', true);
    let q = (await asked()).at(-1);
    await check('the new pool asked "On Layers": the chain first, what the app checked, Layers\' own rows', (await asked()).length === before + 1 && q.chain === 'Layers' && q.rows[0].label === 'Chain' && q.rows.some((r) => r.label === 'Checked by Chains') && q.rows.some((r) => r.label === 'New pool' && r.value === 'constant product · 0.30 %') && q.title === 'Add liquidity', JSON.stringify(q));
    await bmm(1);
    // A second pool (CHN and SILV), straight through the host (the same path the wizard takes).
    await desktop({ yes: true });
    await host('chain.dex.createPool', { stem: 'layers', a: silv.id, b: COIN, amountA: '50000', amountB: '20', curve: 'constant-product', fee: 3000 });
    await bmm(2);
    go('trade', 'pools');
    await waitFor('both pools listed', () => $$('[data-pool-row]').length === 2, 60000);
    await check('each pool with its curve and fee tier, its liquidity about in CHN', $$('[data-pool-row]').every((r) => r.querySelector('[data-pool-curve="constant-product"]') && /0\.30 %/.test(r.querySelector('[data-pool-fee]').textContent)) && /40\.00/.test($('[data-pools-list]').textContent), $('[data-pools-list]').textContent.slice(0, 300));
    await waitFor('the wallet\'s share of each pool', () => $$('[data-pool-row]').every((r) => /%\s*$/.test(r.lastElementChild.textContent.trim())), 60000);
    await shot('dex-pools');

    // 2. A swap whose route goes through both pools: GOLD → CHN → SILV, quoted, shown hop by hop, asked, cleared.
    go('trade', '');
    await waitFor('the swap form', () => $('[data-swap="layers"] [data-swap-amount]'), 30000);
    await check('the tiles offer only the chains with a DEX, Layers picked', $$('[data-chain-tile]').map((t) => t.dataset.chainTile).join() === 'layers' && $('[data-chain-tile="layers"]').getAttribute('aria-checked') === 'true');
    await pickAsset('pay', gold.id);
    await pickAsset('get', silv.id);
    type($('[data-swap-amount]'), '100');
    await waitFor('the quote and its route', () => $('[data-route-hops="2"]') && $('[data-swap-least]'), 30000);
    const path = $('[data-route-path]').textContent.replace(/\s+/g, ' ');
    await check('the route through both pools, each its kind and fee', /GOLD.*CHN.*SILV/.test(path) && $$('[data-hop-curve="constant-product"]').length === 2 && $$('[data-hop-fee="3000"]').length === 2, path);
    const least = $('[data-swap-least]').textContent.replace(/[^0-9.]/g, '');
    const impact = $('[data-swap-impact]').textContent;
    await check('a price impact and the least received at 0.5 %', /%/.test(impact) && Number(least) > 0, `${impact} ${least}`);
    await shot('dex-swap-advanced');
    await desktop({ show: true });
    before = (await asked()).length;
    const silvBefore = Number((await held('SILV')).balance);
    $('[data-swap-go]').click();
    await inWindow('dex-dialog-swap-route', true);
    await waitFor('the swap sent', () => $('[data-swap-done]') || $('[data-swap-error]'), 60000);
    await check('the swap sent', !!$('[data-swap-done]'), $('[data-swap-error]')?.textContent ?? '');
    q = (await asked()).at(-1);
    const row = (label) => q.rows.find((r) => r.label === label)?.value ?? '';
    await check('asked "On Layers", the chain first, its mark', q.chain === 'Layers' && q.chainLogo === true && q.rows[0].label === 'Chain', JSON.stringify(q));
    await check('the route the app checked, in words: the pools, what is paid, quoted again, the least', q.rows.some((r) => r.label === 'Checked by Chains') && row('Route') === 'GOLD → CHN → SILV (2 pools)' && /^GOLD \/ CHN · constant product · 0\.30 %/.test(row('Pool 1')) && row('You pay') === '100.00 GOLD' && row('You receive at least').startsWith(`${least} SILV`), JSON.stringify(q.rows));
    await check('then Layers\' own question: a swap through pools, its route by label and hash, its amounts', q.title === 'Swap through pools' && q.rows.filter((r) => r.label === 'Route').length === 2 && q.rows.some((r) => r.label === 'Asset 1' && r.value.includes(gold.id)) && q.rows.some((r) => r.label === 'min_out'), JSON.stringify(q.rows));
    await bmm(1);
    await waitFor('the SILV of the swap in the wallet', async () => Number((await held('SILV')).balance) >= silvBefore + Number(least), 60000);
    await check('the swap cleared in a block, at least what was shown', true);

    // 3. A pool's page: its price over time, what it holds, liquidity in at its price, then half of it out.
    go('trade', 'pools');
    await waitFor('the pools', () => $$('[data-pool-row]').length === 2, 30000);
    const goldPool = $$('[data-pool-row]').find((r) => r.textContent.includes('GOLD'));
    const goldPoolId = () => goldPool.dataset.poolRow;
    goldPool.click();
    await waitFor('the pool\'s page', () => $('[data-pool-page]') && /GOLD \/ CHN/.test($('[data-pool-page]').textContent), 30000);
    await waitFor('its share shown (the wallet made it)', () => /Of the pool/.test($('[data-pool-share]').textContent), 30000);
    const chart = await host('chain.dex.chart', { stem: 'layers', pool: goldPoolId() });
    await log(`the pool's chart: ${JSON.stringify(chart).slice(0, 400)}`);
    await check('its price over time, from the pool\'s averages (at least two parts)', chart.points.length >= 2 && chart.labelA !== undefined, JSON.stringify(chart).slice(0, 300));
    await waitFor('its price over time drawn', () => Number($('[data-price-chart]')?.dataset.priceChart) >= 2, 70000);
    type($('[data-liq-amount-a]'), '1000');
    await waitFor('the CHN at the pool\'s price', () => /^\d+\.\d{8}$/.test($('[data-liq-amount-b]').value), 10000);
    const chnIn = $('[data-liq-amount-b]').value;
    await check('the second amount at the pool\'s price, exactly', Math.abs(Number(chnIn) - 1000 * Number(($('[data-pool-price]').textContent.match(/= ([0-9.]+)/) || [])[1])) < 0.001, chnIn);
    await shot('dex-pool-page');
    await desktop({ yes: true });
    // The balances as of the last block (the page reads them again at most every 20 s).
    await waitFor('Add enabled (the GOLD of the wallet read again)', () => !$('[data-liq-add]').disabled, 60000);
    await shot('dex-add-liquidity');
    before = (await asked()).length;
    $('[data-liq-add]').click();
    await waitFor('the liquidity asked', async () => (await asked()).length > before, 60000);
    q = (await asked()).at(-1);
    await check('liquidity in asked "On Layers": the pool and what goes in, then Layers\' rows', q.chain === 'Layers' && q.title === 'Add liquidity' && q.rows.some((r) => r.label === 'Pool' && /GOLD \/ CHN/.test(r.value)) && q.rows.some((r) => r.label === 'You put in at most' && r.value.startsWith('1000.00')), JSON.stringify(q.rows));
    await waitFor('liquidity added', () => $('[data-liq-done]') || $('[data-liq-error]'), 30000);
    await bmm(1);
    await waitFor('the wallet\'s share read again', () => $('[data-liq-tab="remove"]') && !$('[data-liq-tab="remove"]').disabled, 60000);
    $('[data-liq-tab="remove"]').click();
    (await waitFor('the shares to take out', () => $('[data-liq-pct="50"]'), 20000)).click();
    await waitFor('Remove enabled', () => !$('[data-liq-remove]').disabled, 60000);
    await waitFor('what half of it gives back', () => /You get about/.test($('[data-liquidity="remove"]').textContent), 20000);
    await shot('dex-remove-liquidity');
    await desktop({ yes: true });
    before = (await asked()).length;
    $('[data-liq-remove]').click();
    await waitFor('the removal asked', async () => (await asked()).length > before, 60000);
    q = (await asked()).at(-1);
    await check('liquidity out asked "On Layers": half of the shares held', q.chain === 'Layers' && q.title === 'Remove liquidity' && q.rows.some((r) => r.label === 'Shares given back' && r.value.startsWith('50.00 %')), JSON.stringify(q.rows));
    await bmm(1);

    // 4. Positions, the market index, the place of limit orders.
    go('trade', 'positions');
    await waitFor('the positions', () => $$('[data-position]').length === 2, 60000);
    await check('both pools among the wallet\'s positions, in full range', $$('[data-position-range="full"]').length === 2);
    await shot('dex-positions');
    go('trade', 'markets');
    await waitFor('the market index', () => $$('[data-market-row]').length === 2, 30000);
    type($('[data-market-search]'), 'silv');
    await waitFor('the search', () => $$('[data-market-row]').length === 1, 10000);
    type($('[data-market-search]'), '');
    await waitFor('every pair', () => $$('[data-market-row]').length === 2, 10000);
    await shot('dex-market-index');
    go('trade', 'orders');
    await waitFor('the place of limit orders', () => $('[data-orders]'), 10000);
    await shot('dex-orders');

    // 5. Simple mode: the swap alone.
    T().savePrefs({ uiMode: 'simple' });
    go('trade', '');
    await waitFor('Simple\'s swap', () => $('.swap.simple [data-swap-amount]'), 30000);
    await check('Simple: Swap, no tabs, no route panel', $('.page.trade h1').textContent.trim() === 'Swap' && !$('[data-trade-tab]') && !$('[data-swap-route-panel]'));
    await pickAsset('pay', gold.id);
    await pickAsset('get', silv.id);
    type($('[data-swap-amount]'), '50');
    await waitFor('the quote', () => $('[data-swap-least]'), 30000);
    await shot('dex-swap-simple');
    T().savePrefs({ uiMode: 'advanced' });

    await host('sidechains.disable', { stem: 'layers' }).catch(() => null);
    await waitFor('Layers stopped', async () => !(await chainOf('layers')).ready, 120000).catch(() => null);
    await finish();
  } catch (e) {
    await finish(e);
  }
})();
