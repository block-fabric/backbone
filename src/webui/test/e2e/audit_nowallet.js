// A node without wallets (-disablewallet, audit C1): no wallet menus, and the wallet pages say why
// instead of aborting the app.
(async () => {
  const { host, sleep, T, waitFor, check, snapshot, text, finish } = E2E;
  try {
    await waitFor('the node to run', () => T() && T().app.phase === 'ready' && T().node.tip);
    await check('C1 the app knows there are no wallets', T().app.info.walletEnabled === false);
    const menus = [...document.querySelectorAll('.menubar .top')].map((b) => b.textContent.trim());
    await check('C1 no wallet menus', !menus.includes('Your wallet') && !menus.includes('Banking'), menus.join(', '));
    await check('C1 the overview says why', text('.balances').includes('-disablewallet') && !text('.balances').includes('Create or open'));
    for (const page of ['wallets', 'send', 'receive', 'transactions', 'addresses', 'psbt', 'proof', 'multisig']) {
      T().go(page);
      await sleep(400);
      await check(`C1 the ${page} page says why`, text('.content').includes('-disablewallet'), text('.content').slice(0, 80));
    }
    T().go('wallets', 'create');
    await sleep(400);
    await snapshot('audit-nowallet-wallets-light');
    // The host answers its wallet functions with an error, never an abort.
    const error = await host('wallets.list').then(() => null, (e) => e);
    await check('C1 the wallet functions answer an error', error && error.code === -18, JSON.stringify(error));
    await finish();
  } catch (e) {
    await finish(e);
  }
})();
