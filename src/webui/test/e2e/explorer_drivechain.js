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
    // One row per bundle: this block's upvote, its state and its score together.
    const bundleRow = $$('[role="row"]', $('[aria-label="Bundle votes and bundles"]', activity)).filter((r) => r.textContent.includes(bundle.hash.slice(0, 8)));
    await check('the vote upvotes the bundle', bundleRow.length === 1 && /Upvote/.test(bundleRow[0].textContent) && /Pending/.test(bundleRow[0].textContent) && /score/.test(bundleRow[0].textContent), bundleRow.map((r) => r.textContent).join(' | '));
    await check('the deposit of the block', $('[aria-label="Escrow changes"]', activity).textContent.includes('s1_zoe_'));
    for (const b of $$('button[aria-label="Dismiss"]')) b.click();
    $('#explorer-title').scrollIntoView({ block: 'start' });
    await sleep(500);
    await snapshot('explorer-bmm-block-midnight');
    activity.scrollIntoView({ block: 'start' });
    await sleep(400);
    await snapshot('explorer-bmm-activity-midnight');

    // Links: the pool's payout marked "yours"; a payment of the block opened from its list, the coins it spends with
    // their addresses (the pool's, "yours"), then the pool's address with its history; Back to the transaction and
    // to the block.
    const payouts = $('[aria-label="Payouts"]', coinbase);
    await waitFor('the payout marked yours', () => $('[data-ours]', payouts), 20000);
    await check('the pool payout is marked yours', /yours/.test(payouts.textContent), payouts.textContent.slice(0, 200));
    // A payment of the pool's wallet spending a coin of the pool's address, in one of the last blocks: its block
    // searched by height, the payment opened from the block's list.
    let paying = null;
    let payingBlock = null;
    for (let h = tip; h > tip - 15 && !paying; h--) {
      payingBlock = await rpc('getblock', [await rpc('getblockhash', [h]), 3]);
      paying = payingBlock.tx.slice(1).find((x) => x.vin.some((i) => i.prevout?.scriptPubKey?.address === pool));
    }
    await check('a payment spends a coin of the pool address', !!paying);
    if (payingBlock.hash !== top.hash) {
      const box = $('#explorer-search');
      box.value = String(payingBlock.height);
      box.dispatchEvent(new Event('input', { bubbles: true }));
      box.form.requestSubmit();
      await waitFor('the block of the payment', () => $('#explorer-title')?.textContent.includes(payingBlock.height.toLocaleString('en-US')) || $('#explorer-title')?.textContent.includes(String(payingBlock.height)), 20000);
    }
    const txLink = await waitFor('the payment in the list', () => $$('ul[aria-label="Transactions of the block"] button').find((b) => b.textContent.includes(paying.txid)), 20000);
    txLink.click();
    const txView = await waitFor('the transaction view', () => $('section[aria-label="Transaction"]'), 20000);
    await waitFor('its inputs with their addresses', () => $$('[data-ours]', txView).length > 0, 20000);
    await check('the coin it spends links to its transaction', $$('button', txView).some((b) => b.textContent.includes(`${paying.vin[0].txid}:${paying.vin[0].vout}`)));
    await check('its block is linked', $$('button', txView).some((b) => b.textContent.includes(payingBlock.hash)));
    for (const b of $$('button[aria-label="Dismiss"]')) b.click();
    $('#explorer-title').scrollIntoView({ block: 'start' });
    await sleep(500);
    await snapshot('explorer-links-tx-midnight');
    const poolLink = $$('button', txView).find((b) => b.textContent.trim() === pool);
    await check('the pool address is linked', !!poolLink);
    poolLink.click();
    const addrView = await waitFor('the address view', () => $('section[aria-label="Address"]'), 20000);
    const history = await waitFor('its history', () => $('table[aria-label="Transactions of the address"]', addrView), 20000);
    await check('the address view says it is the wallet\'s, with its label and history', /Pool payouts/.test(addrView.textContent) && $$('tbody tr', history).length >= 10, addrView.textContent.slice(0, 300));
    for (const b of $$('button[aria-label="Dismiss"]')) b.click();
    $('#explorer-title').scrollIntoView({ block: 'start' });
    await sleep(500);
    await snapshot('explorer-links-address-midnight');
    const back = () => $$('button', $('#explorer-title').closest('section')).find((b) => b.textContent.trim() === 'Back');
    back().click();
    await waitFor('back to the transaction', () => $('section[aria-label="Transaction"]'), 20000);
    back().click();
    await waitFor('back to the block', () => $('section[aria-label="Coinbase"]'), 20000);
    await check('Back returned to the block of the payment', $$('ul[aria-label="Transactions of the block"] button').some((b) => b.textContent.includes(paying.txid)));
    $('section[aria-label="Coinbase"]').scrollIntoView({ block: 'start' });
    await sleep(500);
    await snapshot('explorer-links-yours-midnight');

    await finish();
  } catch (e) {
    await finish(e instanceof Error ? e : new Error(JSON.stringify(e)));
  }
})();
