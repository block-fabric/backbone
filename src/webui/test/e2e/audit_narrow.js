// A narrow window (audit M4): nothing cut off, the menus folded, the amounts of the history in view.
(async () => {
  const { rpc, sleep, log, T, waitFor, check, snapshot, finish } = E2E;
  const W = window.innerWidth;
  const overflow = () => {
    const out = [];
    const doc = document.documentElement;
    if (doc.scrollWidth > doc.clientWidth + 1) out.push(`the page scrolls sideways (${doc.scrollWidth} > ${doc.clientWidth})`);
    for (const el of document.querySelectorAll('.content button, .content .panel, .content h1, .content input, .content select, .appbar button, .statusbar > *')) {
      const r = el.getBoundingClientRect();
      if (r.width && r.right > window.innerWidth + 1 && !el.closest('[role="grid"]')) out.push(`${el.tagName.toLowerCase()} "${(el.textContent || el.getAttribute('aria-label') || '').trim().slice(0, 24)}" ends at ${Math.round(r.right)}`);
      if (out.length > 5) break;
    }
    return out;
  };
  try {
    await waitFor('the node to run', () => T() && T().app.phase === 'ready' && T().node.tip);
    await log(`M width ${W}`);
    T().nav.dialog = null;
    await rpc('createwallet', ['w']);
    const address = await rpc('getnewaddress', [], 'w');
    await rpc('generatetoaddress', [110, address]);
    await waitFor('the history', () => T().node.tip.height >= 110, 60000);
    await sleep(500);
    T().nav.dialog = null;
    const tops = [...document.querySelectorAll('.menubar .top')].map((b) => b.textContent.trim());
    await check(`M4 ${W} px: the menus that do not fit fold into More`, tops.includes('More') && tops.length < 8, tops.join(', '));
    await check(`M4 ${W} px: the sidebar shows icons only`, !!document.querySelector('.sidebar.compact'));
    for (const page of ['overview', 'send', 'receive', 'transactions', 'sidechains', 'sidechain-nodes', 'mine', 'explorer', 'node', 'settings', 'addresses', 'messages', 'psbt', 'proof', 'multisig', 'timestamp', 'crypto', 'wallets']) {
      T().go(page);
      await sleep(450);
      const problems = overflow();
      await check(`M4 ${W} px: ${page} fits`, problems.length === 0, problems.join('; '));
      await snapshot(`w${W}-${page}-light`);
    }
    T().go('transactions');
    await waitFor('rows', () => document.querySelectorAll('[role="grid"] [role="row"]').length > 3, 20000);
    const amount = document.querySelector('[role="grid"] [role="row"]:nth-child(1) .amt') || document.querySelector('[role="grid"] .amt');
    const r = amount ? amount.getBoundingClientRect() : null;
    await check(`M4 ${W} px: the amounts of the history are in view`, !!r && r.right <= window.innerWidth && r.left >= 0, r ? `${Math.round(r.left)}..${Math.round(r.right)}` : 'no amount');
    // A menu opens inside the window.
    document.querySelector('.menubar .top').click();
    await sleep(200);
    const menu = document.querySelector('.dropdown');
    const mr = menu ? menu.getBoundingClientRect() : null;
    await check(`M4 ${W} px: a menu opens inside the window`, !!mr && mr.right <= window.innerWidth + 1 && mr.left >= -1, mr ? `${Math.round(mr.left)}..${Math.round(mr.right)}` : 'no menu');
    await snapshot(`w${W}-menu-light`);
    await finish();
  } catch (e) {
    await finish(e);
  }
})();
