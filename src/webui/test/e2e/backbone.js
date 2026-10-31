// Backbone's sidechains (doc/unified-app.md §26, P1), as a user turns them on: Backbone alone first (no sidechain
// runs); slots 46 and 9 active on the regtest of eCash's rules; Flush and Thunder downloaded from their latest
// releases (a mirror of the runner's), checked by their sums; Flush turned on with its pages from its release (the
// asset of its pages), coins moved into it, its DEX the Trade page's; Thunder, a drop-in, turned on (Backbone's gRPC
// served late for it), its pages from its package, its wallet in its own pages; both turned off. The runner
// (run_backbone.py) checks the processes at each CHECKPOINT and merges-mines Flush's blocks at "CHECKPOINT bmm".
(async () => {
  const { host, rpc, sleep, log, T, waitFor, check, snapshot, finish } = E2E;
  const $ = (selector, root = document) => root.querySelector(selector);
  const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];
  const desktop = (answer) => host('test.confirmAnswer', answer);
  const TAGS = window.__E2E_TAGS;
  const list = () => host('sidechains.list');
  const chainOf = async (stem) => (await list()).chains.find((c) => c.stem === stem);
  async function checkpoint(name, ms = 3000) {
    await log(`CHECKPOINT ${name}`);
    await sleep(ms);
  }
  async function shot(name) {
    $$('button[aria-label="Dismiss"]').forEach((b) => b.click());
    if (T().toasts) T().toasts.list.splice(0);
    await sleep(400);
    await snapshot(name);
  }
  const heightOf = (stem) => host('sidechains.rpc', { stem, method: 'getblockcount' });
  async function bmm(count) {
    const from = await heightOf('flush');
    await log(`CHECKPOINT bmm flush ${count}`);
    await waitFor(`${count} blocks of Flush`, async () => (await heightOf('flush')) >= from + count, 240000);
  }
  async function install(stem, tag) {
    const releases = await host('modules.releases', { stem, force: true });
    await check(`${stem}: its latest release, from its own repository`, releases.repo === `block-fabric/backbone-${stem}` && releases.releases.length === 1 && releases.releases[0].tag === tag, JSON.stringify(releases.releases.map((r) => [r.tag, r.reason])));
    await check(`${stem}: checked by the release's SHA256SUMS (no release key yet: unsigned)`, releases.releases[0].compatible && releases.releases[0].unsigned, releases.releases[0].reason);
    await host('modules.install', { stem, tag });
    await waitFor(`${stem} downloaded`, async () => {
      const p = await host('modules.progress', { stem });
      if (p.state === 'failed') throw new Error(p.error);
      return p.state === 'done' && !p.running;
    }, 300000);
    const on = await chainOf(stem);
    await check(`${stem}: its program is the one downloaded`, on.programSource === 'download' && on.program.includes(`/sidechains/bin/${stem}/${tag}/`), on.program);
  }
  const running = async (stem) => {
    const c = await chainOf(stem);
    if (c.state === 'failed') throw new Error(c.error);
    return c.state === 'running' && c.ready;
  };

  try {
    await waitFor('the node to run', () => T() && T().app.phase === 'ready' && T().node.tip, 120000);
    T().savePrefs({ theme: 'midnight', layout: 'dashboard' });

    // 1. Backbone alone: its sidechains listed, none on, no program yet (none is bundled).
    const first = await list();
    await check('the catalog of eCash: Flush and Thunder', first.chains.map((c) => `${c.stem}:${c.kind}:${c.slot}`).join(',') === 'flush:template:46,thunder:dropin:9', JSON.stringify(first.chains.map((c) => [c.stem, c.kind, c.slot])));
    await check('none on, no RPC server for them', first.chains.every((c) => !c.enabled && c.pid === 0) && first.rpcServer === false);
    await check('no program on this computer before a download', first.chains.every((c) => c.programSource === 'none'), JSON.stringify(first.chains.map((c) => c.programSource)));
    await checkpoint('standalone');

    // Slots 46 and 9 active (one proposal a block, as eCash's rules say).
    await rpc('createwallet', ['demo']);
    await waitFor('the wallet', () => T().wallets.current === 'demo');
    const mining = await rpc('getnewaddress', ['Mining'], 'demo');
    await rpc('generatetoaddress', [110, mining]);
    await rpc('createsidechainproposal', [46, 'Flush', 'Trade, launch and stake assets']);
    await rpc('generatetoaddress', [1, mining]);
    await rpc('createsidechainproposal', [9, 'Thunder', 'Large blocks']);
    await rpc('generatetoaddress', [10, mining]);
    const active = (await rpc('listactivesidechains')).map((s) => s.slot);
    await check('slots 46 and 9 active on the mainchain', active.includes(46) && active.includes(9), JSON.stringify(active));

    T().go('settings', 'sidechains');
    await waitFor('the sidechains of Settings', () => $$('.card[data-stem]').length === 2, 20000);
    await shot('backbone-settings-sidechains');

    // 2. Flush: downloaded from its latest release, turned on with its own wallet, its pages from its release.
    await install('flush', TAGS.flush);
    await host('sidechains.enable', { stem: 'flush', wallet: 'separate' });
    await waitFor('Flush running', () => running('flush'), 180000);
    await checkpoint('flush');
    const flushPages = await host('pack.info', { stem: 'flush' });
    await check('Flush\'s pages: from its release, of the host its node speaks', flushPages.compatible === true && flushPages.pack && flushPages.pack.source === 'release', JSON.stringify(flushPages));
    await bmm(2);
    // Coins in: a deposit, Backbone's own payment asked in its own dialog.
    await desktop({ yes: true });
    const dep = await host('chain.deposit', { stem: 'flush', amount: '5' });
    await check('a deposit into Flush sent', typeof dep.txid === 'string' && dep.txid.length === 64, JSON.stringify(dep));
    await rpc('generatetoaddress', [1, mining]);
    await bmm(3);
    await waitFor('the deposit credited on Flush', async () => {
      const s = await host('chain.summary', { stem: 'flush' });
      return Number(s.balances?.trusted ?? 0) + Number(s.balances?.pending ?? 0) >= 4.9;
    }, 240000);
    const info = await host('chain.dex.info', { stem: 'flush' });
    await check('Flush\'s DEX answers the app (its coin ECX)', info && info.stem === 'flush', JSON.stringify(info).slice(0, 200));
    // Its DEX is the Trade page's: its own Swap in its frame.
    T().go('trade');
    await waitFor('the Trade page on Flush\'s own pages', () => $$('[data-trade-tab]').length === 2, 20000);
    await waitFor('Flush\'s frame', () => $('iframe.module-frame[data-module="flush"]'), 60000).catch(() => null);
    await sleep(4000);
    await shot('backbone-trade-flush');
    T().go('home');
    await sleep(2000);
    await shot('backbone-home-flush');

    // 3. Thunder: a drop-in, its program and pages from its latest release; Backbone's gRPC served for it.
    await install('thunder', TAGS.thunder);
    await host('sidechains.enable', { stem: 'thunder', wallet: 'separate' });
    await waitFor('Thunder running (its node proved it is on its port)', () => running('thunder'), 180000);
    await checkpoint('thunder');
    const thunderPages = await host('pack.info', { stem: 'thunder' });
    await check('Thunder\'s pages: from its release, and its node speaks their host', thunderPages.compatible === true && thunderPages.running === true && thunderPages.pack && thunderPages.pack.source === 'release', JSON.stringify(thunderPages));
    const summary = await host('chain.summary', { stem: 'thunder' });
    await check('Thunder\'s summary by its app layer: its height and its wallet\'s balances', typeof summary.height === 'number' && !!summary.balances && !summary.errors?.height, JSON.stringify(summary));
    const caps = (await host('chain.capabilities')).find((c) => c.stem === 'thunder');
    await check('Thunder is a drop-in: its wallet in its own pages', caps.dropin === true && caps.send === false, JSON.stringify(caps));
    T().go('send');
    await sleep(1500);
    await shot('backbone-send');
    T().go('home');
    await sleep(2000);
    await shot('backbone-home-both');

    // 4. Both off: their programs stop.
    await host('sidechains.disable', { stem: 'thunder' });
    await host('sidechains.disable', { stem: 'flush' });
    await waitFor('both stopped', async () => (await list()).chains.every((c) => c.pid === 0), 180000);
    await checkpoint('disabled');
    await finish();
  } catch (e) {
    await finish(e);
  }
})();
