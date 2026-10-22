// What Hivemind's markets came to, at a glance (doc/unified-app.md §24), as a user sees it: Hivemind turned on and coins
// moved into it; then (the runner, run_hmresults.py, at "CHECKPOINT hivemind results 0") markets at every stage: trading,
// voted on, partly decided, and paid out on yes, on no (an upset), on a scale and on several outcomes, with purchases of
// the wallet in each and the trades the pages record in Hivemind's journal. The page checks the app's own Markets page
// (each market's stage and bars, the result of those that paid out, no price needed) and Home's open positions (a market
// voted on: "Voting", its final price), and pictures Hivemind's own pages (Markets: all, paid out; each market; Simple
// mode), the app's Markets page and Home, in Midnight, Dark and Light.
(async () => {
  const { host, rpc, sleep, log, T, waitFor, check, snapshot, finish } = E2E;
  const $ = (selector, root = document) => root.querySelector(selector);
  const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];
  const desktop = (answer) => host('test.confirmAnswer', answer);
  async function shot(name) {
    $$('.toast button[aria-label="Dismiss"]').forEach((b) => b.click());
    if (T().toasts) T().toasts.list.splice(0);
    await sleep(500);
    await snapshot(name);
  }
  const go = (page, tab = '') => T().go(page, tab);
  const chainOf = async (stem) => (await host('sidechains.list')).chains.find((c) => c.stem === stem);
  const heightOf = (stem) => host('sidechains.rpc', { stem, method: 'getblockcount' });
  // A read of Hivemind's node as its pages make it (its policy).
  let rid = 0;
  const side = (method, params = []) => T().packs.route('hivemind', { rid: `e2e${rid++}`, kind: 'rpc', method, params });
  async function bmm(stem, count) {
    const from = await heightOf(stem);
    await log(`CHECKPOINT bmm ${stem} ${count}`);
    await waitFor(`${count} blocks of ${stem}`, async () => (await heightOf(stem)) >= from + count, 180000);
  }
  const route = () => T().modules.routes.hivemind || { page: '', tab: '' };
  /** Hivemind's own pages at a page and tab, its frame shown. */
  async function hivemind(page, tab, what) {
    // Its pages up (they said what they are: their manifest), then the app's route of the chain (its frame is told when
    // shown or loaded) and the frame told, again until the route holds (a frame still starting may answer with its own).
    await waitFor(`${what}: Hivemind's pages up`, () => T().modules.manifests.hivemind && $('iframe.module-frame[data-stem="hivemind"]:not([hidden])'), 60000);
    let held = 0;
    await waitFor(what, async () => {
      if (route().page === page && route().tab === tab) return ++held >= 6;
      held = 0;
      T().modules.routes.hivemind = { page, tab };
      T().packs.tellFrame('hivemind', 'navigate', { page, tab });
      await sleep(400);
      return false;
    }, 60000).catch((e) => {
      throw new Error(`${e.message}; the route: ${JSON.stringify(route())}`);
    });
    await sleep(2500);
  }
  const showGroup = () => {
    const g = $('[data-chain-group="hivemind"]');
    const box = $('.sidebar .scroll');
    if (g && box) box.scrollTop += g.getBoundingClientRect().top - box.getBoundingClientRect().top - 8;
  };
  async function theme(name) {
    T().savePrefs({ theme: name });
    await sleep(1800);
  }

  try {
    await waitFor('the node to run', () => T() && T().app.phase === 'ready' && T().node.tip, 120000);
    T().savePrefs({ theme: 'midnight', layout: 'dashboard' });
    await rpc('createwallet', ['demo']);
    await waitFor('the wallet', () => T().wallets.current === 'demo');
    const mining = await rpc('getnewaddress', ['Mining'], 'demo');
    await rpc('generatetoaddress', [110, mining]);
    await rpc('createsidechainproposal', [9, 'Hivemind', 'e2e']);
    await rpc('generatetoaddress', [25, mining]);
    await host('sidechains.enable', { stem: 'hivemind', wallet: 'separate' });
    await waitFor('Hivemind running', async () => (await chainOf('hivemind')).ready, 180000);
    // Its wallet, made by the app once its node answers.
    await waitFor('Hivemind\'s wallet', async () => ((await host('sidechains.rpc', { stem: 'hivemind', method: 'listwallets' })) || []).includes('main'), 120000);
    await bmm('hivemind', 2);
    await desktop({ yes: true });
    const dep = await host('chain.deposit', { stem: 'hivemind', amount: '60' });
    await check('coins moved into Hivemind', /^[0-9a-f]{64}$/.test(dep.txid || ''), JSON.stringify(dep));
    await rpc('generatetoaddress', [1, mining]);
    await bmm('hivemind', 2);
    await waitFor('the deposit credited on Hivemind', async () => Number((await host('chain.summary', { stem: 'hivemind' })).balances.trusted) >= 59, 180000);

    // Markets at every stage (the runner).
    await log('CHECKPOINT hivemind results 0');
    const stages = async () => {
      const list = await side('listmarkets');
      const n = (s) => list.filter((m) => m.status === s).length;
      return list.length === 7 && n('settled') === 4 && n('closed') === 2 && n('trading') === 1 ? list : null;
    };
    const markets = await waitFor('markets at every stage', stages, 900000);
    const idOf = (title) => markets.find((m) => m.text.endsWith(title)).id;
    const ids = { rain: idOf('Rain in Paris on Sunday'), snow: idOf('Snow in Oslo on Sunday'), goals: idOf('Goals in the cup final'), regatta: idOf('The regatta'), marathon: idOf('The marathon'), launch: idOf('The launch on time'), bridge: idOf('The bridge this year') };
    await check('a market that paid out has no price from its node, its shares kept', markets.filter((m) => m.status === 'settled').every((m) => m.states.every((s) => s.price === undefined && typeof s.shares === 'number')));

    // The adapter: the markets with their decisions and the clock.
    const read = await host('chain.markets', { stem: 'hivemind' });
    const rain = read.markets.find((m) => m.id === ids.rain);
    const rainDecision = read.decisions.find((d) => d.id === rain.decisions[0]);
    await check('chain.markets: each market\'s decisions, their outcomes, the clock', rain.decisions.length === 1 && rainDecision.status === 'resolved' && rainDecision.outcome === '1.00000000' && read.info.periodblocks === 6, JSON.stringify({ rain, rainDecision, info: read.info }));

    // The app's Markets page: each market's stage and bars.
    go('markets');
    await waitFor('the markets of Hivemind', () => $$('tr[data-market]').length === 7, 60000);
    await sleep(800);
    const row = (key) => $(`tr[data-market="${ids[key]}"]`);
    const stagesShown = Object.fromEntries(Object.keys(ids).map((k) => [k, row(k)?.dataset.stage]));
    await check('the stage of each market', JSON.stringify(stagesShown) === JSON.stringify({ rain: 'paid', snow: 'paid', goals: 'paid', regatta: 'paid', marathon: 'partly', launch: 'voting', bridge: 'trading' }), JSON.stringify(stagesShown));
    await check('paid out on Yes: Yes checked and won, 100 %, No at 0, the last price of each marked, when it paid', row('rain').querySelector('.check') && /Won/.test(row('rain').textContent) && /100\.0%/.test(row('rain').textContent) && /0\.0%/.test(row('rain').textContent) && row('rain').querySelectorAll('.mark').length === 2 && /Paid out at block \d+/.test(row('rain').textContent) && /the market called it/.test(row('rain').textContent), row('rain').textContent);
    await check('paid out on No: an upset said', /the market had Yes/.test(row('snow').textContent), row('snow').textContent);
    await check('a scale: the answer of the vote and the market\'s estimate', /Resolved: 4/.test(row('goals').textContent) && !!row('goals').querySelector('[role="img"][aria-label*="the vote said 4"]'), row('goals').textContent);
    await check('several outcomes: Ann won', /Ann/.test(row('regatta').querySelector('li')?.textContent || '') && !!row('regatta').querySelector('li .check'), row('regatta').textContent);
    await check('voted on: its last prices; partly decided: an option ruled out', /Last prices before trading closed/.test(row('launch').textContent) && /Ruled out/.test(row('marathon').textContent), row('launch').textContent + ' | ' + row('marathon').textContent);
    await check('every bar drawn, nothing NaN', $$('[data-market-outcomes] .fill').every((f) => /^\d+(\.\d+)?%$/.test(f.style.width)) && !/NaN|undefined/.test($('#content').textContent));
    await shot('hm-results-app-markets-midnight');
    await theme('light');
    await shot('hm-results-app-markets-light');
    await theme('dark');
    await shot('hm-results-app-markets-dark');
    await theme('midnight');

    // Home: the open positions, the one voted on said so with its final price.
    go('home');
    await waitFor('the open positions', () => $(`[data-position="${ids.launch}"]`) && $(`[data-position="${ids.bridge}"]`), 60000);
    const launchRow = $(`[data-position="${ids.launch}"]`);
    await check('Home: a position in a market voted on says Voting, its price the final one', launchRow.querySelector('[data-position-status="closed"]')?.textContent === 'Voting' && /final/i.test(launchRow.textContent), launchRow.textContent);
    await check('Home: no position of a market that paid out (the chain paid them)', !$(`[data-position="${ids.rain}"]`));
    const marathonRow = $(`[data-position="${ids.marathon}"]`);
    await check('Home: the option held named (Carl), ruled out by the vote, worth 0', !!marathonRow && /Carl/.test(marathonRow.textContent) && !!marathonRow.querySelector('[data-ruled-out]'), marathonRow && marathonRow.textContent);
    const card = $('[data-market-positions]');
    if (card) card.scrollIntoView({ block: 'center' });
    await shot('hm-results-home-positions-midnight');
    await theme('light');
    await shot('hm-results-home-positions-light');
    await theme('midnight');

    // Hivemind's own Markets: every market, then those that paid out (the palette's "Markets that paid out").
    $('[data-chain-item="hivemind:markets"]').click();
    await waitFor('Hivemind\'s Markets', () => route().page === 'markets' && $('iframe.module-frame[data-stem="hivemind"]:not([hidden])'), 60000);
    showGroup();
    await hivemind('markets', 'status/all', 'every market');
    await shot('hm-results-markets-all-midnight');
    await theme('light');
    await shot('hm-results-markets-all-light');
    await theme('dark');
    await shot('hm-results-markets-all-dark');
    await theme('midnight');
    const paidOut = T().buildCommands().find((c) => c.id === 'module.hivemind.cmd.hm.paid-out');
    await check('the palette: Hivemind\'s "Markets that paid out"', !!paidOut);
    if (paidOut) paidOut.run();
    await waitFor('the markets that paid out', () => route().page === 'markets' && route().tab === 'status/settled', 30000);
    await sleep(2500);
    await shot('hm-results-markets-paid-midnight');
    await theme('light');
    await shot('hm-results-markets-paid-light');
    await theme('midnight');

    // Each market.
    for (const [key, name] of [['rain', 'won'], ['snow', 'upset'], ['goals', 'scale'], ['regatta', 'categorical'], ['marathon', 'partly'], ['launch', 'voting'], ['bridge', 'trading']]) {
      await hivemind('markets', `m-${ids[key]}`, `the market ${key}`);
      await shot(`hm-results-market-${name}-midnight`);
      if (['won', 'scale', 'categorical', 'voting'].includes(name)) {
        await theme('light');
        await shot(`hm-results-market-${name}-light`);
        await theme('midnight');
      }
    }
    await hivemind('markets', `m-${ids.rain}`, 'the market rain, dark');
    await theme('dark');
    await shot('hm-results-market-won-dark');
    await theme('midnight');

    // Simple mode: the questions answered, one of them.
    T().savePrefs({ uiMode: 'simple' });
    await sleep(1500);
    $('[data-chain-item="hivemind:markets"]').click();
    await sleep(1500);
    await hivemind('markets', 'status/settled', 'the questions answered');
    await shot('hm-results-simple-answered-midnight');
    await theme('light');
    await shot('hm-results-simple-answered-light');
    await theme('midnight');
    await hivemind('markets', `m-${ids.rain}`, 'a question answered');
    await shot('hm-results-simple-question-midnight');
    await theme('light');
    await shot('hm-results-simple-question-light');
    await theme('midnight');
    T().savePrefs({ uiMode: 'advanced' });
    await sleep(800);
    go('home');
  } catch (e) {
    await finish(e);
    return;
  }
  await finish();
})();
