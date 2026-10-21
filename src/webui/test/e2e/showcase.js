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
  // The sidechains turned on: the four of the showcase, or the hub's three (--scale hub: Layers, BitAssets, Hivemind).
  const SIDE = S.sidechains && S.sidechains.length ? S.sidechains : ['layers', 'thunder', 'bitassets', 'hivemind'];
  const HUB = S.scale === 'hub';
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
    await check(`${SIDE.length} sidechains active`, SIDE.every((s) => active.includes(SLOTS[s])), JSON.stringify(active));
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
    // At the hub's scale: enough for thousands of payments, 100 assets with logos and 90 markets.
    const amounts = HUB ? { layers: '60', bitassets: '40', hivemind: '400' } : { layers: '20', thunder: '10', bitassets: '6', hivemind: '15' };
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
    // A payment on Thunder.
    if (SIDE.includes('thunder')) {
      const tAddr = (await host('chain.receive', { stem: 'thunder', label: 'Savings' })).address;
      await host('chain.send', { stem: 'thunder', address: tAddr, amount: '1.5' });
      await bmm('thunder', 1);
    }
    // A market on Hivemind, traded (by its own CLI, as its pages would).
    await log('CHECKPOINT hivemind trade 0');
    await waitFor('the trades on Hivemind', async () => ((await host('chain.positions', { stem: 'hivemind' })).positions || []).length >= 1, 600000);
    // A withdrawal from Layers on its way back.
    const back = await rpc('getnewaddress', ['Back from Layers'], 'demo');
    await host('chain.withdraw', { stem: 'layers', address: back, amount: '1' });
    await bmm('layers', 1);
    await desktop({ always: false });
    await desktop({ clear: true });
    if (HUB) {
      // The hub's sizes (hubscale.py, made by the runner through each node's RPC): 25 000 transactions in Chains'
      // wallet, thousands in the sidechains', 98 assets with 54 logos, 90 markets. The page waits on a quiet page.
      go('settings');
      await log('CHECKPOINT hubscale 0');
      // The runner labels an address "hubscale-done" when it is done (whatever it could make).
      await waitFor("the hub's history in Chains' wallet", async () => Object.keys(await rpc('getaddressesbylabel', ['hubscale-done'], 'demo')).length > 0, 3600000);
      const made = (await rpc('getwalletinfo', [], 'demo')).txcount;
      await check("the hub's history in Chains' wallet", made >= 25000 * (S.hubScale || 1), `${made} transactions`);
      go('home');
      await check('setup done', true);
      return;
    }
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
    // A state made hours ago has an old tip: the node would say it is catching up (its dialog over every picture). A
    // block now (on this run's copy of the state) makes it up to date again.
    const tipTime = (await rpc('getblockheader', [await rpc('getbestblockhash')])).time;
    if (Date.now() / 1000 - tipTime > 1800) {
      await rpc('generatetoaddress', [1, await rpc('getnewaddress', [], 'demo')]);
      await waitFor('up to date', () => T().node.tip && !T().node.tip.ibd, 60000).catch(() => undefined);
      await sleep(2000);
    }
    T().savePrefs({ theme: 'midnight', layout: 'dashboard', chain: 'all' });
    // As the hub has it (its chains-app.json): Catppuccin Mocha, the text two steps larger, every block in the
    // explorer, ten a page, coin control on.
    if (HUB) T().savePrefs({ theme: 'mocha', fontAdjust: 2, explorerCount: 0, explorerPageSize: 10, coinControl: true });
    if (S.maximize) await host('window.toggleMaximize').catch(() => undefined);
    await sleep(3000);
  }

  // The pages measured idle, in order: Home first (the budget), then each page of the app; with --scale hub also the
  // sidechains' own pages the hub shows most (a group's item of the sidebar: "<stem>:<page>").
  const PAGES = [
    ['home', ''],
    ['overview', ''],
    ['send', ''],
    ['receive', ''],
    ['transactions', ''],
    ['wallets', ''],
    ['sidechains', ''],
    ['staking', ''],
    ['explorer', ''],
    ['settings', ''],
    ...(HUB ? [['hivemind:markets', ''], ['hivemind:voting', ''], ['bitassets:assets', ''], ['layers:my-assets', '']] : []),
  ];

  // What changes in the page while it is measured (S.probe): the changes of the document by the part of the page they are
  // in (each is a style, a layout and a frame drawn), and how late the page's timers run (its main thread busy).
  function region(node) {
    let el = node && node.nodeType === 1 ? node : node && node.parentElement;
    const parts = [];
    for (let depth = 0; el && el !== document.body && depth < 14; depth++, el = el.parentElement) {
      const data = [...el.attributes].find((a) => a.name.startsWith('data-') && a.name !== 'data-theme');
      const cls = typeof el.className === 'string' ? el.className.split(/\s+/).filter((c) => c && !c.startsWith('svelte-'))[0] : '';
      if (data || el.id || ['ASIDE', 'MAIN', 'FOOTER', 'HEADER', 'NAV', 'SECTION', 'TABLE'].includes(el.tagName)) {
        parts.push(`${el.tagName.toLowerCase()}${el.id ? '#' + el.id : ''}${cls ? '.' + cls : ''}${data ? `[${data.name}]` : ''}`);
        if (parts.length >= 2) break;
      }
    }
    return parts.reverse().join(' > ') || '(body)';
  }
  function startProbe() {
    const counts = new Map();
    let total = 0;
    const observer = new MutationObserver((records) => {
      const what = (n) => (n && n.nodeType === 1 ? `${n.tagName.toLowerCase()}${typeof n.className === 'string' && n.className ? '.' + n.className.split(/\s+/).filter((c) => c && !c.startsWith('svelte-'))[0] : ''}` : n && n.nodeType === 3 ? '#text' : '?');
      for (const r of records) {
        total++;
        let detail = what(r.target);
        if (r.type === 'attributes') detail += `@${r.attributeName}`;
        if (r.type === 'childList') detail += ` +${[...r.addedNodes].map(what).join(',')} -${[...r.removedNodes].map(what).join(',')}`;
        const key = `${r.type}: ${region(r.target)} :: ${detail}`;
        counts.set(key, (counts.get(key) || 0) + 1);
      }
    });
    observer.observe(document.body, { subtree: true, childList: true, attributes: true, characterData: true });
    let late = 0;
    let ticks = 0;
    let last = performance.now();
    const timer = setInterval(() => {
      const now = performance.now();
      late += Math.max(0, now - last - 100);
      ticks++;
      last = now;
    }, 100);
    return () => {
      observer.disconnect();
      clearInterval(timer);
      const top = [...counts.entries()].sort((a, b) => b[1] - a[1]).slice(0, 12);
      return { mutations: total, lateMs: Math.round(late), ticks, top };
    };
  }
  async function measured(kind, name, seconds) {
    const stop = S.probe ? startProbe() : null;
    await log(`CHECKPOINT ${kind} ${name} ${seconds}`);
    await sleep(1000 * (seconds + 3));
    if (stop) await log(`PROBE ${kind}:${name} ${JSON.stringify(stop())}`);
  }

  /** Show a page: one of the app's, or a sidechain's own (its item in its group of the sidebar). */
  async function show(page, tab = '') {
    const own = /^([a-z0-9]+):([a-z0-9-]+)$/.exec(page);
    if (!own) {
      go(page, tab);
      return;
    }
    T().savePrefs({ chain: 'all' });
    go('home');
    await sleep(300);
    const item = $(`[data-chain-item="${own[1]}:${own[2]}"]`);
    if (item) item.click();
    else await log(`no item ${page} in the sidebar`);
  }

  async function perf() {
    const seconds = S.perfSeconds || 60;
    const short = Math.min(seconds, 30);
    // The first minute after the start, as it is: the sidechains' pages loading, the nodes catching up.
    await measured('idle', 'startup', 45);
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
      await log(`CHECKPOINT idle catchup-home ${short}`);
      await sleep(1000 * (short + 4));
      stop2 = true;
      await log(`catchup: ${await catchup} blocks`);
      await sleep(5000);
    }
    const pages = S.pages && S.pages.length ? PAGES.filter(([p]) => S.pages.includes(p)) : PAGES;
    for (const [page, tab] of pages) {
      await show(page, tab);
      // Let the page load what it shows, then nothing happens: what it costs now is what it costs idle.
      await sleep(8000);
      dismissToasts();
      await sleep(2000);
      await measured('idle', page, page === 'home' ? seconds : short);
    }
    // What one change of the page costs (S.tickTest): a word of the status bar changed once a second on a page left
    // alone, the rest still: the cost of a frame of this window, its size and its way of drawing.
    if (S.tickTest) {
      go('home');
      await sleep(8000);
      dismissToasts();
      const bar = $('footer.statusbar') || document.body;
      const tick = document.createElement('span');
      tick.id = 'perf-tick';
      tick.textContent = '0';
      bar.appendChild(tick);
      let n = 0;
      const timer = setInterval(() => (tick.textContent = String(++n % 10)), 1000);
      await measured('idle', 'tick-1s', 30);
      clearInterval(timer);
      tick.remove();
    }
    // As on the hub (--steady): a block of Chains a minute, each sidechain merged-mined in it, a payment on a sidechain
    // every few seconds (the runner makes them, while it measures), on each page asked.
    for (const page of S.steady || []) {
      await show(page);
      await sleep(8000);
      dismissToasts();
      await measured('steady', page, S.steadySeconds || 180);
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
      // The explorer with every chain: a strip of boxes each (§21.4), then a block of Chains opened under them, then one of
      // the first sidechain; and Chains alone, its strip above the block it opened.
      if (want('explorer-strips')) {
        await select('all');
        go('explorer');
        await settle(5000);
        await shot(`explorer-strips-${tag}`);
        const chainsBox = $$('[data-strip="chains"] button[data-strip-block]').find((b) => b.dataset.stripBlock !== 'next');
        if (chainsBox) {
          chainsBox.click();
          await settle(3000);
          $('[data-strip-detail]')?.scrollIntoView({ block: 'start' });
          await sleep(300);
          await snapshot(`explorer-strips-block-chains-${tag}`);
        }
        const sideBox = $$(`[data-strip="${SIDE[0]}"] button[data-strip-block]`).find((b) => b.dataset.stripBlock !== 'next');
        if (sideBox) {
          scrollTop();
          sideBox.click();
          await settle(6000);
          $('[data-strip-detail]')?.scrollIntoView({ block: 'start' });
          await sleep(300);
          await snapshot(`explorer-strips-block-${SIDE[0]}-${tag}`);
        }
        await select('chains');
        go('explorer');
        await settle(4000);
        const box = $('.ribbon button.blk');
        if (box) {
          box.click();
          await settle(3000);
          await shot(`explorer-one-chain-block-${tag}`);
        }
      }
      // Hivemind's group: Markets and its Create tab, the Voting center and its Votecoin tab (§21.1).
      if (SIDE.includes('hivemind') && want('hivemind-pages')) {
        await select('all');
        go('home');
        await settle(1000);
        const groupInView = () => {
          const box = $('.sidebar .scroll');
          const g = $('[data-chain-group="hivemind"]');
          if (box && g) box.scrollTop = g.offsetTop - 80;
        };
        for (const [item, name, then] of [
          ['markets', 'hivemind-markets', null],
          ['create', 'hivemind-create', null],
          ['voting', 'hivemind-voting', null],
          ['voting', 'hivemind-votecoin', 'hm.transfer'],
        ]) {
          const el = $(`[data-chain-item="hivemind:${item}"]`);
          if (!el) continue;
          el.click();
          if (then) T().buildCommands().find((c) => c.id === `module.hivemind.cmd.${then}`)?.run();
          await settle(5000);
          groupInView();
          await snapshot(`${name}-${tag}`);
        }
      }
      // Staking: the first chain that stakes, its whole page under the app's title (§21.2).
      if (want('staking-chain')) {
        await select('all');
        go('staking');
        await settle(6000);
        await shot(`staking-chain-${tag}`);
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
