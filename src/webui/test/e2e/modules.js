// The sidechains as modules of one window, and their programs downloaded (doc/unified-app.md §14, §15), as a user
// drives them. Layers is not on this computer: its enable wizard downloads it from the releases of its repository (a
// local mirror of them, served by the runner over HTTPS): the newest release it can use proposed, an older one without
// a manifest refused with why; an older compatible release chosen, downloaded with its progress, checked, installed.
// Then Layers is a module: in the switcher, its pages in the app's sidebar, its page in the main area with no chrome of
// its own; coins moved in, a send asked in the trusted dialog; back to the app. An update found, approved: stopped,
// replaced, started. Pictures in Midnight. The runner (run_modules.py) merged-mines Layers' blocks at
// "CHECKPOINT bmm layers <count>".
(async () => {
  const { host, rpc, sleep, log, T, waitFor, check, snapshot, finish } = E2E;
  const $ = (selector, root = document) => root.querySelector(selector);
  const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];
  const desktop = (answer) => host('test.confirmAnswer', answer);
  const lastAsked = async () => (await host('test.confirmed')).at(-1);
  const dialogWindow = (action, more = {}) => host('test.dialog', { action, ...more });
  const OLD = window.__E2E_TAGS.old;
  const T1 = window.__E2E_TAGS.first;
  const T2 = window.__E2E_TAGS.second;
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
    $$('button[aria-label="Dismiss"]').forEach((b) => b.click());
    if (T().toasts) T().toasts.list.splice(0);
    await sleep(400);
    await snapshot(name);
  }
  const click = (el, what) => {
    if (!el) throw new Error(`no ${what}`);
    el.click();
  };
  const chainOf = async (stem) => (await host('sidechains.list')).chains.find((c) => c.stem === stem);
  const packs = () => T().packs;
  let rid = 1;
  const inFrame = (req) => packs().route('layers', { rid: `e2e${rid++}`, ...req });
  const frameRpc = (method, params = [], wallet) => inFrame({ kind: 'rpc', method, params, ...(wallet ? { wallet } : {}) });
  const frameHost = (method, params = {}) => inFrame({ kind: 'host', method, params });
  const heightOf = (stem) => host('sidechains.rpc', { stem, method: 'getblockcount' });
  async function bmm(stem, count) {
    const from = await heightOf(stem);
    await log(`CHECKPOINT bmm ${stem} ${count}`);
    await waitFor(`${count} blocks of ${stem}`, async () => (await heightOf(stem)) >= from + count, 180000);
  }
  const modulePage = (id) => $(`[data-module-page="${id}"]`);
  async function openPage(id, name) {
    click(modulePage(id), `the ${id} entry of the sidebar`);
    await waitFor(`${name}: the app at it`, () => T().modules.routes.layers?.page === id, 10000);
    await sleep(2500);
  }

  try {
    await waitFor('the node to run', () => T() && T().app.phase === 'ready' && T().node.tip, 120000);
    T().savePrefs({ theme: 'midnight', layout: 'dashboard' });

    await rpc('createwallet', ['demo']);
    await waitFor('the wallet', () => T().wallets.current === 'demo');
    const mining = await rpc('getnewaddress', ['Mining'], 'demo');
    await rpc('generatetoaddress', [110, mining]);
    await rpc('createsidechainproposal', [7, 'Layers', 'Assets on a fast chain']);
    await rpc('generatetoaddress', [25, mining]);
    await check('Layers active on the mainchain', (await rpc('listactivesidechains')).some((s) => s.slot === 7));

    // 1. Not on this computer: the enable wizard downloads it.
    const before = await chainOf('layers');
    await check('the program of Layers is not on this computer before', !before.program, JSON.stringify(before.program));
    T().go('settings', 'sidechains');
    await waitFor('Settings → Sidechains', () => $('.card[data-stem="layers"] input[type="checkbox"]'), 20000);
    click($('.card[data-stem="layers"] input[type="checkbox"]'), 'the switch of Layers');
    await waitFor('the enable wizard', () => $('[data-wizard="enable"]'), 10000);
    click($('[data-action="next"]'), 'Next');
    await waitFor('the program step', () => $('[data-wizard="enable"]').dataset.step === 'program', 10000);
    await waitFor('the releases of the mirror', () => $(`[data-release="${OLD}"]`) && $(`[data-release="${T2}"]`), 60000);
    await check('Next waits for the program', $('[data-action="next"]').disabled === true);
    const old = $(`[data-release="${OLD}"]`);
    await check('a release made before this app (no manifest) is refused, and says why', old.dataset.compatible === 'no' && old.querySelector('input').disabled && old.textContent.length > 40, old.textContent.trim());
    await check('the newest release it can use is proposed', $(`[data-release="${T2}"] input`).checked === true);
    await check('a signed release says so', /signed/.test($(`[data-release="${T2}"]`).textContent));
    await shot('modules-wizard-download');
    // The older compatible one, to have an update to find after.
    click($(`[data-release="${T1}"] input`), `release ${T1}`);
    await sleep(200);
    click($('[data-action="download"]'), 'Download');
    await waitFor('the download under way', () => $('[data-progress]') && $('[data-progress]').dataset.progress !== 'idle', 30000);
    if ($('[data-progress="downloading"]')) await shot('modules-wizard-downloading');
    await waitFor('installed', () => $('[data-progress="done"]'), 180000);
    await waitFor('its program known', () => $('[data-module-program="layers"]')?.dataset.source === 'download', 30000);
    const rel = await host('modules.releases', { stem: 'layers' });
    await check(`release ${T1} installed and in use`, rel.current === T1 && rel.source === 'download', JSON.stringify({ current: rel.current, source: rel.source }));
    await shot('modules-wizard-installed');
    await waitFor('Next allowed', () => !$('[data-action="next"]').disabled, 10000);
    click($('[data-action="next"]'), 'Next');
    while ($('[data-wizard="enable"]').dataset.step !== 'review') {
      click($('[data-action="next"]'), 'Next');
      await sleep(150);
    }
    // Its own wallet (the single seed comes in phase 3).
    click($('[data-action="turn-on"]'), 'Turn it on');
    await waitFor('turned on', () => $('[data-wizard="enable"]')?.dataset.step === 'done', 60000);
    const on = await chainOf('layers');
    await check('its node runs the downloaded program', on.program.includes(`/sidechains/bin/layers/${T1}/`), on.program);
    click($$('dialog[open] button').find((b) => b.textContent.trim() === 'Close'), 'Close');
    await waitFor('running', async () => (await chainOf('layers')).ready, 120000);
    await bmm('layers', 2);

    // 2. A module of the app: in the switcher; its pages in the app's sidebar; its page in the main area.
    await waitFor('the module switcher', () => $$('.switcher .chain').length === 2, 30000);
    T().go('home');
    await sleep(1500);
    await shot('modules-home-switcher');
    click($('.switcher .chain[data-chain="layers"]'), 'Layers in the switcher');
    await waitFor('its frame shown', () => $('iframe.module-frame[data-module="layers"]') && !$('iframe.module-frame[data-module="layers"]').hidden, 60000);
    await waitFor('its pages in the sidebar (its manifest)', () => $$('[data-module-page]').length >= 6, 60000);
    const frame = $('iframe.module-frame[data-module="layers"]');
    const style = getComputedStyle(frame);
    await check('one sidebar, one status bar: the app\'s', $$('nav.sidebar').length === 1 && $$('footer.statusbar').length === 1);
    await check('the module brings no chrome: its pages say so (module mode: a manifest), its frame has no border', !!T().modules.manifests.layers && style.borderTopWidth === '0px' && style.borderLeftWidth === '0px', `${style.borderTopWidth} ${JSON.stringify(Object.keys(T().modules.manifests))}`);
    await check('the frame fills the main area', Math.abs(frame.getBoundingClientRect().width - frame.parentElement.getBoundingClientRect().width) < 4);
    await check('the frame is still of its own origin, sandboxed', frame.getAttribute('src').startsWith('chains-pack://layers/') && frame.getAttribute('sandbox') === 'allow-scripts allow-same-origin' && frame.contentDocument === null);
    await check('Layers\' own pages in the app\'s sidebar', ['home', 'send', 'receive', 'transactions', 'staking', 'trade'].every((id) => modulePage(id)), $$('[data-module-page]').map((e) => e.dataset.modulePage).join(','));
    await check('its status in the app\'s status bar', !!$('[data-module-sync="layers"]'));
    await sleep(2500);
    await shot('modules-layers-home');

    // 3. Coins in; a send through the trusted dialog.
    const dep = await frameRpc('getdepositaddress', [], 'main');
    await desktop({ yes: true });
    await frameHost('peg.deposit', { destination: dep.depositaddress, amount: '20' });
    await rpc('generatetoaddress', [1, mining]);
    await bmm('layers', 2);
    await waitFor('the deposit credited on Layers', async () => (await frameRpc('getbalances', [], 'main')).mine.trusted >= 19, 120000);
    await openPage('send', 'Send');
    const to = await frameRpc('getnewaddress', [], 'main');
    const prepared = await frameHost('wallet.send.prepare', { wallet: 'main', recipients: [{ address: to, amount: 150000000 }] });
    await desktop({ show: true });
    const sent = frameHost('wallet.send.commit', { wallet: 'main', token: prepared.token });
    await inWindow('modules-dialog-layers-send', true);
    const send = await sent;
    const asked = await lastAsked();
    await check('the send was asked in the app\'s trusted dialog, on Layers', asked.chain === 'Layers' && asked.rows.some((r) => r.value === to), JSON.stringify(asked && asked.title));
    await check('the send ran', send && /^[0-9a-f]{64}$/.test(send.txid), JSON.stringify(send));
    await bmm('layers', 1);
    await check('the send is confirmed on Layers', !(await frameRpc('getrawmempool')).includes(send.txid));

    // An asset of its own, for the portfolio: reserved and registered (one question), asked in the trusted dialog.
    await desktop({ always: true });
    await frameHost('rpc.confirmed', { method: 'reserveasset', params: ['GOLD'], wallet: 'main', then: { method: 'registerasset', params: ['GOLD', 100000, 2] } });
    await bmm('layers', 4);
    await frameHost('rpc.confirmed', { method: 'registerasset', params: ['GOLD', 100000, 2], wallet: 'main', authorizedOnly: true });
    await bmm('layers', 1);
    await desktop({ always: false });
    await desktop({ clear: true });

    // Its pages in the main shell (the frame is told the page by the app).
    await openPage('trade', 'Trade');
    await check('the breadcrumbs say where', /Layers/.test($('.crumbs').textContent) && /Trade|Swap/.test($('.crumbs').textContent), $('.crumbs').textContent);
    await shot('modules-layers-trade');
    await openPage('staking', 'Staking');
    await shot('modules-layers-staking');
    // Back: the app's history.
    click($('[data-history="back"]'), 'Back');
    await waitFor('back at Trade', () => T().modules.routes.layers?.page === 'trade', 10000);
    await check('back goes where the user was, in the module', T().modules.routes.layers.page === 'trade');

    // 4. Back to the app; the combined portfolio; every chain's transactions.
    click($('.switcher .chain[data-chain="chains"]'), 'the app in the switcher');
    await waitFor('the app\'s pages again', () => T().nav.page === 'home' && !$('[data-module-page]'), 10000);
    await check('the app\'s own pages back in the sidebar', !$('[data-module-page]') && $$('nav.sidebar .entry').length > 5);
    await waitFor('the portfolio', () => $('[data-portfolio-row="layers"]'), 30000);
    await waitFor('the asset of Layers in the portfolio', () => /GOLD/.test($('[data-portfolio-row="layers"]').textContent), 60000).catch(() => null);
    await check('the portfolio lists the asset Layers\' wallet holds', /GOLD/.test($('[data-portfolio-row="layers"]').textContent), $('[data-portfolio-row="layers"]').textContent);
    await sleep(1500);
    await shot('modules-portfolio');
    T().go('transactions');
    // Every chain: the top bar's selector (doc/unified-app.md §19), the app's own page with a Chain column (§21.4).
    T().savePrefs({ chain: 'all' });
    await waitFor('every chain\'s transactions', () => $$('.cellwrap[data-row-chain="layers"][data-txid]').length > 0 && $$('.cellwrap[data-row-chain="chains"][data-txid]').length > 0, 30000);
    await check('the transactions of both chains in one list', true);
    await shot('modules-transactions-all');

    // 5. An update: found, said, approved; stopped, replaced, started.
    T().go('settings', 'sidechains');
    await waitFor('its program box', () => $('[data-program-box="layers"]'), 20000);
    const box = $('[data-program-box="layers"]');
    box.open = true;
    box.dispatchEvent(new Event('toggle'));
    await waitFor('update available', () => $(`[data-update-available="${T2}"]`), 60000);
    box.scrollIntoView({ block: 'nearest' });
    await sleep(800);
    await shot('modules-update-available');
    const pidBefore = (await chainOf('layers')).pid;
    click($('[data-program-box="layers"] [data-action="download"]'), 'Update');
    await waitFor('updated', async () => (await host('modules.releases', { stem: 'layers' })).current === T2, 180000);
    await waitFor('running again', async () => { const c = await chainOf('layers'); return c.ready && c.pid !== pidBefore; }, 180000);
    const after = await host('modules.releases', { stem: 'layers' });
    await check(`updated to ${T2}, ${T1} kept to go back to`, after.current === T2 && after.previous === T1, JSON.stringify({ current: after.current, previous: after.previous }));
    await check('its node runs the new program', (await chainOf('layers')).program.includes(`/${T2}/`));

    await host('sidechains.disable', { stem: 'layers' });
    await waitFor('stopped', async () => (await chainOf('layers')).pid === 0, 180000);
    await finish();
  } catch (e) {
    await finish(e instanceof Error ? e : new Error(JSON.stringify(e)));
  }
})();
