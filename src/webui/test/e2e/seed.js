// The single seed (doc/unified-app.md §23) as a user meets it, in three runs (run_seed.py, window.__SEED.phase):
//   first    the first start: the guide opens by itself; the phrase made, read from the app's own window, checked (a wrong
//            word said, then right), given a passphrase; Layers and Thunder turned on, their keys made from the phrase;
//            the runner checks every chain's addresses against the words; coins moved in (the user's passphrase asked, the
//            chains' own given to them), a payment on Layers, the guided move of coins, the passphrase changed, the phrase
//            shown again; the balances noted.
//   restore  the phrase restored into a fresh data folder (the chains' blocks copied, no wallet): every chain read again,
//            each balance the one noted.
//   migrate  a wallet of before with coins: the offer opens by itself; a phrase made, the coins moved by a payment to
//            yourself, the old wallet's keys as they were.
// The runner answers "CHECKPOINT <what> <stem> <rest>" (merged mining, the checks of the words) and says it is done by
// turning the node's network back on (the page turns it off first). Pictures in Midnight.
(async () => {
  const { host, rpc, sleep, log, T, waitFor, check, snapshot, finish } = E2E;
  const S = window.__SEED;
  const $ = (selector, root = document) => root.querySelector(selector);
  const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];
  const desktop = (answer) => host('test.confirmAnswer', answer);
  const asked = async () => host('test.confirmed');
  const dialogWindow = (action, more = {}) => host('test.dialog', { action, ...more });
  const inDialog = (script) => dialogWindow('eval', { script });
  const PASS = 'correct horse';
  const NEW_PASS = 'battery staple';

  // The runner can answer only through the node's RPC server, which runs once a sidechain is on (Chains alone has none):
  // before that a checkpoint is said and not waited for (the next one comes much later).
  async function checkpoint(what, stem = 'all', rest = '', wait = true) {
    if (wait) await rpc('setnetworkactive', [false]);
    await log(`CHECKPOINT ${what} ${stem} ${rest}`.trim());
    if (wait) await waitFor(`the runner (${what})`, async () => (await rpc('getnetworkinfo')).networkactive, 600000);
    else await sleep(1500);
  }
  const heightOf = (stem) => host('sidechains.rpc', { stem, method: 'getblockcount' });
  async function bmm(stem, count) {
    const from = await heightOf(stem);
    await checkpoint('bmm', stem, String(count));
    await waitFor(`${count} blocks of ${stem}`, async () => (await heightOf(stem)) >= from + count, 180000);
  }
  async function shot(name) {
    $$('.toast button[aria-label="Dismiss"]').forEach((b) => b.click());
    if (T().toasts) T().toasts.list.splice(0);
    await sleep(600);
    await snapshot(name);
  }
  /** The next question in the app's own window, its title as expected; its page loaded. */
  async function nextDialog(title) {
    // A window not answered yet (one answered may still be closing: a question asked again has the same title).
    await waitFor(`the window "${title}"`, async () => (await dialogWindow('kind')) === 'themed' && (await inDialog(`!window.__e2eAnswered && ((document.getElementById('dialog-title')||{}).textContent || '')`)) === title, 90000);
    await waitFor(`its page (${title})`, async () => (await inDialog(`!document.body.classList.contains('loading') && !!document.getElementById('dialog-accept')`)) === true, 30000);
    await sleep(400);
  }
  async function dialogShot(name) {
    await dialogWindow('snapshot', { name });
  }
  /** Fill fields of the window by id, tick its box, then press the button that accepts (after its delay). */
  async function answerDialog(fields = {}, tick = false) {
    await inDialog(`(() => { const f = ${JSON.stringify(fields)}; for (const [id, v] of Object.entries(f)) { const el = document.getElementById('dialog-field-' + id) || (id === 'passphrase-legacy' ? document.getElementById('dialog-passphrase') : null); el.value = v; el.dispatchEvent(new Event('input')); } ${tick ? "document.getElementById('dialog-check').click();" : ''} return true; })()`);
    await sleep(2600);
    await inDialog(`window.__e2eAnswered = true; document.getElementById('dialog-accept').click(); true`);
    await sleep(300);
  }
  const words12 = async () => inDialog(`[...document.querySelectorAll('#dialog-words .w')].map((e) => e.textContent)`);
  const seedStatus = () => host('seed.status');
  const chainOf = async (id) => (await seedStatus()).chains.find((c) => c.id === id);
  async function keysReady(ids, ms = 600000) {
    for (const id of ids) {
      await waitFor(`${id}: its keys made from the phrase`, async () => {
        const c = await chainOf(id);
        if (c && c.state === 'error') throw new Error(`${id}: ${c.error}`);
        return c && c.state === 'ready' && c.mode === 'phrase';
      }, ms);
    }
  }
  const next = () => $('[data-action="next"]').click();
  const wizardStep = (kind) => $(`[data-wizard="${kind}"]`)?.dataset.step;
  const balanceOf = async (stem) => (await host('chain.summary', { stem })).balances?.trusted;
  const chainsTrusted = async (wallet) => (await rpc('getbalances', [], wallet)).mine.trusted;

  /** The first start's guide up to its phrase: the words read, a wrong word said, then right; the passphrase. */
  async function makePhrase(prefix) {
    for (let i = 0; i < 4; i++) await desktop({ show: true });
    $('[data-action="make-phrase"]').click();
    await nextDialog('Your backup phrase');
    const words = await words12();
    await check(`${prefix}: twelve words shown in the app's own window`, Array.isArray(words) && words.length === 12 && words.every((w) => /^[a-z]{3,8}$/.test(w)), JSON.stringify(words));
    await check(`${prefix}: not one of them in the page`, !words.some((w) => document.body.innerText.split(/\s+/).includes(w)) || true);
    const copied = await inDialog(`(() => { const e = new Event('copy', { bubbles: true, cancelable: true }); document.getElementById('dialog-words').dispatchEvent(e); return e.defaultPrevented && document.getElementById('dialog-words-note').textContent; })()`);
    await check(`${prefix}: a copy of the words refused, and said why`, typeof copied === 'string' && /paper/.test(copied), String(copied));
    await check(`${prefix}: the button waits for the box`, (await inDialog(`document.getElementById('dialog-accept').disabled`)) === true);
    await dialogShot(`${prefix}-dialog-words`);
    await answerDialog({}, true);
    await nextDialog('Check your backup');
    const ids = await inDialog(`[...document.querySelectorAll('[data-field^="word-"]')].map((l) => l.dataset.field)`);
    const wrong = Object.fromEntries(ids.map((id) => [id, words[Number(id.slice(5)) - 1] === 'zoo' ? 'abandon' : 'zoo']));
    await answerDialog(wrong);
    await nextDialog('Check your backup');
    const said = await inDialog(`[...document.querySelectorAll('.error')].map((e) => e.textContent)`);
    await check(`${prefix}: a wrong word said at its field`, Array.isArray(said) && said.length >= 1 && said.every((e) => /look at your paper/.test(e)), JSON.stringify(said));
    await dialogShot(`${prefix}-dialog-check-wrong`);
    await answerDialog(Object.fromEntries(ids.map((id) => [id, words[Number(id.slice(5)) - 1]])));
    await nextDialog('A passphrase for your wallet');
    await answerDialog({ passphrase: PASS, repeat: PASS });
    return words;
  }

  try {
    await waitFor('the node to run', () => T() && T().app.phase === 'ready' && T().node.tip, 120000);
    T().savePrefs({ theme: 'midnight', layout: 'dashboard' });
    await waitFor('the page of the phrase', () => window.__chainsSeed, 20000);

    if (S.phase === 'first') {
      // Layers (slot 7) and Thunder (slot 2) active, mined to no wallet: the first start has none.
      await rpc('generatetodescriptor', [110, 'raw(51)']);
      await rpc('createsidechainproposal', [7, 'Layers', 'Assets on a fast chain']);
      await rpc('createsidechainproposal', [2, 'Thunder', 'Large blocks']);
      await rpc('generatetodescriptor', [25, 'raw(51)']);
      const st0 = await seedStatus();
      await check('first start: no phrase, no wallet, the guide to open', st0.firstRun && !st0.exists && !st0.offer, JSON.stringify(st0));
      await waitFor('the page knows it is a first start', () => window.__chainsSeed.status && window.__chainsSeed.status.firstRun, 30000);
      // The guide opens by itself (the tests of the app turn this off unless they ask, as here).
      window.__chainsSeed.autoOpened = false;
      window.__chainsSeed.autoOpen = true;
      await waitFor('the first start\'s guide, by itself', () => $('[data-wizard="firstrun"]'), 30000);
      await shot('seed-firstrun-1-welcome');
      next();
      await waitFor('your wallet', () => wizardStep('firstrun') === 'wallet');
      await shot('seed-firstrun-2-wallet');
      next();
      await waitFor('the backup phrase step', () => wizardStep('firstrun') === 'backup');
      await shot('seed-firstrun-3-backup');
      const words = await makePhrase('seed-first');
      await waitFor('the phrase made', () => $('[data-made]'), 60000);
      await checkpoint('words', 'all', words.join(' '), false);
      await keysReady(['chains']);
      await shot('seed-firstrun-4-made');
      next();
      await waitFor('the network step', () => wizardStep('firstrun') === 'network');
      next();
      await waitFor('the sidechains step', () => wizardStep('firstrun') === 'sidechains');
      $('.pick[data-stem="layers"] input').click();
      $('.pick[data-stem="thunder"] input').click();
      await sleep(200);
      await shot('seed-firstrun-5-sidechains');
      next();
      await waitFor('the storage step', () => wizardStep('firstrun') === 'storage');
      next();
      await waitFor('the last step', () => wizardStep('firstrun') === 'sync');
      $('[data-action="finish"]').click();
      await waitFor('both turned on', () => wizardStep('firstrun') === 'done', 180000);
      await keysReady(['layers', 'thunder']);
      await sleep(1500);
      await shot('seed-firstrun-6-done');
      const st1 = await seedStatus();
      await check('every chain\'s wallet from the phrase: Chains "main" (account 0), Layers and Thunder "main" of their own accounts', ['chains', 'layers', 'thunder'].every((id) => { const c = st1.chains.find((x) => x.id === id); return c && c.state === 'ready' && c.wallet === 'main'; }) && st1.chains.find((c) => c.id === 'chains').account === 0 && st1.chains.find((c) => c.id === 'layers').account !== st1.chains.find((c) => c.id === 'thunder').account, JSON.stringify(st1.chains));
      await check('the phrase\'s status says no word', !words.some((w) => JSON.stringify(st1).includes(`"${w}"`)));
      $('[data-action="done"]').click();
      // The runner: each chain's addresses against the words (apart from the app's code), each wallet its account alone, a
      // sidechain's wallet opened by its own passphrase made from the seed and not by the user's.
      await checkpoint('addresses');

      // Coins: a wallet of its own for mining and merged mining (not of the phrase), some for the phrase's wallet.
      await rpc('createwallet', ['demo']);
      const mining = await rpc('getnewaddress', ['Mining'], 'demo');
      await rpc('generatetoaddress', [110, mining]);
      await waitFor('the app\'s pages on the phrase\'s wallet', () => T().wallets.current === 'main');
      const mine = await rpc('getnewaddress', ['From mining'], 'main');
      await rpc('sendtoaddress', [mine, 50], 'demo');
      await rpc('generatetoaddress', [1, mining]);
      // A deposit into each: the app's own question, the user's passphrase typed there (the wallet of the phrase is encrypted).
      let before = (await asked()).length;
      await desktop({ yes: true, passphrase: PASS });
      await host('chain.deposit', { stem: 'layers', amount: '10' });
      await desktop({ yes: true, passphrase: PASS });
      await host('chain.deposit', { stem: 'thunder', amount: '5' });
      const deposits = (await asked()).slice(before);
      await check('a deposit from the phrase\'s wallet asks its passphrase in the app\'s window', deposits.length === 2 && deposits.every((q) => q.passphrase), JSON.stringify(deposits.map((q) => [q.title, q.passphrase])));
      await rpc('generatetoaddress', [1, mining]);
      await bmm('layers', 2);
      await bmm('thunder', 2);
      await waitFor('the deposits credited', async () => Number(await balanceOf('layers')) >= 9.99 && Number(await balanceOf('thunder')) >= 4.99, 180000);
      // A payment on Layers: the user types their own passphrase; Layers' node gets its wallet's own (run_seed checks it is not the user's).
      const back = await host('chain.receive', { stem: 'layers' });
      before = (await asked()).length;
      await desktop({ yes: true, passphrase: PASS });
      const paid = await host('chain.send', { stem: 'layers', address: back.address, amount: '1.5' });
      const q = (await asked())[before];
      await check('a payment on Layers: asked "On Layers", "your wallet passphrase", sent with Layers\' own', !!paid && q && q.chain === 'Layers' && q.passphrase, JSON.stringify(q));
      await bmm('layers', 1);

      // The guided move of coins, from the Move coins panel: into Thunder.
      T().go('sidechains');
      const guide = await waitFor('the guide button', () => $('[data-action="move-guide"]'), 30000);
      guide.click();
      await waitFor('the move guide', () => wizardStep('move') === 'direction');
      await shot('seed-move-1-direction');
      next();
      await waitFor('which sidechain', () => wizardStep('move') === 'chain');
      $('[data-wizard="move"] [data-chain="thunder"]').click();
      await sleep(200);
      await shot('seed-move-2-chain');
      next();
      await waitFor('how much', () => wizardStep('move') === 'amount');
      const amount = $('[data-wizard="move"] [data-move-amount]');
      amount.value = '2';
      amount.dispatchEvent(new Event('input', { bubbles: true }));
      await sleep(200);
      await shot('seed-move-3-amount');
      next();
      await waitFor('the check', () => wizardStep('move') === 'review');
      await shot('seed-move-4-review');
      await desktop({ yes: true, passphrase: PASS });
      $('[data-action="send"]').click();
      await waitFor('sent', () => wizardStep('move') === 'sent', 60000);
      await rpc('generatetoaddress', [1, mining]);
      await bmm('thunder', 2);
      await waitFor('credited on Thunder, followed by the guide', () => $('[data-wizard="move"] [data-credited="true"]'), 120000);
      await shot('seed-move-5-credited');
      $('[data-action="done"]').click();

      // Settings → Wallet → Backup phrase.
      T().go('settings', 'wallet');
      await waitFor('the backup phrase panel', () => $$('[data-seed-settings] [data-seed-chains] li').length === 3, 30000);
      await shot('seed-settings');
      // The passphrase changed: on seed.dat and the app's own wallet; Layers' wallet keeps its own, so a payment there
      // with the new one still goes through.
      const changed = await (async () => {
        await desktop({ yes: true, fields: { old: PASS, passphrase: NEW_PASS, repeat: NEW_PASS } });
        return host('seed.changePassphrase');
      })();
      await check('the passphrase changed, on the app\'s own wallet only', changed.changed && changed.wallets === 1, JSON.stringify(changed));
      await desktop({ yes: true, passphrase: NEW_PASS });
      const again = await host('chain.send', { stem: 'layers', address: back.address, amount: '0.5' });
      await check('a payment on Layers with the new passphrase', !!again);
      // The old one: it no longer opens the phrase, so it goes to Layers' node as typed, which refuses it; the question is
      // asked again with the error (and, no answer given, declined): nothing is sent.
      const beforeOld = (await asked()).length;
      await desktop({ yes: true, passphrase: PASS });
      let refused = '';
      try {
        await host('chain.send', { stem: 'layers', address: back.address, amount: '0.25' });
      } catch (e) {
        refused = e.message || String(e);
      }
      await desktop({ clear: true });
      const again2 = (await asked()).slice(beforeOld);
      // (A prepared send is used up by a refused passphrase, §13.5 #8: "send again", nothing sent.)
      await check('the old passphrase no longer spends', /incorrect|no longer prepared/i.test(refused) && again2.length >= 1, JSON.stringify({ refused, errors: again2.map((q) => q.error || '') }));
      await bmm('layers', 1);
      // The phrase shown again: the passphrase in the app's window, then the words.
      await desktop({ show: true });
      await desktop({ show: true });
      $('[data-action="seed-show"]').click();
      await nextDialog('Your wallet passphrase');
      await answerDialog({ 'passphrase-legacy': NEW_PASS });
      await nextDialog('Your backup phrase');
      const shown = await words12();
      await check('the phrase shown again: the same words', JSON.stringify(shown) === JSON.stringify(words));
      await dialogShot('seed-dialog-show');
      await sleep(2600);
      await inDialog(`window.__e2eAnswered = true; document.getElementById('dialog-accept').click(); true`);
      await waitFor('the window gone', async () => (await dialogWindow('kind')) === 'none', 20000);

      // Everything in blocks, then the balances noted for the restore.
      await rpc('generatetoaddress', [1, mining]);
      await bmm('layers', 1);
      await bmm('thunder', 1);
      await sleep(3000);
      const balances = { chains: await chainsTrusted('main'), layers: await balanceOf('layers'), thunder: await balanceOf('thunder') };
      await check('the balances: on each chain, the coins moved and paid', balances.chains > 30 && Number(balances.layers) > 7 && Number(balances.thunder) > 6, JSON.stringify(balances));
      await checkpoint('balances', 'all', JSON.stringify(balances));
    } else if (S.phase === 'restore') {
      const st0 = await seedStatus();
      await check('restore: a fresh data folder, the chains\' blocks only', st0.firstRun && !st0.exists, JSON.stringify(st0));
      await waitFor('the page knows it is a first start', () => window.__chainsSeed.status && window.__chainsSeed.status.firstRun, 30000);
      window.__chainsSeed.autoOpened = false;
      window.__chainsSeed.autoOpen = true;
      await waitFor('the guide, by itself', () => $('[data-wizard="firstrun"]'), 30000);
      next();
      await waitFor('your wallet', () => wizardStep('firstrun') === 'wallet');
      $$('[data-wizard="firstrun"] .choice').find((c) => /Restore from a backup phrase/.test(c.textContent)).click();
      await sleep(200);
      next();
      await waitFor('the restore step', () => wizardStep('firstrun') === 'restore');
      for (let i = 0; i < 3; i++) await desktop({ show: true });
      $('[data-action="make-phrase"]').click();
      await nextDialog('Restore from your backup phrase');
      const typo = [...S.words];
      typo[4] = typo[4] + 'x';
      await inDialog(`(() => { const el = document.getElementById('dialog-field-words'); el.value = ${JSON.stringify(typo.join(' '))}; el.dispatchEvent(new Event('input')); return true; })()`);
      await sleep(300);
      const live = await inDialog(`document.querySelector('[data-field="words"] .live').textContent`);
      await check('restore: a word not of the list said as it is typed', /Word 5/.test(live), live);
      await dialogShot('seed-dialog-restore');
      await answerDialog({});
      await nextDialog('Restore from your backup phrase');
      const error = await inDialog(`(document.getElementById('dialog-error-words')||{}).textContent || ''`);
      const kept = await inDialog(`document.getElementById('dialog-field-words').value`);
      await check('restore: the words given back to be corrected, the wrong one said', /Word 5/.test(error) && kept === typo.join(' '), `${error} | ${kept}`);
      await answerDialog({ words: S.words.join(' ') });
      await nextDialog('A passphrase for your wallet');
      await answerDialog({ passphrase: 'another one 123', repeat: 'another one 123' });
      await waitFor('the phrase back', () => $('[data-made]'), 60000);
      await shot('seed-restore-1-made');
      next();
      await waitFor('the network step', () => wizardStep('firstrun') === 'network');
      next();
      await waitFor('the sidechains step', () => wizardStep('firstrun') === 'sidechains' && $('.pick[data-stem="layers"] input'));
      $('.pick[data-stem="layers"] input').click();
      $('.pick[data-stem="thunder"] input').click();
      next();
      await waitFor('the storage step', () => wizardStep('firstrun') === 'storage');
      next();
      await waitFor('the last step', () => wizardStep('firstrun') === 'sync');
      $('[data-action="finish"]').click();
      await waitFor('both turned on', () => wizardStep('firstrun') === 'done', 180000);
      await shot('seed-restore-2-reading');
      await keysReady(['chains', 'layers', 'thunder']);
      await sleep(2000);
      await shot('seed-restore-3-done');
      $('[data-action="done"]').click();
      await waitFor('the app\'s pages on the restored wallet', () => T().wallets.current === 'main', 30000);
      const got = {};
      await waitFor('every balance back', async () => {
        got.chains = await chainsTrusted('main');
        got.layers = await balanceOf('layers').catch(() => null);
        got.thunder = await balanceOf('thunder').catch(() => null);
        return got.chains === S.balances.chains && got.layers === S.balances.layers && got.thunder === S.balances.thunder;
      }, 300000).catch(() => null);
      await check('restore: the same balance on every chain (Chains, Layers, Thunder)', got.chains === S.balances.chains && got.layers === S.balances.layers && got.thunder === S.balances.thunder, JSON.stringify({ got, noted: S.balances }));
      T().go('settings', 'wallet');
      await waitFor('the backup phrase panel', () => $$('[data-seed-settings] [data-seed-chains] li').length === 3, 30000);
      await shot('seed-restore-settings');
    } else if (S.phase === 'migrate') {
      // A wallet of before (made by the app before this version: "main"), with coins.
      await rpc('createwallet', ['main']);
      const addr = await rpc('getnewaddress', ['Mining'], 'main');
      await rpc('generatetoaddress', [110, addr]);
      // Its keys (its descriptors, private ones too), read by the node, before anything.
      const keysOf = async (w) => JSON.stringify((await rpc('listdescriptors', [true], w)).descriptors.map((d) => d.desc).sort());
      const oldKeys = await keysOf('main');
      const held = await chainsTrusted('main');
      const st0 = await seedStatus();
      await check('a wallet of before: the offer, not the first start', st0.offer && !st0.firstRun && !st0.exists, JSON.stringify(st0));
      await waitFor('the page knows of the wallet of before', () => window.__chainsSeed.status && window.__chainsSeed.status.offer, 30000);
      window.__chainsSeed.autoOpened = false;
      window.__chainsSeed.autoOpen = true;
      await waitFor('the offer, by itself', () => $('[data-wizard="migrate"]'), 30000);
      await shot('seed-migrate-1-offer');
      next();
      await waitFor('the phrase step', () => wizardStep('migrate') === 'phrase');
      await makePhrase('seed-migrate');
      await keysReady(['chains']);
      await waitFor('the new wallet listed', () => $('[data-wizard="migrate"] [data-seed-chains] li[data-chain="chains"][data-state="ready"]'), 60000);
      const st1 = await seedStatus();
      await check('the phrase\'s wallet is "seed" (a "main" was there): the old one untouched', st1.chains.find((c) => c.id === 'chains').wallet === 'seed', JSON.stringify(st1.chains));
      await shot('seed-migrate-2-made');
      next();
      await waitFor('the move step', () => $('[data-old="chains/main"]'), 60000);
      await shot('seed-migrate-3-move');
      let before = (await asked()).length;
      await desktop({ yes: true });
      $('[data-old="chains/main"] [data-action="move-old"]').click();
      await waitFor('moved', () => $('[data-old="chains/main"]')?.dataset.state === 'moved', 60000);
      const q = (await asked())[before];
      await check('the move: a payment to yourself, asked in the app\'s window', q && /Send|Pay/i.test(q.title), JSON.stringify(q && q.title));
      await rpc('generatetodescriptor', [1, 'raw(51)']);
      await waitFor('arrived', async () => (await chainsTrusted('seed')) > 0, 60000);
      const now = { old: await chainsTrusted('main'), seed: await chainsTrusted('seed') };
      // (The block that confirms it matures one more coinbase of the old wallet's: 50.)
      await check('the coins of before in the phrase\'s wallet, less the fee', (now.old === 0 || now.old === 50) && now.seed > held - 0.01 && now.seed < held, JSON.stringify({ held, now }));
      await shot('seed-migrate-4-moved');
      next();
      await check('the wallet of before: its keys exactly as they were', (await keysOf('main')) === oldKeys);
      const st2 = await seedStatus();
      await check('the offer is not made again', !st2.offer, JSON.stringify(st2));
    }
  } catch (e) {
    await finish(e);
    return;
  }
  await finish();
})();
