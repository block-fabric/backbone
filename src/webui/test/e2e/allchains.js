// Every sidechain of the catalog on at once (doc/unified-app.md §25), as a user drives them in the app's own pages. The
// runner (run_allchains.py) merged-mines the sidechains' blocks at "CHECKPOINT bmm <stem|all> <count>", trades on Hivemind
// at "CHECKPOINT hivemind trade 0" and checks a node's own arguments at "CHECKPOINT validator <stem>". Each part is a step
// of its own: a step that fails is said and the next one runs (the pictures of every page are wanted even so).
(async () => {
  const { host, rpc, sleep, log, T, waitFor, check, snapshot, finish } = E2E;
  const $ = (selector, root = document) => root.querySelector(selector);
  const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];
  const desktop = (answer) => host('test.confirmAnswer', answer);
  const asked = async () => host('test.confirmed');
  const dialogWindow = (action, more = {}) => host('test.dialog', { action, ...more });
  const CHAINS = (window.__ALLCHAINS && window.__ALLCHAINS.chains) || [];
  const has = (stem) => CHAINS.includes(stem);
  const SLOTS = { layers: 7, fastblocks: 5, thunder: 2, bitnames: 3, bitassets: 4, zside: 6, hivemind: 9 };
  const NAMES = { layers: 'Layers', fastblocks: 'FastBlocks', thunder: 'Thunder', bitnames: 'BitNames', bitassets: 'BitAssets', zside: 'zSide', hivemind: 'Hivemind' };
  const steps = [];

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
    // The notices of the mining wallet (the runner mines a block every few seconds) kept out of the pictures (by the
    // element's style: the page's policy refuses a <style> added).
    const stack = $('.toasts');
    if (stack) stack.style.setProperty('visibility', 'hidden', 'important');
    await sleep(500);
    await snapshot(name);
    if (stack) stack.style.removeProperty('visibility');
  }
  /** A picture in Midnight, then the same in a light theme. */
  async function shots(name, settle = 1200) {
    await shot(`${name}-midnight`);
    T().savePrefs({ theme: 'light' });
    await sleep(settle);
    await shot(`${name}-light`);
    T().savePrefs({ theme: 'midnight' });
    await sleep(400);
  }
  function type(el, value) {
    el.value = value;
    el.dispatchEvent(new Event('input', { bubbles: true }));
  }
  function choose(el, value) {
    el.value = value;
    el.dispatchEvent(new Event('change', { bubbles: true }));
  }
  function scrollTo(el, box = $('#content')) {
    if (!el || !box) return;
    box.scrollTop += el.getBoundingClientRect().top - box.getBoundingClientRect().top - 8;
  }
  const go = (page, tab = '') => T().go(page, tab);
  const chainOf = async (stem) => (await host('sidechains.list')).chains.find((c) => c.stem === stem);
  const heightOf = (stem) => host('sidechains.rpc', { stem, method: 'getblockcount' });
  const summary = (stem) => host('chain.summary', { stem });
  const call = (stem, method, params = {}) => T().packs.route(stem, { rid: `e2e${Math.random()}`, kind: 'host', method, params });
  const confirmed = (stem, method, params, more = {}) => call(stem, 'rpc.confirmed', { method, params, wallet: 'main', ...more });
  async function bmm(stem, count) {
    const stems = stem === 'all' ? CHAINS : [stem];
    const from = {};
    for (const s of stems) from[s] = await heightOf(s).catch(() => 0);
    await log(`CHECKPOINT bmm ${stem} ${count}`);
    for (const s of stems) await waitFor(`${count} blocks of ${s}`, async () => (await heightOf(s)) >= from[s] + count, 240000).catch((e) => log(`NOTE ${e.message}`));
  }
  async function selectChain(id) {
    if (T().prefs.chain === id && $('[data-global-chain]')?.dataset.globalChain === id) return;
    const b = await waitFor('the chain selector of the top bar', () => $('[data-global-chain]'), 20000);
    b.click();
    const o = await waitFor(`the option ${id}`, () => $(`[data-chain-option="${id}"]`), 10000);
    o.click();
    await waitFor(`${id} selected`, () => T().prefs.chain === id && $('[data-global-chain]')?.dataset.globalChain === id, 10000);
    await sleep(300);
  }
  async function step(name, fn) {
    await log(`STEP ${name}`);
    try {
      await fn();
      steps.push(name);
    } catch (e) {
      await check(`step "${name}"`, false, e && e.message ? e.message : String(e));
      await desktop({ always: false }).catch(() => undefined);
      await desktop({ clear: true }).catch(() => undefined);
      $$('dialog[open] button').filter((b) => /^(Cancel|Close|No)$/.test(b.textContent.trim())).forEach((b) => b.click());
      go('home');
      await sleep(500);
    }
  }
  /** Answer the app's own question dialog of the page (not the trusted window): its button of that text. */
  async function answerPage(label) {
    const b = await waitFor(`the button "${label}" of the page's dialog`, () => $$('dialog[open] button').find((x) => x.textContent.trim() === label), 20000);
    b.click();
  }

  let mining = '';
  const withdrawals = {};
  /** A payment of each chain (its txid), found again on Transactions. */
  const paid = {};
  try {
    await waitFor('the node to run', () => T() && T().app.phase === 'ready' && T().node.tip, 120000);
    T().savePrefs({ theme: 'midnight', layout: 'dashboard' });

    // 0. A chain with every sidechain's slot active; all seven turned on (FastBlocks through its wizard, as a validator).
    await step('every sidechain active and on', async () => {
      await rpc('createwallet', ['demo']);
      await waitFor('the wallet', () => T().wallets.current === 'demo');
      mining = await rpc('getnewaddress', ['Mining'], 'demo');
      await rpc('generatetoaddress', [110, mining]);
      for (const stem of CHAINS) await rpc('createsidechainproposal', [SLOTS[stem], NAMES[stem], 'all chains']);
      // A proposal a block: seven take more than the 25 blocks of acks one does.
      let active = [];
      for (let i = 0; i < 6; i++) {
        await rpc('generatetoaddress', [25, mining]);
        active = (await rpc('listactivesidechains')).map((s) => s.slot);
        if (CHAINS.every((s) => active.includes(SLOTS[s]))) break;
      }
      await check('every slot active on Chains', CHAINS.every((s) => active.includes(SLOTS[s])), JSON.stringify(active));
      for (const stem of CHAINS) if (stem !== 'fastblocks') await host('sidechains.enable', { stem, wallet: 'separate' });
      if (has('fastblocks')) {
        // Through its wizard: the staking step, "Make this computer a validator" ticked.
        go('settings', 'sidechains');
        await waitFor('the cards of Settings → Sidechains', () => $('.card[data-stem="fastblocks"] input[type="checkbox"]'), 30000);
        $('.card[data-stem="fastblocks"] input[type="checkbox"]').click();
        const wizard = () => $('[data-wizard="enable"]');
        await waitFor('the enable wizard of FastBlocks', () => wizard() && wizard().dataset.step === 'about', 20000);
        for (let i = 0; i < 8 && wizard() && wizard().dataset.step !== 'staking'; i++) {
          $('[data-action="next"]').click();
          await sleep(400);
        }
        await waitFor('its staking step', () => wizard().dataset.step === 'staking', 10000);
        const box = $('[data-wizard-stake] input[type="checkbox"]');
        await check('the staking step: "Make this computer a validator" can be chosen, its key never from the phrase', box && !box.disabled && /never from your backup phrase/.test($('[data-wizard-stake]').textContent), box ? $('[data-wizard-stake]').textContent : 'none');
        box.click();
        await sleep(300);
        await shot('allchains-enable-fastblocks-validator');
        $('[data-action="next"]').click();
        await waitFor('its review', () => wizard().dataset.step === 'review', 10000);
        await check('the review says this computer validates', /This computer, with a key made here/.test(wizard().textContent), wizard().textContent);
        await shot('allchains-enable-fastblocks-review');
        $('[data-action="turn-on"]').click();
        await waitFor('FastBlocks turned on', () => wizard() && wizard().dataset.step === 'done', 60000);
        $$('[data-wizard="enable"] ~ * button, dialog[open] button').find((b) => b.textContent.trim() === 'Close')?.click();
      }
      for (const stem of CHAINS) await waitFor(`${stem} running`, async () => (await chainOf(stem)).ready, 240000);
      await bmm('all', 2);
      const list = (await host('sidechains.list')).chains;
      await check('all seven on and running', CHAINS.every((s) => list.find((c) => c.stem === s && c.enabled && c.ready)), JSON.stringify(list.map((c) => [c.stem, c.state])));
      if (has('fastblocks')) {
        await check('FastBlocks: a validator key made by the app on this computer', list.find((c) => c.stem === 'fastblocks').validatorKey === true);
        await log('CHECKPOINT validator fastblocks 0');
      }
    });

    // 1. Coins into every sidechain: FastBlocks' through the Move coins panel, the others' by the adapter (the same path).
    await step('a deposit into every sidechain', async () => {
      await desktop({ always: true });
      for (const stem of CHAINS) {
        if (stem === 'fastblocks') continue;
        const r = await host('chain.deposit', { stem, amount: '40' });
        await check(`a deposit into ${NAMES[stem]}`, /^[0-9a-f]{64}$/.test(r.txid || ''), JSON.stringify(r));
      }
      await desktop({ always: false });
      await desktop({ clear: true });
      if (has('fastblocks')) {
        go('sidechains');
        await waitFor('the Move coins panel', () => $('[data-move-panel] #move-chain'), 30000);
        choose($('#move-chain'), 'fastblocks');
        type($('[data-move-amount]'), '40');
        await sleep(400);
        await shot('allchains-move-coins-fastblocks');
        await desktop({ show: true });
        $('[data-move-submit]').click();
        await inWindow('allchains-dialog-deposit-fastblocks', true);
        await waitFor('the deposit on its way', () => $('[data-sidechain-status] [data-on-their-way="deposit"]'), 30000);
      }
      await rpc('generatetoaddress', [1, mining]);
      await bmm('all', 2);
      for (const stem of CHAINS) await waitFor(`the deposit credited on ${stem}`, async () => Number((await summary(stem)).balances.trusted) >= 39, 240000);
      await check('every deposit credited', true);
    });

    // 1b. The guided move of coins (Move coins, step by step): every chain offered, a move into zSide followed until credited.
    await step('the guided move of coins', async () => {
      const target = has('zside') ? 'zside' : CHAINS[0];
      const wizardStep = (k) => $(`[data-wizard="${k}"]`)?.dataset.step;
      const next = () => $('[data-action="next"]').click();
      await selectChain('all');
      go('sidechains');
      const guide = await waitFor('the guide button', () => $('[data-action="move-guide"]'), 30000);
      guide.click();
      await waitFor('the move guide', () => wizardStep('move') === 'direction', 20000);
      next();
      await waitFor('which sidechain', () => wizardStep('move') === 'chain', 20000);
      await check('the guide offers every chain on', CHAINS.every((c) => $(`[data-wizard="move"] [data-chain="${c}"]`)), $$('[data-wizard="move"] [data-chain]').map((b) => b.dataset.chain).join(','));
      $(`[data-wizard="move"] [data-chain="${target}"]`).click();
      await sleep(300);
      await shot('allchains-move-wizard-chain');
      next();
      await waitFor('how much', () => wizardStep('move') === 'amount', 20000);
      type($('[data-wizard="move"] [data-move-amount]'), '3');
      await sleep(200);
      next();
      await waitFor('the check', () => wizardStep('move') === 'review', 20000);
      await shot('allchains-move-wizard-review');
      await desktop({ yes: true });
      $('[data-action="send"]').click();
      await waitFor('sent', () => wizardStep('move') === 'sent', 60000);
      await rpc('generatetoaddress', [1, mining]);
      await bmm(target, 2);
      await waitFor(`credited on ${target}, followed by the guide`, () => $('[data-wizard="move"] [data-credited="true"]'), 180000);
      await check(`the guided move into ${NAMES[target]} credited`, true);
      await shot('allchains-move-wizard-credited');
      $$('[data-wizard="move"] button, dialog[open] button').find((b) => /^(Done|Close)$/.test(b.textContent.trim()))?.click();
      await sleep(400);
    });

    // 2. A payment on every chain from the app's Send page (its tile picked, the address checked, the chain's own question
    //    "On <chain>"), and an address of each from Receive.
    await step('a payment and an address on every chain', async () => {
      for (const stem of CHAINS) {
        const to = (await host('chain.receive', { stem, label: 'to myself' })).address;
        await selectChain('all');
        go('send');
        await waitFor('the chain tiles of Send', () => $(`[data-chain-tiles="send"] [data-chain-tile="${stem}"]`), 30000);
        $(`[data-chain-tile="${stem}"]`).click();
        await waitFor(`the Send form of ${stem}`, () => $(`[data-sidechain-send="${stem}"] [data-send-amount]`), 30000);
        type($('[data-send-amount]'), '1.25');
        type($('[data-send-address]'), to);
        await waitFor(`the address of ${stem} taken`, () => $('[data-address-ok]'), 30000);
        if (['fastblocks', 'bitnames', 'zside', 'thunder'].includes(stem)) await shot(`allchains-send-${stem}`);
        await desktop(stem === 'fastblocks' || stem === 'bitnames' ? { show: true } : { yes: true });
        const before = (await asked()).length;
        $('[data-send-go]').click();
        if (stem === 'fastblocks' || stem === 'bitnames') await inWindow(`allchains-dialog-send-${stem}`, true);
        await waitFor(`the ${stem} send done`, () => $('[data-send-done]') || $('[data-send-error]'), 90000);
        const q = (await asked()).at(-1);
        paid[stem] = $('[data-send-done] .mono') ? $('[data-send-done] .mono').textContent.trim() : '';
        await check(`${NAMES[stem]}: a payment asked "On ${NAMES[stem]}", sent`, !!$('[data-send-done]') && (await asked()).length > before && q.chain === NAMES[stem] && q.rows[0].value === NAMES[stem], $('[data-send-error]') ? $('[data-send-error]').textContent : JSON.stringify(q));
        // Receive: a new address of its wallet, its QR, its deposit address.
        go('receive');
        await waitFor('the chain tiles of Receive', () => $(`[data-chain-tiles="receive"] [data-chain-tile="${stem}"]`), 30000);
        $(`[data-chain-tile="${stem}"]`).click();
        await waitFor(`Receive on ${stem}`, () => $(`[data-sidechain-receive="${stem}"] [data-receive-new]`), 30000);
        $('[data-receive-new]').click();
        await waitFor(`a new address of ${stem}`, () => $(`[data-sidechain-receive="${stem}"] [data-receive-address]`), 30000);
        const shown = $(`[data-sidechain-receive="${stem}"] [data-receive-address]`).dataset.receiveAddress;
        const v = await host('chain.validate', { stem, address: shown });
        await check(`${NAMES[stem]}: an address of its wallet on Receive`, v.own === true, shown);
        if (['fastblocks', 'bitnames', 'zside', 'thunder'].includes(stem)) {
          $('[data-receive-deposit-address]')?.click();
          await sleep(800);
          await shot(`allchains-receive-${stem}`);
        }
      }
      await bmm('all', 1);
    });

    // 3. FastBlocks: this computer a validator (the key made in the wizard), coins bonded to its key from the wallet (its own
    //    question "On FastBlocks"), staking on; Layers: its key made from the Staking page's offer.
    await step('staking: validator keys made by the app, a bond', async () => {
      if (!has('fastblocks')) return;
      go('staking');
      await selectChain('all');
      $('[data-staking-tab="fastblocks"]')?.click();
      await waitFor('FastBlocks\' own staking page', () => $('[data-staking-chain="fastblocks"]') && $('iframe.module-frame.embedded[data-stem="fastblocks"]:not([hidden])'), 60000);
      const st = await host('chain.staking', { stem: 'fastblocks' });
      const key = st.node && st.node.keys && st.node.keys[0] && st.node.keys[0].pubkey;
      await check('FastBlocks: its node has the validator key the app made', st.node && st.node.validator === true && /^[0-9a-f]{64}$/.test(key || ''), JSON.stringify(st.node));
      await check('FastBlocks: no offer to make a key (it has one)', !$('[data-validator-offer="fastblocks"]'));
      const min = Number((st.chain && st.chain.staking && st.chain.staking.minstake) || 1);
      const amount = String(Math.max(min, 5));
      await desktop({ show: true });
      const bonding = call('fastblocks', 'fastblocks.bond', { wallet: 'main', pubkey: key, amount });
      await inWindow('allchains-dialog-bond-fastblocks', true);
      const bonded = await bonding;
      await check('FastBlocks: coins bonded to this computer\'s key, asked "On FastBlocks"', bonded && !bonded.error, JSON.stringify(bonded));
      const q = (await asked()).at(-1);
      await check('the bond asked in the trusted dialog with the chain first', q && q.chain === 'FastBlocks' && q.rows[0].value === 'FastBlocks', JSON.stringify(q));
      await bmm('fastblocks', 2);
      await waitFor('the bond of the wallet', async () => ((await host('chain.staking', { stem: 'fastblocks' })).bonds || []).some((b) => b.validator === key), 120000);
      await check('FastBlocks: the wallet\'s bond to its key', true);
      await sleep(3000);
      await shots('allchains-staking-fastblocks', 3500);
      // Layers: no key yet: the app offers to make one on this computer.
      if (has('layers')) {
        $('[data-staking-tab="layers"]').click();
        await waitFor('the offer to make Layers\' validator key', () => $('[data-validator-offer="layers"] [data-make-validator-key="layers"]'), 60000);
        await sleep(2500);
        await shot('allchains-staking-layers-offer');
        $('[data-make-validator-key="layers"]').click();
        await waitFor('the question of the page', () => $('dialog[open]') && /validator/i.test($('dialog[open]').textContent), 20000);
        await shot('allchains-staking-layers-question');
        await answerPage('Make a validator key');
        await waitFor('Layers started again with its key', async () => {
          const s = await host('chain.staking', { stem: 'layers' }).catch(() => null);
          return s && s.node && s.node.validator === true;
        }, 180000);
        await waitFor('the offer gone', () => !$('[data-validator-offer="layers"]'), 60000);
        await log('CHECKPOINT validator layers 0');
        await check('Layers: a validator key made from the Staking page, its node runs with it', (await chainOf('layers')).validatorKey === true);
        await sleep(3000);
        await shot('allchains-staking-layers-validator');
      }
    });

    // 4. BitNames: a name reserved and registered (its own question), held on Home; found by the palette and the explorer.
    await step('BitNames: a name, held, found again', async () => {
      if (!has('bitnames')) return;
      await desktop({ always: true });
      await confirmed('bitnames', 'reservename', ['alice'], { then: { method: 'registername', params: ['alice', { ipv4: '203.0.113.7:443' }, true] } });
      await bmm('bitnames', 3);
      const info = await host('sidechains.rpc', { stem: 'bitnames', method: 'getblockcount' });
      await confirmed('bitnames', 'registername', ['alice', { ipv4: '203.0.113.7:443' }, true], { authorizedOnly: true }).catch(async (e) => {
        await log(`NOTE registername at ${info}: ${e && e.message}`);
        await bmm('bitnames', 3);
        return confirmed('bitnames', 'registername', ['alice', { ipv4: '203.0.113.7:443' }, true], { authorizedOnly: true });
      });
      await bmm('bitnames', 1);
      await desktop({ always: false });
      await desktop({ clear: true });
      await waitFor('the name held by the wallet', async () => ((await summary('bitnames')).names || {}).count >= 1, 120000);
      await check('BitNames: the name alice held by the wallet', true);
      await selectChain('bitnames');
      go('home');
      await waitFor('Home of BitNames: its names held', () => $('[data-sidechain-cards="bitnames"] [data-card-names]') && /1/.test($('[data-card-names]').textContent), 60000);
      await shots('allchains-home-bitnames');
      // The palette (Ctrl+K): the name typed, looked up on BitNames, opened in its directory.
      await selectChain('all');
      T().openPalette ? T().openPalette() : window.dispatchEvent(new KeyboardEvent('keydown', { key: 'k', ctrlKey: true, bubbles: true }));
      const input = await waitFor('the palette', () => $('dialog.palette[open] input'), 10000);
      type(input, 'alice');
      await waitFor('the name found by the palette', () => $$('dialog.palette[open] .option').some((o) => /alice — a name on BitNames/.test(o.textContent)), 30000);
      await sleep(400);
      await shot('allchains-palette-name');
      $$('dialog.palette[open] .option').find((o) => /alice — a name on BitNames/.test(o.textContent)).click();
      await waitFor('BitNames\' directory at alice', () => T().prefs.chain === 'bitnames' && (T().modules.routes.bitnames || {}).page === 'directory' && $('iframe.module-frame[data-stem="bitnames"]:not([hidden])'), 60000);
      await sleep(3500);
      await shot('allchains-bitnames-directory-alice');
      // The explorer's search over every chain finds it too.
      await selectChain('all');
      go('explorer');
      await waitFor('the explorer of every chain', () => $('[data-all-search]'), 20000);
      type($('[data-all-search]'), 'alice');
      $('[data-all-search]').closest('form').requestSubmit();
      await waitFor('found on BitNames', () => $('[data-found="bitnames:name"]'), 30000);
      await check('the explorer\'s search finds the name on BitNames, "yours"', /yours/.test($('[data-found="bitnames:name"]').textContent), $('[data-found="bitnames:name"]').textContent);
      await shot('allchains-explorer-search-name');
    });

    // 5. zSide: coins shielded (to yourself), paid privately with a memo, unshielded; both balances on Home and Overview.
    await step('zSide: shielded, paid privately, unshielded', async () => {
      if (!has('zside')) return;
      await selectChain('zside');
      go('send');
      await waitFor('the Send form of zSide', () => $('[data-sidechain-send="zside"] [data-send-from]'), 30000);
      $('[data-send-to-self]').click();
      await waitFor('to yourself: shield', () => $('[data-send-self="shield"]'), 10000);
      type($('[data-send-amount]'), '10');
      await sleep(400);
      await shot('allchains-zside-shield');
      await desktop({ show: true });
      $('[data-send-go]').click();
      await inWindow('allchains-dialog-zside-shield', true);
      await waitFor('shielded', () => $('[data-send-done]') || $('[data-send-error]'), 90000);
      await check('zSide: 10 shielded, its own question', !!$('[data-send-done]') && (await asked()).at(-1).chain === 'zSide', $('[data-send-error]') ? $('[data-send-error]').textContent : '');
      await bmm('zside', 4);
      await waitFor('the private coins spendable', async () => Number(((await summary('zside')).shielded || {}).spendable || 0) >= 9.9, 180000);
      // Paid privately, with a memo, to a shielded address of its own (Receive's private address).
      const zaddr = (await host('chain.receive', { stem: 'zside', shielded: true })).address;
      go('home');
      go('send');
      await waitFor('the Send form of zSide', () => $('[data-sidechain-send="zside"] [data-send-from]'), 30000);
      $('[data-send-from-pick="private"]').click();
      await waitFor('from the private coins', () => $('[data-send-from-pick="private"]').getAttribute('aria-checked') === 'true', 10000);
      await check('zSide: after shielding, the form pays someone again', !!$('[data-send-address]') && !$('[data-send-self]'));
      await waitFor('the private coins on the form', () => /[1-9]/.test($('[data-send-from-pick="private"]').textContent), 30000);
      await desktop({ show: true });
      type($('[data-send-address]'), zaddr);
      type($('[data-send-amount]'), '2.5');
      await waitFor('the shielded address taken', () => $('[data-address-ok]'), 30000);
      type($(`#send-memo-zside`), 'For the coffee');
      await sleep(300);
      await shot('allchains-zside-send-private');
      $('[data-send-go]').click();
      await inWindow('allchains-dialog-zside-private', true);
      await waitFor('paid privately', () => $('[data-send-done]') || $('[data-send-error]'), 90000);
      await check('zSide: a private payment with a memo, its own question', !!$('[data-send-done]'), $('[data-send-error]') ? $('[data-send-error]').textContent : '');
      await bmm('zside', 4);
      // Unshielded: back to its coins.
      go('home');
      go('send');
      await waitFor('the Send form of zSide', () => $('[data-sidechain-send="zside"] [data-send-from]'), 30000);
      $('[data-send-from-pick="private"]').click();
      // Its notes deep enough to spend again (the payment's change, the note received).
      await waitFor('the private coins spendable again', async () => Number(((await summary('zside')).shielded || {}).spendable || 0) >= 1.5, 180000);
      await waitFor('the private coins on the form', () => /[1-9]/.test($('[data-send-from-pick="private"]').textContent), 30000);
      $('[data-send-to-self]').click();
      await waitFor('to yourself: unshield', () => $('[data-send-self="unshield"]'), 10000);
      type($('[data-send-amount]'), '1');
      await sleep(300);
      await shot('allchains-zside-unshield');
      await desktop({ show: true });
      $('[data-send-go]').click();
      await inWindow('allchains-dialog-zside-unshield', true);
      await waitFor('unshielded', () => $('[data-send-done]') || $('[data-send-error]'), 90000);
      await check('zSide: 1 unshielded', !!$('[data-send-done]'), $('[data-send-error]') ? $('[data-send-error]').textContent : '');
      await bmm('zside', 2);
      // Receive its private address.
      go('receive');
      await waitFor('Receive on zSide', () => $('[data-sidechain-receive="zside"] [data-receive-new]'), 30000);
      $$('[data-sidechain-receive="zside"] label.check input').forEach((i) => { if (!i.checked) i.click(); });
      $('[data-receive-new]').click();
      await waitFor('a private address', () => $('[data-sidechain-receive="zside"] [data-receive-address]'), 30000);
      await shot('allchains-receive-zside-private');
      go('home');
      await waitFor('Home of zSide: its coins and its private coins', () => $('[data-sidechain-cards="zside"] [data-card-private]') && /[1-9]/.test($('[data-card-private]').textContent), 60000);
      await shots('allchains-home-zside');
    });

    // 6. Hivemind: a market traded by its own CLI; the open position on Home.
    await step('Hivemind: a position', async () => {
      if (!has('hivemind')) return;
      await log('CHECKPOINT hivemind trade 0');
      await waitFor('a position of the wallet', async () => ((await host('chain.positions', { stem: 'hivemind' })).positions || []).length > 0, 180000);
      await check('Hivemind: an open position', true);
    });

    // 7. A withdrawal started from every chain (to an address of Chains' wallet), its own question; on its way.
    await step('a withdrawal started from every chain', async () => {
      await desktop({ always: true });
      for (const stem of CHAINS) {
        const to = await rpc('getnewaddress', [`Back from ${NAMES[stem]}`], 'demo');
        const r = await host('chain.withdraw', { stem, address: to, amount: '2' }).catch((e) => ({ error: e && e.message }));
        withdrawals[stem] = r;
        await check(`${NAMES[stem]}: a withdrawal started`, !r.error && (r.txid || r.result), JSON.stringify(r));
      }
      await bmm('all', 1);
      for (const stem of CHAINS) {
        await waitFor(`the withdrawal of ${stem} on its way`, async () => ((await summary(stem)).withdrawals || {}).count >= 1, 120000).catch((e) => check(`${NAMES[stem]}: its withdrawal on its way`, false, e.message));
      }
      await desktop({ always: false });
      await desktop({ clear: true });
      await selectChain('all');
      go('sidechains');
      await waitFor('the withdrawals on their way', () => $('[data-sidechain-status] [data-on-their-way="withdrawal"]'), 60000);
      await sleep(800);
      await shots('allchains-sidechains');
    });

    // 8. Every chain at once: Home, Overview, the explorer's strips, Transactions, the sidebar, the selector.
    await step('all seven: Home, Overview, explorer, transactions, sidebar, selector', async () => {
      await selectChain('all');
      go('home');
      await waitFor('every chain in Home\'s Balances', () => CHAINS.every((s) => $(`[data-chains-overview="home"] [data-chain-card="${s}"]`)), 60000);
      await check('Home: all seven and Chains in one Balances card', $$('[data-chains-overview="home"] [data-chain-card]').length === CHAINS.length + 1);
      await waitFor('the chart of every chain', () => $$('[data-chart-series]').length >= CHAINS.length, 90000).catch((e) => check('Home: a line per chain', false, e.message));
      await sleep(1000);
      await shots('allchains-home-all', 2000);
      scrollTo($('[data-chain-chart]') || $('[data-chart-series]'));
      await sleep(500);
      await shot('allchains-home-all-chart');
      $('#content').scrollTo(0, 0);
      go('overview');
      await waitFor('every chain on Overview', () => CHAINS.every((s) => $(`[data-chains-overview="overview"] [data-chain-card="${s}"]`)), 60000);
      await waitFor('every chain\'s latest blocks', () => CHAINS.every((s) => $(`[data-many-blocks="blocks"] [data-block-row="${s}"]`)), 60000).catch((e) => check('Overview: the latest blocks of every chain', false, e.message));
      await sleep(800);
      await shots('allchains-overview-all', 2000);
      scrollTo($('[data-many-recent]') && $('[data-many-recent]').closest('section'));
      await sleep(500);
      await shot('allchains-overview-all-lists');
      $('#content').scrollTo(0, 0);
      go('explorer');
      await waitFor('a strip of every chain', () => ['chains', ...CHAINS].every((s) => $(`[data-strip="${s}"] button[data-strip-block]:not([data-strip-block="next"])`)), 90000);
      await check('the explorer: a strip for Chains and each of the seven', $$('[data-strip]').length === CHAINS.length + 1, $$('[data-strip]').map((r) => r.dataset.strip).join(','));
      await sleep(1500);
      await shots('allchains-explorer-strips', 2000);
      for (const stem of ['fastblocks', 'bitnames', 'zside', 'thunder'].filter(has)) {
        $$(`[data-strip="${stem}"] button[data-strip-block]`)[1].click();
        await waitFor(`a block of ${stem} under the strips`, () => $(`[data-strip-detail^="${stem}:"] [data-side-block^="${stem}:"]`), 60000);
        await sleep(800);
        scrollTo($('[data-strip-detail]'));
        await sleep(400);
        await shot(`allchains-explorer-block-${stem}`);
        $('[data-strip-close]').click();
        $('#content').scrollTo(0, 0);
        await sleep(400);
      }
      go('transactions');
      await waitFor('the transactions of every chain', () => $(`[data-tx-chains="${['chains', ...CHAINS].join(',')}"]`), 60000);
      await check('Transactions: a Chain column', $$('[role="columnheader"]').some((h) => h.textContent.includes('Chain')));
      await sleep(1500);
      await shots('allchains-transactions', 2000);
      // Each chain's payment of step 2, found by its id in the one list (the list is windowed: a search brings it up).
      const box = $('.transactions input[type="search"]');
      for (const stem of CHAINS.filter((x) => paid[x])) {
        type(box, paid[stem]);
        await waitFor(`the payment of ${stem} in the list`, () => $(`.cellwrap[data-row-chain="${stem}"][data-txid="${paid[stem]}"]`), 60000).catch((e) => check(`Transactions: the payment of ${NAMES[stem]}`, false, e.message));
      }
      await check('Transactions: a payment of every chain in the one list', CHAINS.every((x) => paid[x]), JSON.stringify(paid));
      type(box, '');
      // The sidebar: a group per sidechain; the selector: All, Chains, then the seven in the catalog's order.
      await check('a group per sidechain in the sidebar', CHAINS.every((s) => $(`[data-chain-group="${s}"]`)));
      const bnItems = $$('[data-chain-group="bitnames"] [data-chain-item]').map((b) => b.dataset.chainItem);
      if (has('bitnames')) await check('BitNames\' group: Get a name, My names, Name directory, Paymail, DNS', JSON.stringify(bnItems) === JSON.stringify(['bitnames:register', 'bitnames:names', 'bitnames:directory', 'bitnames:paymail', 'bitnames:dns']), JSON.stringify(bnItems));
      const zItems = $$('[data-chain-group="zside"] [data-chain-item]').map((b) => b.dataset.chainItem);
      if (has('zside')) await check('zSide\'s group: its private pages', zItems.includes('zside:private'), JSON.stringify(zItems));
      go('home');
      scrollTo($('[data-chain-group="layers"]') || $('[data-chain-group]'), $('.sidebar .scroll'));
      await sleep(400);
      await shot('allchains-sidebar-groups');
      scrollTo($('[data-chain-group="bitnames"]'), $('.sidebar .scroll'));
      await sleep(400);
      await shot('allchains-sidebar-groups-2');
      $('[data-global-chain]').click();
      await waitFor('the list of chains', () => $('[data-global-chain-list]'), 10000);
      const order = ['layers', 'fastblocks', 'thunder', 'bitnames', 'bitassets', 'zside', 'hivemind'].filter(has);
      await check('the selector: All, Chains, then the seven in the catalog\'s order', $$('[data-chain-option]').map((o) => o.dataset.chainOption).join(',') === ['all', 'chains', ...order].join(','), $$('[data-chain-option]').map((o) => o.dataset.chainOption).join(','));
      await shots('allchains-selector');
      $('[data-chain-option="all"]').click();
      await sleep(400);
    });

    // 9. Each chain's own Home, in both looks.
    await step('each chain\'s Home', async () => {
      for (const stem of ['fastblocks', 'thunder', 'bitnames', 'zside', 'layers', 'bitassets', 'hivemind'].filter(has)) {
        await selectChain(stem);
        go('home');
        await waitFor(`Home of ${stem}`, () => $(`[data-sidechain-cards="${stem}"]`), 30000);
        await sleep(2500);
        await shot(`allchains-home-${stem}-one`);
      }
      await selectChain('all');
    });

    // 10. A sidechain turned off and on again: its group, its tile and its line go, then come back.
    await step('turned off and on again', async () => {
      if (!has('thunder')) return;
      // Off from Settings → Sidechains: its card's switch, the page's question ("Its node stops. Its data and its keys
      // stay"), answered.
      go('settings', 'sidechains');
      await waitFor('Thunder\'s card', () => $('.card[data-stem="thunder"] input[type="checkbox"]'), 30000);
      $('.card[data-stem="thunder"] input[type="checkbox"]').click();
      await waitFor('the question to turn it off', () => $('dialog[open]') && /Turn Thunder off/.test($('dialog[open]').textContent), 20000);
      await shot('allchains-disable-thunder-question');
      await answerPage('Turn off');
      await waitFor('Thunder off', async () => (await chainOf('thunder')).pid === 0, 120000);
      await check('Thunder off: its data stays', (await chainOf('thunder')).hasData === true);
      go('home');
      await waitFor('Thunder gone from Home and the sidebar', () => !$('[data-chains-overview="home"] [data-chain-card="thunder"]') && !$('[data-chain-group="thunder"]'), 30000);
      await check('a chain turned off leaves Home and the sidebar', true);
      // On again through its wizard, its own wallet as before (its card's switch off and free again first: the page
      // waits for the node to stop before it says so).
      go('settings', 'sidechains');
      const box = () => $('.card[data-stem="thunder"] input[type="checkbox"]');
      await waitFor('Thunder\'s card, off', () => box() && !box().checked && !box().disabled, 60000);
      box().click();
      const wizard = () => $('[data-wizard="enable"]');
      await waitFor('the enable wizard of Thunder', () => wizard() && wizard().dataset.step === 'about', 20000);
      for (let i = 0; i < 8 && wizard() && wizard().dataset.step !== 'review'; i++) {
        $('[data-action="next"]').click();
        await sleep(400);
      }
      await waitFor('its review', () => wizard().dataset.step === 'review', 10000);
      await shot('allchains-enable-thunder-review');
      $('[data-action="turn-on"]').click();
      await waitFor('Thunder turned on', () => wizard() && wizard().dataset.step === 'done', 60000);
      $$('[data-wizard="enable"] ~ * button, dialog[open] button').find((b) => b.textContent.trim() === 'Close')?.click();
      await waitFor('Thunder on again', async () => (await chainOf('thunder')).ready, 120000);
      go('home');
      await waitFor('Thunder back on Home and in the sidebar', () => $('[data-chains-overview="home"] [data-chain-card="thunder"]') && $('[data-chain-group="thunder"]'), 60000);
      await check('turned on again: back on Home and in the sidebar', true);
    });

    // Off: nothing left running.
    for (const stem of CHAINS) await host('sidechains.disable', { stem });
    await waitFor('every node stopped', async () => (await host('sidechains.list')).chains.every((c) => c.pid === 0), 240000);
    await log(`STEPS done: ${steps.join(' | ')}`);
    await finish();
  } catch (e) {
    await finish(e instanceof Error ? e : new Error(JSON.stringify(e)));
  }
})();
