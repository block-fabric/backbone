// The app over every chain with a lived-in state (run_showcase.py, doc/unified-app.md §20): set up once (four
// sidechains active and turned on, coins in each, an asset, a market, payments with labels, mining rewards), then run:
// what the app costs while nothing happens, page by page (the runner samples /proc), and the pictures of every page in
// each mode, look and size asked.
(async () => {
  const { host, rpc, sleep, log, T, waitFor, check, snapshot, finish } = E2E;
  const $ = (selector, root = document) => root.querySelector(selector);
  const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];
  const S = window.__SHOWCASE || { phase: 'setup' };
  const desktop = (answer) => host('test.confirmAnswer', answer);
  const go = (page, tab = '') => T().go(page, tab);
  const chainOf = async (stem) => (await host('sidechains.list')).chains.find((c) => c.stem === stem);
  const heightOf = (stem) => host('sidechains.rpc', { stem, method: 'getblockcount' });
  const summary = (stem) => host('chain.summary', { stem });
  const SIDE = ['layers', 'thunder', 'bitassets', 'hivemind'];
  const SLOTS = { layers: 7, thunder: 2, bitassets: 4, hivemind: 9 };
  const NAMES = { layers: 'Layers', thunder: 'Thunder', bitassets: 'BitAssets', hivemind: 'Hivemind' };
  async function bmm(stem, count) {
    const from = await heightOf(stem);
    await log(`CHECKPOINT bmm ${stem} ${count}`);
    await waitFor(`${count} blocks of ${stem}`, async () => (await heightOf(stem)) >= from + count, 300000);
  }
  const packCall = (stem, method, params = {}) => T().packs.route(stem, { rid: `sc${Math.random()}`, kind: 'host', method, params });
  const confirmed = (stem, method, params, more = {}) => packCall(stem, 'rpc.confirmed', { method, params, wallet: 'main', ...more });
  function dismissToasts() {
    $$('.toast button[aria-label="Dismiss"]').forEach((b) => b.click());
    if (T().toasts) T().toasts.list.splice(0);
  }

  async function setup() {
    await waitFor('the node to run', () => T() && T().app.phase === 'ready' && T().node.tip, 120000);
    T().savePrefs({ theme: 'midnight', layout: 'dashboard' });
    await rpc('createwallet', ['demo']);
    await waitFor('the wallet', () => T().wallets.current === 'demo');
    await rpc('createwallet', ['friend']);
    const mining = await rpc('getnewaddress', ['Mining'], 'demo');
    await rpc('generatetoaddress', [110, mining]);
    for (const stem of SIDE) await rpc('createsidechainproposal', [SLOTS[stem], NAMES[stem], 'showcase']);
    await rpc('generatetoaddress', [25, mining]);
    const active = (await rpc('listactivesidechains')).map((s) => s.slot);
    await check('four sidechains active', SIDE.every((s) => active.includes(SLOTS[s])), JSON.stringify(active));
    for (const stem of SIDE) await host('sidechains.enable', { stem, wallet: 'separate' });
    for (const stem of SIDE) await waitFor(`${stem} running`, async () => (await chainOf(stem)).ready, 240000);
    for (const stem of SIDE) await bmm(stem, 2);

    // Payments with labels on Chains: from and to a friend, and a few of its own.
    const friendIn = await rpc('getnewaddress', ['Alice'], 'demo');
    const friendOut = await rpc('getnewaddress', ['Rent'], 'friend');
    await rpc('sendtoaddress', [await rpc('getnewaddress', [], 'friend'), 40], 'demo');
    await rpc('generatetoaddress', [1, mining]);
    await rpc('sendtoaddress', [friendIn, 12.5], 'friend');
    await rpc('sendtoaddress', [friendOut, 3.25], 'demo');
    await rpc('setlabel', [friendOut, 'Rent'], 'demo').catch(() => undefined);
    await rpc('generatetoaddress', [2, mining]);

    // Coins into each sidechain, each the chain's own question (answered yes here).
    await desktop({ always: true });
    const amounts = { layers: '20', thunder: '10', bitassets: '6', hivemind: '15' };
    for (const stem of SIDE) {
      const d = await host('chain.deposit', { stem, amount: amounts[stem] });
      await check(`a deposit into ${stem}`, /^[0-9a-f]{64}$/.test(d.txid || ''), JSON.stringify(d));
    }
    await rpc('generatetoaddress', [1, mining]);
    for (const stem of SIDE) await bmm(stem, 2);
    for (const stem of SIDE) await waitFor(`the deposit credited on ${stem}`, async () => Number((await summary(stem)).balances.trusted) >= Number(amounts[stem]) - 1, 180000);

    // An asset on Layers, and one on BitAssets.
    await confirmed('layers', 'reserveasset', ['GOLD'], { then: { method: 'registerasset', params: ['GOLD', 100000, 2] } });
    await confirmed('bitassets', 'reserveasset', ['SILV'], { then: { method: 'registerasset', params: ['SILV', 5000, 0] } });
    await bmm('layers', 4);
    await bmm('bitassets', 4);
    await confirmed('layers', 'registerasset', ['GOLD', 100000, 2], { authorizedOnly: true });
    await confirmed('bitassets', 'registerasset', ['SILV', 5000, 0], { authorizedOnly: true });
    await bmm('layers', 1);
    await bmm('bitassets', 1);
    // A payment on Thunder, one on Layers.
    const tAddr = (await host('chain.receive', { stem: 'thunder', label: 'Savings' })).address;
    await host('chain.send', { stem: 'thunder', address: tAddr, amount: '1.5' });
    await bmm('thunder', 1);
    // A market on Hivemind, traded (by its own CLI, as its pages would).
    await log('CHECKPOINT hivemind trade 0');
    await waitFor('the trades on Hivemind', async () => ((await host('chain.positions', { stem: 'hivemind' })).positions || []).length >= 1, 600000);
    // A withdrawal from Layers on its way back.
    const back = await rpc('getnewaddress', ['Back from Layers'], 'demo');
    await host('chain.withdraw', { stem: 'layers', address: back, amount: '1' });
    await bmm('layers', 1);
    await desktop({ always: false });
    await desktop({ clear: true });
    // A wallet with a long history, as the hub's (about 20 000 records): 40 payments of 500 outputs from the friend to
    // 500 addresses of demo, some labelled.
    const addresses = [];
    for (let i = 0; i < 500; i++) addresses.push(await rpc('getnewaddress', [i % 5 === 0 ? `Customer ${i}` : ''], 'demo'));
    await rpc('sendtoaddress', [await rpc('getnewaddress', [], 'friend'), 200], 'demo');
    await rpc('generatetoaddress', [1, mining]);
    for (let n = 0; n < 40; n++) {
      const outputs = {};
      for (const a of addresses) outputs[a] = 0.0001;
      await rpc('sendmany', ['', outputs], 'friend');
      if (n % 10 === 9) await rpc('generatetoaddress', [1, mining]);
    }
    await rpc('generatetoaddress', [1, mining]);
    // And on Layers: a few hundred payments in its wallet (by its own CLI).
    await log('CHECKPOINT bigside layers 0');
    await waitFor('the long history of Layers', async () => (await host('chain.transactions', { stem: 'layers', count: 1000 })).rows.length >= 300, 300000);
    // More merged-mined blocks: the small rewards of the sidechains' wallets.
    for (let i = 0; i < 3; i++) for (const stem of SIDE) await bmm(stem, 2);
    await rpc('generatetoaddress', [5, mining]);
    await check('setup done', true);
  }

  async function ready() {
    await waitFor('the node to run', () => T() && T().app.phase === 'ready' && T().node.tip, 120000);
    // The wallets made by the setup (made by RPC: not loaded at the start by themselves).
    if (T().wallets.current === null) {
      await rpc('loadwallet', ['demo']).catch(() => undefined);
      await rpc('loadwallet', ['friend']).catch(() => undefined);
    }
    await waitFor('the wallet', () => T().wallets.current !== null, 60000);
    if (T().wallets.current !== 'demo') T().savePrefs({ lastWallet: 'demo' });
    for (const stem of SIDE) await waitFor(`${stem} running`, async () => (await chainOf(stem))?.ready, 300000);
    T().savePrefs({ theme: 'midnight', layout: 'dashboard', chain: 'all' });
    if (S.maximize) await host('window.toggleMaximize').catch(() => undefined);
    await sleep(3000);
  }

  // The pages measured idle, in order: Home first (the budget), then each page of the app.
  const PAGES = [
    ['home', ''],
    ['overview', ''],
    ['send', ''],
    ['receive', ''],
    ['transactions', ''],
    ['wallets', ''],
    ['sidechains', ''],
    ['explorer', ''],
    ['settings', ''],
  ];

  async function perf() {
    const seconds = S.perfSeconds || 60;
    // The first minute after the start, as it is: the sidechains' pages loading, the nodes catching up.
    await log(`CHECKPOINT idle startup 45`);
    await sleep(48000);
    if (S.busy) {
      go('home');
      await sleep(5000);
      // As on a busy network: a payment every 2 s, a block every 10 s (the page makes them: the runner measures).
      const to = await rpc('getnewaddress', [], 'friend');
      const mining = await rpc('getnewaddress', [], 'demo');
      let stop = false;
      const busy = (async () => {
        let n = 0;
        while (!stop) {
          await rpc('sendtoaddress', [to, 0.01], 'demo').catch(() => undefined);
          if (++n % 5 === 0) await rpc('generatetoaddress', [1, mining]).catch(() => undefined);
          await sleep(2000);
        }
        return n;
      })();
      await log(`CHECKPOINT idle busy-home ${seconds}`);
      await sleep(1000 * (seconds + 4));
      stop = true;
      await log(`busy: ${await busy} payments`);
      // Catching up: blocks as fast as the node takes them (10 at a time), as after a restart.
      let stop2 = false;
      const catchup = (async () => {
        let n = 0;
        while (!stop2) {
          await rpc('generatetoaddress', [10, mining]).catch(() => undefined);
          n += 10;
          await sleep(500);
        }
        return n;
      })();
      await log(`CHECKPOINT idle catchup-home ${Math.min(seconds, 30)}`);
      await sleep(1000 * (Math.min(seconds, 30) + 4));
      stop2 = true;
      await log(`catchup: ${await catchup} blocks`);
      await sleep(5000);
    }
    const pages = S.pages && S.pages.length ? PAGES.filter(([p]) => S.pages.includes(p)) : PAGES;
    for (const [page, tab] of pages) {
      go(page, tab);
      // Let the page load what it shows, then nothing happens: what it costs now is what it costs idle.
      await sleep(8000);
      dismissToasts();
      await sleep(2000);
      await log(`CHECKPOINT idle ${page} ${page === 'home' ? seconds : Math.min(seconds, 30)}`);
      await sleep(1000 * ((page === 'home' ? seconds : Math.min(seconds, 30)) + 3));
    }
    await check('measured', true);
  }

  // ---- Pictures of every page: each mode and look of the matrix, at the size of the run (the window, or maximized) ----
  const select = async (id) => {
    T().savePrefs({ chain: id });
    await sleep(400);
  };
  async function settle(ms = 2500) {
    await sleep(ms);
    dismissToasts();
    await sleep(300);
  }
  function scrollTop() {
    const box = $('#content');
    if (box) box.scrollTop = 0;
  }
  async function shot(name) {
    dismissToasts();
    scrollTop();
    await snapshot(name);
  }
  async function shots() {
    const size = S.maximize ? 'wide' : '1180';
    for (const entry of S.matrix) {
      const [mode, look] = entry.split(':');
      T().savePrefs({ uiMode: mode, theme: look, layout: 'dashboard', sidebarAllPages: false });
      const tag = `${mode}-${look}-${size}`;
      await sleep(800);
      const pages = [
        ['home', '', 'all', 'home-all'],
        ['home', '', 'chains', 'home-chains'],
        ['home', '', 'layers', 'home-layers'],
        ['overview', '', 'all', 'overview-all'],
        ['overview', '', 'layers', 'overview-layers'],
        ['send', '', 'all', 'send-all'],
        ['send', '', 'layers', 'send-layers'],
        ['receive', '', 'thunder', 'receive-thunder'],
        ['transactions', '', 'all', 'transactions-all'],
        ['wallets', '', 'all', 'wallets-all'],
        ['sidechains', '', 'all', 'sidechains-all'],
        ['sidechains', '', 'layers', 'sidechains-layers'],
        ['explorer', '', 'all', 'explorer-all'],
        ['explorer', '', 'chains', 'explorer-chains'],
        ['explorer', '', 'layers', 'explorer-layers'],
        ['staking', '', 'all', 'staking-all'],
        ['settings', 'sidechains', 'all', 'settings-sidechains'],
      ];
      for (const [page, tab, chain, name] of pages) {
        if (S.pages && S.pages.length && !S.pages.includes(name) && !S.pages.includes(page)) continue;
        await select(chain);
        go(page, tab);
        await settle(page === 'explorer' && chain === 'layers' ? 6000 : 3000);
        await shot(`${name}-${tag}`);
      }
      // The pictures that are not a page: each when asked for by name (or with every page).
      const want = (name) => !(S.pages && S.pages.length) || S.pages.includes(name);
      // The explorer's block of Chains: its drivechain tables.
      if (want('explorer-chains-block')) {
        await select('chains');
        go('explorer', `block:${await rpc('getblockhash', [(await rpc('getblockcount')) - 2])}`);
        await settle(3000);
        await shot(`explorer-chains-block-${tag}`);
      }
      // Move coins with "Choose coins" open, the coins of Chains' wallet.
      if (want('sidechains-choose-coins')) {
        await select('all');
        go('sidechains');
        await settle(2500);
        $('[data-choose-coins]')?.click();
        await settle(2000);
        await shot(`sidechains-choose-coins-${tag}`);
      }
      // The sidebar: each chain's own pages; with the debugging option, every page.
      await select('all');
      go('home');
      await settle(1500);
      const sideScroll = $('.sidebar .scroll');
      const group = $('[data-chain-group="layers"]');
      if (sideScroll && group) sideScroll.scrollTop = group.offsetTop - 120;
      else if (sideScroll) sideScroll.scrollTop = sideScroll.scrollHeight;
      if (want('sidebar-groups')) await shot(`sidebar-groups-${tag}`);
      if (sideScroll) sideScroll.scrollTop = 0;
      if (mode === 'advanced' && want('sidebar-all-pages')) {
        T().savePrefs({ sidebarAllPages: true });
        await settle(1000);
        $('[data-chain-item="layers:own"]')?.click();
        await settle(5000);
        const g2 = $('[data-chain-group="layers"]');
        if (sideScroll && g2) sideScroll.scrollTop = g2.offsetTop - 120;
        await shot(`sidebar-all-pages-${tag}`);
        T().savePrefs({ sidebarAllPages: false });
        // A chain's own page from its group: Layers' Trade, in the app.
        $('[data-chain-item="layers:trade"]')?.click();
        await settle(5000);
        await shot(`group-layers-trade-${tag}`);
        T().go('home');
        await settle(1000);
        if (sideScroll) sideScroll.scrollTop = 0;
      }
    }
    await check('pictures', true);
  }

  try {
    if (S.phase === 'setup') {
      await setup();
    } else {
      await ready();
      if (S.perf) await perf();
      if (S.matrix && S.matrix.length) await shots();
    }
    await finish();
  } catch (e) {
    await finish(e);
  }
})();
