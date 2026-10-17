// The block explorer of Chains with drivechain activity (run_explorer.py): on regtest, sidechains proposed, acked
// and activated, deposits, BMM requests taken by the blocks (M7 accepts), a withdrawal bundle proposed and upvoted,
// transactions with fees. Then the explorer in Midnight, Advanced: the list with its reward, fees, BMM and coinbase
// columns, and a block opened with a double click: its Coinbase and Drivechain activity sections.
(async () => {
  const { rpc, sleep, T, waitFor, check, snapshot, finish } = E2E;
  const $ = (selector, root = document) => root.querySelector(selector);
  const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];
  const hex = (n) => [...crypto.getRandomValues(new Uint8Array(n))].map((b) => b.toString(16).padStart(2, '0')).join('');
  const le64 = (n) => BigInt(n).toString(16).padStart(16, '0').match(/../g).reverse().join('');
  const be64 = (n) => BigInt(n).toString(16).padStart(16, '0');
  async function depositAddress(slot, address) {
    const text = `s${slot}_${address}_`;
    const digest = new Uint8Array(await crypto.subtle.digest('SHA-256', new TextEncoder().encode(text)));
    return text + [...digest.slice(0, 3)].map((b) => b.toString(16).padStart(2, '0')).join('');
  }
  try {
    await waitFor('the node to run', () => T() && T().app.phase === 'ready' && T().node.tip);
    await rpc('createwallet', ['pool']);
    await waitFor('the wallet', () => T().wallets.current === 'pool');
    const pool = await rpc('getnewaddress', ['Pool payouts', 'bech32'], 'pool');
    const mine = (n = 1) => rpc('generatetoaddress', [n, pool]);
    await mine(110);

    // Two sidechains proposed, acked by the blocks after, activated; a third proposed later, collecting acks.
    await rpc('createsidechainproposal', [1, 'Thunder', 'Fast payments, large blocks, merged mined with Chains.']);
    await rpc('createsidechainproposal', [3, 'BitNames', 'Permanent names owned by addresses, with a DNS bridge.']);
    // The miner files one proposal a block; each activates after its own acks.
    for (let i = 0; i < 45 && (await rpc('listactivesidechains')).length < 2; i++) await mine(1);
    const active = await rpc('listactivesidechains');
    await check('two sidechains active', active.length === 2, JSON.stringify(active.map((s) => s.slot)));
    await rpc('createsidechainproposal', [6, 'zSide', 'Shielded payments (Orchard).']);
    await mine(1);

    // Deposits to both escrows, and payments with fees.
    for (const [slot, who, amount] of [[1, 'erin', 12], [3, 'nadia', 4], [1, 'otto', 2.5]]) {
      await rpc('createsidechaindeposit', [slot, await depositAddress(slot, who), amount], 'pool');
    }
    for (let i = 0; i < 6; i++) await rpc('sendtoaddress', [await rpc('getnewaddress', [], 'pool'), 1 + i / 4], 'pool');
    await mine(1);

    // Blind merged mining: requests of the sidechains taken by the next blocks, some with deposits and payments.
    for (let i = 0; i < 4; i++) {
      await rpc('createbmmrequest', [1, hex(32), Number((0.0005 + i * 0.0001).toFixed(8))], 'pool');
      if (i % 2 === 0) await rpc('createbmmrequest', [3, hex(32), 0.0003], 'pool');
      if (i === 1) await rpc('createsidechaindeposit', [3, await depositAddress(3, 'ivy'), 1.5], 'pool');
      await rpc('sendtoaddress', [await rpc('getnewaddress', [], 'pool'), 0.75], 'pool');
      await mine(1);
    }

    // A withdrawal bundle of Thunder: proposed (M3), then upvoted (M4) by the blocks after, with BMM.
    const payTo = await rpc('getnewaddress', ['withdrawal'], 'pool');
    const spk = (await rpc('getaddressinfo', [payTo], 'pool')).scriptPubKey;
    const bundleHex = '02000000' + '00' + '02' + le64(0) + '0a' + '6a08' + be64(1_000_000) + le64(300_000_000) + (spk.length / 2).toString(16).padStart(2, '0') + spk + '00000000';
    const bundle = await rpc('receivewithdrawalbundle', [1, bundleHex]);
    await mine(1);
    for (let i = 0; i < 3; i++) {
      await rpc('createbmmrequest', [1, hex(32), 0.0008], 'pool');
      await rpc('createbmmrequest', [3, hex(32), 0.0004], 'pool');
      if (i === 2) await rpc('createsidechaindeposit', [1, await depositAddress(1, 'zoe'), 3], 'pool');
      await mine(1);
    }
    const tip = await rpc('getblockcount');
    const top = await rpc('getblock', [await rpc('getblockhash', [tip]), 2]);
    await check('the last block has BMM accepts and a vote', top.tx.length > 2, `${top.tx.length} transactions`);

    T().savePrefs({ theme: 'midnight', layout: 'dashboard', uiMode: 'advanced', explorerCount: 50, explorerPageSize: 25 });
    T().go('explorer');
    const table = await waitFor('the list', () => $('[role="grid"][aria-label="Blocks"]'), 30000);
    await waitFor('the coinbase column', () => $$('[role="row"]', table).length > 10 && table.textContent.includes('BMM ·'), 30000);
    const headers = $$('[role="columnheader"]', table).map((h) => h.textContent.trim());
    await check('the list has reward, fees, BMM and coinbase columns', ['Reward', 'Fees', 'BMM', 'Coinbase'].every((h) => headers.some((x) => x.startsWith(h))), headers.join(','));
    await check('the top row says its BMM and vote', $$('[role="row"]', table)[1].textContent.includes('2 BMM · 1 vote'), $$('[role="row"]', table)[1].textContent);
    // The toasts of the wallet's new transactions out of the pictures.
    await sleep(1500);
    for (const b of $$('button[aria-label="Dismiss"]')) b.click();
    $('#content').scrollTop = 0;
    await sleep(800);
    await snapshot('explorer-bmm-list-midnight');

    // The block opened: its coinbase and what it did to the sidechains.
    const row = $$('[role="row"]', table).find((r) => r.textContent.trim().startsWith(String(tip)));
    row.dispatchEvent(new MouseEvent('dblclick', { bubbles: true }));
    const coinbase = await waitFor('the Coinbase section', () => $('section[aria-label="Coinbase"]'), 20000);
    const activity = $('section[aria-label="Drivechain activity"]');
    await check('the coinbase outputs are named', ['Miner payout', 'Witness commitment', 'M7 BMM accept', 'M4 bundle votes', 'M2 proposal ack'].every((w) => coinbase.textContent.includes(w)), coinbase.textContent.slice(0, 600));
    await check('the BMM accepts with their sidechains and bids', /Thunder/.test($('[aria-label="BMM accepts"]', activity).textContent) && /BitNames/.test($('[aria-label="BMM accepts"]', activity).textContent));
    await check('the vote upvotes the bundle', $('[aria-label="Bundle votes and bundles"]', activity).textContent.includes(bundle.hash.slice(0, 10)));
    await check('the deposit of the block', $('[aria-label="Escrow changes"]', activity).textContent.includes('s1_zoe_'));
    for (const b of $$('button[aria-label="Dismiss"]')) b.click();
    $('#explorer-title').scrollIntoView({ block: 'start' });
    await sleep(500);
    await snapshot('explorer-bmm-block-midnight');
    activity.scrollIntoView({ block: 'start' });
    await sleep(400);
    await snapshot('explorer-bmm-activity-midnight');

    await finish();
  } catch (e) {
    await finish(e instanceof Error ? e : new Error(JSON.stringify(e)));
  }
})();
