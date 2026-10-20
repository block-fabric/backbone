// The pages of the sidechains in the unified app (doc/unified-app.md §13), as a user drives them: Layers and Thunder
// turned on; Layers chosen in the switcher shows its own pages in a frame; coins moved into Layers (a deposit made by
// the Chains wallet, asked in Chains' own dialog); a send on Layers, an asset sent and a swap on its DEX, each asked in
// the app's trusted dialog with "On Layers" and its mark, then confirmed in a block of Layers; what the pages may not do
// refused; the same for Thunder. The runner (run_packs.py) merged-mines the blocks of the sidechains when the script
// says "CHECKPOINT bmm <stem> <count>", and checks at "CHECKPOINT tamper <stem>" that the daemon refuses a call changed
// after its question. Pictures in Midnight: the pages, and each trusted dialog.
(async () => {
  const { host, rpc, sleep, log, T, waitFor, check, snapshot, finish } = E2E;
  const $ = (selector, root = document) => root.querySelector(selector);
  const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];
  const desktop = (answer) => host('test.confirmAnswer', answer);
  const lastAsked = async () => (await host('test.confirmed')).at(-1);
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
    await sleep(400);
    await snapshot(name);
  }
  const hubList = () => host('sidechains.list');
  const chainOf = async (stem) => (await hubList()).chains.find((c) => c.stem === stem);
  const frame = () => $('iframe.pack');
  let rid = 1;
  // A request as the pages in the frame make it: the frame's messages, once the page knows which frame sent them, go
  // through route (lib/packs.svelte.ts) for that frame's sidechain. The frame itself is of another origin: the test
  // cannot reach into it, as the pages cannot reach out of it.
  const packs = () => T().packs;
  let shown = 'layers';
  const inFrame = (req) => packs().route(shown, { rid: `e2e${rid++}`, ...req });
  const frameRpc = (method, params = [], wallet) => inFrame({ kind: 'rpc', method, params, ...(wallet ? { wallet } : {}) });
  const frameHost = (method, params = {}) => inFrame({ kind: 'host', method, params });
  const confirmed = (method, params, wallet, more = {}) => frameHost('rpc.confirmed', { method, params, wallet, ...more });
  const navigate = (page, tab = '') => packs().deliver(shown, JSON.stringify([{ type: 'navigate', data: { page, tab } }]));
  async function heightOf(stem) {
    return host('sidechains.rpc', { stem, method: 'getblockcount' });
  }
  /** Blocks of a sidechain, merged mined by the runner (as a miner of Chains would). */
  async function bmm(stem, count) {
    const from = await heightOf(stem);
    await log(`CHECKPOINT bmm ${stem} ${count}`);
    await waitFor(`${count} blocks of ${stem}`, async () => (await heightOf(stem)) >= from + count, 180000);
  }
  async function show(stem, name) {
    $(`.switcher .chain[data-chain="${stem}"]`).click();
    shown = stem;
    await waitFor(`the pages of ${name}`, () => frame() && frame().dataset.stem === stem, 30000);
    // Its pages started: they asked for their state (app.hello, node.state, wallets...) through their messages.
    await waitFor(`${name}: its pages asking their node`, () => (packs().traffic[stem] || 0) >= 6, 60000);
  }
  /** The wallet of a sidechain: what it holds, trusted. */
  const trusted = async (wallet) => (await frameRpc('getbalances', [], wallet)).mine.trusted;

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

    // Both turned on (their wizard is the test of phase 1).
    await host('sidechains.enable', { stem: 'layers', wallet: 'separate' });
    await host('sidechains.enable', { stem: 'thunder', wallet: 'separate' });
    await waitFor('both running', async () => (await chainOf('layers')).ready && (await chainOf('thunder')).ready, 120000);
    const packList = await host('pack.list');
    await check('the pages of all seven sidechains are built in, at pinned commits', ['layers', 'fastblocks', 'thunder', 'bitnames', 'bitassets', 'zside', 'hivemind'].every((s) => packList.some((p) => p.stem === s && /^[0-9a-f]{40}$/.test(p.commit) && p.hostApi === 1)), JSON.stringify(packList));
    const info = await host('pack.info', { stem: 'layers' });
    await check('the node of Layers speaks this version', info.compatible === true && info.hostApi === 1, JSON.stringify(info));
    await bmm('layers', 2);
    await bmm('thunder', 2);

    // 1. Layers: its own pages, in the app.
    await waitFor('the switcher', () => $$('.switcher .chain').length === 3, 30000);
    await show('layers', 'Layers');
    await sleep(2500);
    await shot('phase2-layers-home');
    // The frame is of its own origin: the page of the app cannot reach into it, nor it out to the page.
    let reached = true;
    try {
      void frame().contentWindow.__chains;
    } catch {
      // Reading a global of another origin throws: as it should.
      reached = false;
    }
    const isolated = frame().contentDocument === null && !reached;
    await check('the pages of Layers are of their own origin (no reach into the app, nor from it)', isolated && frame().getAttribute('src').startsWith('chains-pack://layers/') && frame().getAttribute('sandbox') === 'allow-scripts allow-same-origin', frame().getAttribute('src'));
    await check('the app offers the frames no route of its own', window.__chainsPackRoute === undefined);
    const wallets = await frameRpc('listwallets');
    await check('Layers has its wallet', wallets.includes('main'), JSON.stringify(wallets));

    // 2. Coins into Layers: the deposit address from its node, paid by the Chains wallet, asked in Chains' own dialog.
    const dep = await frameRpc('getdepositaddress', [], 'main');
    await desktop({ show: true });
    const deposit = frameHost('peg.deposit', { destination: dep.depositaddress, amount: '20' });
    await inWindow('phase2-dialog-deposit-into-layers', true);
    const depositTx = await deposit;
    let asked = await lastAsked();
    await check('the deposit was asked in Chains\' own dialog (Deposit to a sidechain)', asked && asked.title === 'Deposit to a sidechain' && !asked.chain, JSON.stringify(asked && asked.title));
    await check('the deposit is a transaction of the Chains wallet', depositTx && /^[0-9a-f]{64}$/.test(depositTx.txid), JSON.stringify(depositTx));
    await rpc('generatetoaddress', [1, mining]);
    await bmm('layers', 2);
    await waitFor('the deposit credited on Layers', async () => (await trusted('main')) >= 19, 120000);
    await check('20 CHN on Layers', (await trusted('main')) >= 19);

    // 3. A send on Layers: its question in the app's trusted dialog, "On Layers"; then confirmed in a block of Layers.
    const to = await frameRpc('getnewaddress', [], 'main');
    const prepared = await frameHost('wallet.send.prepare', { wallet: 'main', recipients: [{ address: to, amount: 150000000 }] });
    await desktop({ show: true });
    const sent = frameHost('wallet.send.commit', { wallet: 'main', token: prepared.token });
    await inWindow('phase2-dialog-layers-send', true);
    const send = await sent;
    asked = await lastAsked();
    await check('the send was asked on Layers', asked.chain === 'Layers' && asked.chainLogo === true && asked.rows[0].label === 'Chain' && asked.rows[0].value === 'Layers', JSON.stringify(asked));
    await check('its question names the address and the amount', asked.rows.some((r) => r.value === to && r.kind === 'address') && asked.rows.some((r) => /^1\.50000000 /.test(r.value)), JSON.stringify(asked.rows));
    await check('the send ran once it was accepted', send && send.complete === true && /^[0-9a-f]{64}$/.test(send.txid), JSON.stringify(send));
    await bmm('layers', 1);
    const tx = await frameHost('wallet.transaction', { wallet: 'main', txid: send.txid, idx: 0, address: '' }).catch(() => null);
    const mempool = await frameRpc('getrawmempool');
    await check('the send is confirmed in a block of Layers', !mempool.includes(send.txid) && (!tx || tx.confirmations === undefined || tx.confirmations >= 1), JSON.stringify(tx && tx.confirmations));

    // A no: nothing sent.
    const again = await frameHost('wallet.send.prepare', { wallet: 'main', recipients: [{ address: to, amount: 10000000 }] });
    await desktop({ yes: false });
    const declined = await frameHost('wallet.send.commit', { wallet: 'main', token: again.token }).then(() => null, (e) => e);
    await check('declined in the dialog: nothing sent', declined && declined.code === -32800, JSON.stringify(declined));

    // 4. An asset: reserved and registered in one question, sent (its question shown), pooled and swapped on the DEX.
    await desktop({ always: true });
    const reserved = await confirmed('reserveasset', ['GOLD'], 'main', { then: { method: 'registerasset', params: ['GOLD', 100000, 2] } });
    await check('the reservation, with its registration authorized once', reserved && reserved.authorized > 0, JSON.stringify(reserved));
    await bmm('layers', 4);
    await confirmed('registerasset', ['GOLD', 100000, 2], 'main', { authorizedOnly: true });
    await bmm('layers', 1);
    await confirmed('addliquidity', ['GOLD', 10000, 'CHN', 2], 'main');
    await bmm('layers', 1);
    await desktop({ always: false });
    await desktop({ clear: true });
    await desktop({ show: true });
    const assetSend = confirmed('sendasset', [to, 'GOLD', 25], 'main');
    await inWindow('phase2-dialog-layers-asset-transfer', true);
    await assetSend;
    await bmm('layers', 1);
    asked = await lastAsked();
    await check('the asset transfer was asked on Layers with its destination', asked.chain === 'Layers' && asked.title === 'Send an asset' && asked.rows.some((r) => r.value === to), JSON.stringify(asked));
    await desktop({ show: true });
    const swap = confirmed('swapasset', ['GOLD', 100, 'CHN', 1, 10, '0.001'], 'main');
    await inWindow('phase2-dialog-layers-dex-swap', true);
    await swap;
    asked = await lastAsked();
    await check('the swap was asked on Layers', asked.chain === 'Layers' && asked.title === 'Swap', JSON.stringify(asked && asked.title));
    await bmm('layers', 1);

    // 5. What the pages may not do: an RPC method that spends (the pages' own policy, in the daemon), a request of
    // the tests, a method of the whole app. And a call changed after its question (the runner, at the daemon).
    const refused = await frameRpc('sendtoaddress', [to, 1], 'main').then(() => null, (e) => e);
    await check('the pages cannot spend by an RPC method', refused && refused.code === -32601, JSON.stringify(refused));
    const testrpc = await packs().route('layers', { kind: 'testrpc', method: 'stop', params: [] }).then(() => null, (e) => e);
    await check('nor make a request of the tests', testrpc && testrpc.code === -32600, JSON.stringify(testrpc));
    const quit = await frameHost('app.quit').then(() => null, (e) => e);
    await check('nor quit the app', quit && quit.code === -32601, JSON.stringify(quit));
    await log('CHECKPOINT tamper layers 0');
    await sleep(3000);

    // Pictures of the pages of Layers in the app.
    for (const [page, tab, name] of [['send', '', 'phase2-layers-send'], ['my-assets', '', 'phase2-layers-my-assets'], ['trade', 'swap', 'phase2-layers-trade'], ['staking', '', 'phase2-layers-staking'], ['transactions', '', 'phase2-layers-transactions']]) {
      navigate(page, tab);
      await sleep(2500);
      await shot(name);
    }

    // 6. Thunder: its pages; a deposit; a send asked "On Thunder".
    await show('thunder', 'Thunder');
    await sleep(2000);
    await shot('phase2-thunder-home');
    const tdep = await frameRpc('getdepositaddress', [], 'main');
    await desktop({ yes: true });
    await frameHost('peg.deposit', { destination: tdep.depositaddress, amount: '10' });
    await rpc('generatetoaddress', [1, mining]);
    await bmm('thunder', 2);
    await waitFor('the deposit credited on Thunder', async () => (await trusted('main')) >= 9, 120000);
    const tto = await frameRpc('getnewaddress', [], 'main');
    const tprep = await frameHost('wallet.send.prepare', { wallet: 'main', recipients: [{ address: tto, amount: 250000000 }] });
    await desktop({ show: true });
    const tsent = frameHost('wallet.send.commit', { wallet: 'main', token: tprep.token });
    await inWindow('phase2-dialog-thunder-send', true);
    const tsend = await tsent;
    asked = await lastAsked();
    await check('the Thunder send was asked on Thunder', asked.chain === 'Thunder' && asked.rows.some((r) => r.value === tto), JSON.stringify(asked));
    await bmm('thunder', 1);
    await check('the Thunder send is confirmed', !(await frameRpc('getrawmempool')).includes(tsend.txid));
    // A deposit address of another sidechain is refused by the pages of this one.
    const wrongDeposit = await frameHost('peg.deposit', { destination: dep.depositaddress, amount: '1' }).then(() => null, (e) => e);
    await check('the pages of Thunder cannot pay into Layers', wrongDeposit && /slot 7/.test(wrongDeposit.message), JSON.stringify(wrongDeposit));

    // Off again: nothing left running.
    $('.switcher .chain[data-chain="chains"]').click();
    await host('sidechains.disable', { stem: 'layers' });
    await host('sidechains.disable', { stem: 'thunder' });
    await waitFor('both stopped', async () => (await hubList()).chains.every((c) => c.pid === 0), 180000);
    await finish();
  } catch (e) {
    await finish(e instanceof Error ? e : new Error(JSON.stringify(e)));
  }
})();
