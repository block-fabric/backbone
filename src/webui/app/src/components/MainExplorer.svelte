<script lang="ts">
  // The block explorer (BlockExplorer of chains-qt): search by height, block hash or transaction id;
  // the latest blocks as a ribbon of boxes after the block being mined (components/BlockRibbon.svelte,
  // after the user's pool site), then the latest blocks and the transactions waiting in the mempool as
  // tables; a block with its transactions; a transaction with its inputs and outputs. The raw JSON of the
  // node is there too. Every hash, id and height links to its view (a block, a transaction, an address of this chain;
  // a bundle or a proposal to the Sidechains page), the addresses of the open wallet are marked "yours", and Back
  // returns along the views opened.
  import { t } from '../lib/i18n.svelte';
  import Amount from '../components/Amount.svelte';
  import BlockActivity from '../components/BlockActivity.svelte';
  import SafeText from '../components/SafeText.svelte';
  import AddressLink from '../components/AddressLink.svelte';
  import AddressView from '../components/AddressView.svelte';
  import { heightRuns, mayBeAddress, summaryText, type BlockDetail, type BlockSummary, type ScriptInfo } from '../lib/explorer';
  import { simpleMode } from '../lib/uimode.svelte';
  import BlockRibbon from '../components/BlockRibbon.svelte';
  import { coinbaseTag, type RibbonBlock } from '../lib/blocks';
  import { every } from '../lib/poll';
  import CopyButton from '../components/CopyButton.svelte';
  import Pager from '../components/Pager.svelte';
  import DataTable from '../components/DataTable.svelte';
  import Icon from '../components/Icon.svelte';
  import { errorText, inform } from '../lib/dialogs.svelte';
  import { formatBytes, formatCount, formatTime, shortHash } from '../lib/format';
  import { untrack } from 'svelte';
  import { css, fillHeight } from '../lib/actions';
  import { detail, showDetail } from '../lib/detail.svelte';
  import { nav } from '../lib/nav.svelte';
  import { host, rpc } from '../lib/rpc';
  import { node, prefs, savePrefs } from '../lib/state.svelte';
  import { filterAndSort, type Column } from '../lib/table';
  import { explorerQuery } from '../lib/tools';
  import { coinsToSats } from '../lib/units';

  /** Most waiting transactions listed (MAX_MEMPOOL_ROWS). */
  const MAX_MEMPOOL_ROWS = 1000;

  interface BlockRow {
    height: number;
    time: number;
    nTx: number;
    /** Read with the block (getblock) for the rows shown: unknown before. */
    size?: number;
    weight?: number;
    difficulty: number;
    hash: string;
    /** The id of its coinbase (for the miner's tag). */
    coinbase?: string;
    /** What its coinbase says (reward, fees, BMM, votes), read with the block; null when it cannot be read (pruned). */
    summary?: BlockSummary | null;
  }
  interface MempoolRow {
    txid: string;
    time: number;
    fee: number;
    vsize: number;
  }
  interface Block extends BlockRow {
    size: number;
    weight: number;
    tx: string[];
    previousblockhash?: string;
    nextblockhash?: string;
    merkleroot: string;
    confirmations: number;
    bits: string;
    nonce: number;
    version: number;
    mediantime: number;
    chainwork: string;
    [key: string]: unknown;
  }
  interface Tx {
    txid: string;
    hash: string;
    size: number;
    vsize: number;
    weight: number;
    locktime: number;
    version: number;
    vin: { txid?: string; vout?: number; coinbase?: string; sequence: number; prevout?: { height: number; value: number; scriptPubKey: { type: string; address?: string } } }[];
    vout: { value: number; n: number; scriptPubKey: { type: string; address?: string; asm: string; hex: string } }[];
    blockhash?: string;
    confirmations?: number;
    time?: number;
    /** With the coins it spent (verbosity 2, block undo data read). */
    fee?: number;
    [key: string]: unknown;
  }
  /** What the explorer shows: Back returns along them. */
  type View = { kind: 'block'; hash: string } | { kind: 'tx'; txid: string; blockHash: string | null; height?: number } | { kind: 'address'; address: string };
  /** The most views Back goes back along. */
  const MAX_TRAIL = 50;

  const TABS = [
    { id: 'blocks', label: 'Blocks' },
    { id: 'mempool', label: 'Waiting transactions' },
  ];
  let list = $state<'blocks' | 'mempool'>('blocks');
  let query = $state('');
  // Replaced whole, never changed in place: not made deep reactive (50 000 rows would be 50 000 proxies; audit 7 #12).
  let blocks = $state.raw<BlockRow[]>([]);
  let mempool = $state<MempoolRow[]>([]);
  let mempoolTotal = $state(0);
  let selectedBlock = $state<string | null>(null);
  let selectedTx = $state<string | null>(null);
  // The workspace layout shows the block selected in its detail panel.
  $effect(() => {
    const hash = selectedBlock;
    if (hash && detail.follow) untrack(() => showDetail({ kind: 'block', hash }));
  });
  let blockSort = $state<string | null>('height');
  let blockDir = $state<'asc' | 'desc'>('desc');
  // How many of the latest blocks are read, and how many a page shows (user decisions); sorted across all
  // of them, then paged.
  // 0: all of them.
  const BLOCK_POINTS = [25, 50, 100, 250, 500, 1000, 10000, 50000, 0];
  const BLOCK_PAGE_SIZES = [10, 25, 50, 100];
  /** The height of a row of the list (DataTable's), to size the list to its page. */
  const ROW = 34;
  const blockPoints = $derived(BLOCK_POINTS.includes(prefs.explorerCount) ? BLOCK_POINTS : [...BLOCK_POINTS.filter((n) => n > 0), prefs.explorerCount].sort((a, b) => a - b).concat(0));
  let blocksPage = $state(0);
  // Another order: from its first page.
  $effect(() => {
    void blockSort;
    void blockDir;
    blocksPage = 0;
  });
  let shown = $state<{ kind: 'block'; block: Block } | { kind: 'tx'; tx: Tx; blockHash: string | null } | { kind: 'address'; address: string; info: ScriptInfo } | null>(null);
  let trail = $state.raw<View[]>([]);
  let raw = $state(false);
  let loading = $state(false);
  const cache = new Map<string, BlockRow>();


  // The list: from the block index (host.explorerBlocks), in parts of at most EXPLORER_PART rows from a height down, up
  // to every block of the chain; on a new tip only the new rows are read (audit 7 #12: the whole chain read again under
  // the lock of the node at each block stalled it). One refresh at a time: one asked meanwhile runs after it. The size and
  // weight of a block (read with the block) for the rows a page shows and the boxes of the ribbon.
  let detailsVersion = $state(0);
  const EXPLORER_PART = 10000;
  type IndexRow = [number, string, number, number, number];
  const toRow = ([height, hash, time, nTx, difficulty]: IndexRow): BlockRow => cache.get(hash) ?? { height, hash, time, nTx, difficulty };
  /** Up to `count` rows from the height `from` down, in parts; fewer when the chain is shorter (or moved meanwhile). */
  async function readRows(from: number, count: number): Promise<BlockRow[]> {
    const out: BlockRow[] = [];
    while (out.length < count && from >= 0) {
      const part = await host.explorerBlocks(Math.min(EXPLORER_PART, count - out.length), from);
      // The rows start where asked, or the chain changed under the reading: what was read is kept, the rest next time.
      if (!part.length || part[0][0] !== from) break;
      out.push(...part.map(toRow));
      from = part[part.length - 1][0] - 1;
    }
    return out;
  }
  async function loadBlocks() {
    const count = prefs.explorerCount;
    const [tip] = await host.explorerBlocks(1);
    if (!tip) {
      blocks = [];
      return;
    }
    const [tipHeight, tipHash] = tip;
    const wanted = count === 0 ? tipHeight + 1 : Math.min(count, tipHeight + 1);
    // Nothing to do if the list is up to date.
    if (blocks.length === wanted && blocks[0]?.hash === tipHash) return;
    // The rows kept: those below the new ones, if the top one is still in the chain (no reorganization past it).
    let kept: BlockRow[] = [];
    const top = blocks[0];
    if (top && top.height <= tipHeight) {
      const [same] = await host.explorerBlocks(1, top.height);
      if (same && same[1] === top.hash) kept = blocks;
    }
    const fresh = await readRows(tipHeight, Math.min(wanted, kept.length ? tipHeight - kept[0].height : wanted));
    let next = [...fresh, ...kept].slice(0, wanted);
    // More of the older ones (a larger count chosen).
    if (next.length < wanted && next.length && next[next.length - 1].height > 0) next = next.concat(await readRows(next[next.length - 1].height - 1, wanted - next.length));
    blocks = next;
  }
  let loadingBlocks: Promise<void> | null = null;
  let loadAgain = false;
  async function refreshBlocks() {
    if (loadingBlocks) {
      loadAgain = true;
      return loadingBlocks;
    }
    loadingBlocks = (async () => {
      do {
        loadAgain = false;
        try {
          await loadBlocks();
        } catch {
          // Again at the next block.
        }
      } while (loadAgain);
    })();
    try {
      await loadingBlocks;
    } finally {
      loadingBlocks = null;
    }
  }
  /**
   * The size, weight and coinbase summary of these blocks, read once each: from the host (explorer.blocks with
   * details), a call per run of consecutive heights (100 blocks at most each), the blocks read by the node.
   */
  const reading = new Map<string, Promise<void>>();
  async function fillDetails(rows: readonly BlockRow[]) {
    // Those being read already (the ribbon and the table ask for the same blocks at once) are waited for, not asked again.
    const waiting = rows.map((b) => reading.get(b.hash)).filter((p) => p !== undefined);
    const missing = rows.filter((b) => !cache.has(b.hash) && !reading.has(b.hash));
    if (missing.length) {
      const read = (async () => {
        for (const run of heightRuns(missing.map((b) => b.height))) {
          try {
            const part = await host.explorerSummaries(run.count, run.from);
            for (const [height, hash, time, nTx, difficulty, summary] of part) {
              cache.set(hash, { height, hash, time, nTx, difficulty, size: summary?.size, weight: summary?.weight, coinbase: summary?.coinbase, summary });
            }
          } catch {
            // Again when the rows are shown next.
          }
        }
        for (const b of missing) reading.delete(b.hash);
        detailsVersion++;
      })();
      for (const b of missing) reading.set(b.hash, read);
      waiting.push(read);
    }
    await Promise.all(waiting);
  }
  const withDetails = (b: BlockRow): BlockRow => cache.get(b.hash) ?? b;

  async function refreshMempool() {
    try {
      const all = await rpc.raw<Record<string, { time: number; fees: { base: number }; vsize: number }>>('getrawmempool', [true]);
      const ids = Object.keys(all);
      mempoolTotal = ids.length;
      mempool = ids.slice(0, MAX_MEMPOOL_ROWS).map((txid) => ({ txid, time: all[txid].time, fee: all[txid].fees.base, vsize: all[txid].vsize }));
    } catch {
      mempool = [];
    }
  }

  $effect(() => {
    void node.tip?.height;
    void prefs.explorerCount;
    refreshBlocks();
  });
  $effect(() => {
    void node.mempool.size;
    void node.tip?.height;
    refreshMempool();
  });

  // --- The ribbon: the blocks of the list ("Last N blocks") as boxes, newest first. Their details (reward,
  //     tag, BMM) are read for the first boxes, and for more as the strip scrolls towards them. ---
  const RIBBON_STEP = 12;
  const RIBBON_MAX = 1000;
  let detailed = $state(RIBBON_STEP);
  interface Extra {
    subsidy: number;
    fees: number;
    feeRate: number;
    tag: string;
  }
  const extras = new Map<string, Extra>();
  let extrasVersion = $state(0);
  let bmmAt = $state.raw<Record<number, number[]>>({});
  let fresh = $state<string | null>(null);
  let nowSecs = $state(Math.floor(Date.now() / 1000));
  $effect(() => every(15000, () => (nowSecs = Math.floor(Date.now() / 1000))));

  async function extraOf(row: BlockRow): Promise<void> {
    if (extras.has(row.hash)) return;
    const stats = await rpc.raw<{ subsidy: number; totalfee: number; avgfeerate: number }>('getblockstats', [row.hash, ['subsidy', 'totalfee', 'avgfeerate']]);
    let tag = '';
    if (row.summary?.script !== undefined) tag = coinbaseTag(row.summary.script);
    else if (row.coinbase) {
      const cb = await rpc.raw<{ vin: { coinbase?: string }[] }>('getrawtransaction', [row.coinbase, true, row.hash]).catch(() => null);
      tag = coinbaseTag(cb?.vin[0]?.coinbase ?? '');
    }
    extras.set(row.hash, { subsidy: stats.subsidy, fees: stats.totalfee, feeRate: stats.avgfeerate, tag });
  }

  async function refreshRibbon(rows: readonly BlockRow[]) {
    const latest = rows.slice(0, detailed);
    if (!latest.length) return;
    try {
      // A block whose stats cannot be read (pruned) is drawn without them.
      // Their blocks first (the coinbase, the weight).
      await fillDetails(latest);
      for (const row of latest) await extraOf(withDetails(row)).catch(() => undefined);
      extrasVersion++;
      // Blind merged mining: the sidechains each block committed to (its coinbase's accepts, in the summaries).
      const at: Record<number, number[]> = { ...bmmAt };
      for (const row of latest) {
        const bmm = withDetails(row).summary?.bmm;
        if (bmm) at[row.height] = bmm;
      }
      bmmAt = at;
    } catch {
      // Again at the next block.
    }
  }

  /** The strip neared its end: the details of the next boxes. */
  function moreDetails() {
    const boxes = Math.min(blocks.length, RIBBON_MAX);
    if (detailed >= boxes) return;
    detailed = Math.min(boxes, detailed + RIBBON_STEP);
    void refreshRibbon(blocks);
  }

  const ribbon = $derived.by((): RibbonBlock[] => {
    void extrasVersion;
    void detailsVersion;
    // The boxes: the first RIBBON_MAX blocks of the list (all 50 000 would be too many to draw).
    return blocks.slice(0, RIBBON_MAX).map(withDetails).map((b) => {
      const x = extras.get(b.hash);
      return { height: b.height, hash: b.hash, time: b.time, nTx: b.nTx, weight: b.weight ?? null, subsidy: x?.subsidy ?? null, fees: x?.fees ?? null, feeRate: x?.feeRate ?? null, tag: x ? x.tag : null, bmm: bmmAt[b.height] ?? [] };
    });
  });

  // A new block while the page is shown: it slides in once.
  let topSeen: string | null = null;
  $effect(() => {
    const top = blocks[0]?.hash ?? null;
    if (!top) return;
    const first = topSeen === null;
    const changed = topSeen !== top;
    topSeen = top;
    untrack(() => refreshRibbon(blocks));
    if (first || !changed) return;
    fresh = top;
    const timer = setTimeout(() => (fresh = null), 1500);
    return () => clearTimeout(timer);
  });

  async function pickBlock(hash: string) {
    selectedBlock = hash;
    list = 'blocks';
    if (!detail.follow) await open({ kind: 'block', hash });
  }

  // --- Views, and Back along them ---
  function viewShown(): View | null {
    if (shown?.kind === 'block') return { kind: 'block', hash: shown.block.hash };
    if (shown?.kind === 'tx') return { kind: 'tx', txid: shown.tx.txid, blockHash: shown.blockHash };
    if (shown?.kind === 'address') return { kind: 'address', address: shown.address };
    return null;
  }
  const sameView = (a: View, b: View) => (a.kind === 'block' && b.kind === 'block' ? a.hash === b.hash : a.kind === 'tx' && b.kind === 'tx' ? a.txid === b.txid : a.kind === 'address' && b.kind === 'address' && a.address === b.address);
  function display(v: View): Promise<boolean> {
    if (v.kind === 'block') return showBlock(v.hash);
    if (v.kind === 'tx') return showTx(v.txid, v.blockHash, v.height);
    return showAddress(v.address);
  }
  /** Open a view; the one shown before goes on the trail Back returns along. */
  async function open(v: View): Promise<boolean> {
    const from = viewShown();
    if (!(await display(v))) return false;
    if (from && !sameView(from, v)) trail = [...trail, from].slice(-MAX_TRAIL);
    const title = document.getElementById('explorer-title');
    title?.scrollIntoView?.({ block: 'nearest' });
    return true;
  }
  async function back() {
    const v = trail[trail.length - 1];
    if (!v) return;
    trail = trail.slice(0, -1);
    await display(v);
  }
  const openBlock = (hash: string) => open({ kind: 'block', hash });
  const openTx = (txid: string, blockHash: string | null, height?: number) => open({ kind: 'tx', txid, blockHash, height });
  const openAddress = (address: string) => open({ kind: 'address', address });
  async function openHeight(height: number) {
    try {
      await openBlock(await rpc.call('getblockhash', [height]));
    } catch (e) {
      await inform(t('Block Explorer'), errorText(e));
    }
  }
  /** A transaction of a block shown or a coin of a wallet (its block's height known; -1 waiting). */
  const openTxAt = (txid: string, height: number) => openTx(txid, null, height >= 0 ? height : undefined);


  // The coinbase and drivechain activity of the block shown (host explorer.block); null until read, or if it cannot be.
  let activity = $state<BlockDetail | null>(null);
  async function showBlock(hash: string): Promise<boolean> {
    try {
      const block = await rpc.raw<Block>('getblock', [hash, 1]);
      shown = { kind: 'block', block };
      selectedTx = null;
      activity = null;
      host.explorerBlock(hash).then(
        (d) => {
          if (shown?.kind === 'block' && shown.block.hash === hash) activity = d;
        },
        () => undefined,
      );
      return true;
    } catch {
      return false;
    }
  }

  // Which transactions spend the outputs of the one shown (gettxspendingprevout: from the mempool, or from
  // -txospenderindex if it is on); by output number.
  let spenders = $state<Record<number, { txid: string; blockHash: string | null }>>({});
  async function showTx(txid: string, blockHash: string | null, height?: number): Promise<boolean> {
    try {
      // A transaction of a known block is read with it (no -txindex needed): from a coin's height when its block is not named.
      if (!blockHash && height !== undefined) blockHash = await rpc.call('getblockhash', [height]).catch(() => null);
      const at = blockHash ? [txid, 2, blockHash] : [txid, 2];
      // Verbosity 2 adds the coins spent (block undo data); without it (pruned), the transaction alone.
      const tx = await rpc.raw<Tx>('getrawtransaction', at).catch(() => rpc.raw<Tx>('getrawtransaction', blockHash ? [txid, 1, blockHash] : [txid, 1]));
      shown = { kind: 'tx', tx, blockHash: blockHash ?? tx.blockhash ?? null };
      spenders = {};
      const outs = tx.vout.slice(0, 100).map((o) => ({ txid: tx.txid, vout: o.n }));
      rpc.raw<{ txid: string; vout: number; spendingtxid?: string; blockhash?: string }[]>('gettxspendingprevout', [outs]).then(
        (r) => {
          if (shown?.kind !== 'tx' || shown.tx.txid !== tx.txid) return;
          const found: Record<number, { txid: string; blockHash: string | null }> = {};
          for (const s of r) if (s.spendingtxid) found[s.vout] = { txid: s.spendingtxid, blockHash: s.blockhash ?? null };
          spenders = found;
        },
        () => undefined,
      );
      return true;
    } catch {
      return false;
    }
  }

  async function showAddress(address: string): Promise<boolean> {
    try {
      const info = await rpc.call('validateaddress', [address]);
      if (!info.isvalid) return false;
      shown = { kind: 'address', address: info.address ?? address, info };
      return true;
    } catch {
      return false;
    }
  }

  async function search(input = query) {
    const q = explorerQuery(input);
    loading = true;
    try {
      if (q.kind === 'height') {
        try {
          const hash = await rpc.call('getblockhash', [q.height]);
          await openBlock(hash);
          return;
        } catch (e) {
          await inform(t('Block Explorer'), errorText(e));
          return;
        }
      }
      if (q.kind === 'hash') {
        if (await openBlock(q.hash)) return;
        if (await openTx(q.hash, null)) return;
        await inform(
          t('Block Explorer'),
          t('No block has this hash, and no transaction with this id is waiting to be mined. Finding a mined transaction by its id alone requires the node to run with -txindex.'),
        );
        return;
      }
      // An address of this chain (the node says), of any kind.
      if (mayBeAddress(input) && (await openAddress(input.trim()))) return;
      await inform(t('Block Explorer'), t('Enter a block height, a block hash, a transaction id or an address.'));
    } finally {
      loading = false;
    }
  }

  // A search given with the page ("search:<hash>", from the overview and the transactions).
  $effect(() => {
    const t = nav.tab;
    if (!t.startsWith('search:')) return;
    const q = t.slice('search:'.length);
    nav.tab = '';
    query = q;
    search(q);
  });

  // The columns, with the narrowest width of the page each is shown at (the least useful go first) and whether
  // Simple mode shows it. The reward, fees, BMM and coinbase come with the summaries of the rows a page shows.
  const ALL_BLOCK_COLUMNS: (Column<BlockRow> & { from: number; simple: boolean })[] = [
    { key: 'height', label: 'Height', value: (b) => b.height, width: '5.5rem', align: 'end', from: 0, simple: true },
    { key: 'time', label: 'Time', value: (b) => b.time, width: '8rem', from: 0, simple: true },
    { key: 'nTx', label: 'Transactions', value: (b) => b.nTx, width: '6.5rem', align: 'end', from: 640, simple: true },
    { key: 'reward', label: 'Reward', value: (b) => b.summary?.reward, width: '11rem', align: 'end', from: 0, simple: true },
    { key: 'fees', label: 'Fees', value: (b) => b.summary?.fees, width: '10.5rem', align: 'end', from: 900, simple: false },
    { key: 'bmm', label: 'BMM', value: (b) => b.summary?.bmm.length, width: '4rem', align: 'end', from: 700, simple: false },
    { key: 'coinbase', label: 'Coinbase', value: (b) => (b.summary ? `${b.summary.payout} ${summaryText(b.summary)}` : undefined), width: 'minmax(13rem, 1.5fr)', from: 760, simple: false, noFilter: true },
    { key: 'size', label: 'Size', value: (b) => b.size, width: '5.5rem', align: 'end', from: 1350, simple: true },
    { key: 'weight', label: 'Weight', value: (b) => b.weight, width: '6rem', align: 'end', from: 1650, simple: false },
    { key: 'difficulty', label: 'Difficulty', value: (b) => b.difficulty, width: '8.5rem', align: 'end', from: 1550, simple: false },
    { key: 'hash', label: 'Hash', value: (b) => b.hash, width: 'minmax(8rem, 1fr)', mono: true, from: 1200, simple: true },
  ];
  let pageEl: HTMLDivElement | undefined = $state();
  let pageWidth = $state(1400);
  $effect(() => {
    if (!pageEl || typeof ResizeObserver === 'undefined') return;
    const o = new ResizeObserver(() => pageEl && (pageWidth = pageEl.clientWidth));
    o.observe(pageEl);
    pageWidth = pageEl.clientWidth;
    return () => o.disconnect();
  });
  const simple = $derived(simpleMode());
  const blockColumns = $derived(ALL_BLOCK_COLUMNS.filter((c) => pageWidth >= c.from && (!simple || c.simple)));
  const sortedBlocks = $derived.by(() => {
    void detailsVersion;
    return filterAndSort(blocks.map(withDetails), ALL_BLOCK_COLUMNS, '', blockSort, blockDir);
  });
  const blocksShown = $derived(sortedBlocks.slice(blocksPage * prefs.explorerPageSize, (blocksPage + 1) * prefs.explorerPageSize));
  $effect(() => {
    const rows = blocksShown;
    untrack(() => fillDetails(rows));
  });
  const mempoolColumns: Column<MempoolRow>[] = [
    { key: 'time', label: 'Received', value: (m) => m.time, width: '11.5rem' },
    { key: 'fee', label: 'Fee', value: (m) => m.fee, width: '9rem', align: 'end' },
    { key: 'vsize', label: 'Virtual size', value: (m) => m.vsize, width: '7rem', align: 'end' },
    { key: 'txid', label: 'Transaction id', value: (m) => m.txid, width: 'minmax(14rem, 1fr)', mono: true },
  ];

  function rawJson(value: Record<string, unknown>, omit: string[]): string {
    const out: Record<string, unknown> = {};
    for (const [k, v] of Object.entries(value)) if (!omit.includes(k)) out[k] = v;
    return JSON.stringify(out, null, 2);
  }
  const txValueOut = $derived(shown?.kind === 'tx' ? shown.tx.vout.reduce((sum, o) => sum + coinsToSats(o.value), 0n) : 0n);
</script>

<div class="page explorer" bind:this={pageEl}>
  <header class="page-head"><h1>Block explorer</h1></header>
  <form class="search row" onsubmit={(e) => { e.preventDefault(); search(); }}>
    <div class="box grow">
      <Icon name="search" size={17} />
      <input id="explorer-search" type="search" class="mono" placeholder="Block height, block hash, transaction id or address" spellcheck="false" autocomplete="off" bind:value={query} aria-label="Search" />
    </div>
    <button type="submit" class="btn primary" disabled={loading}>Search</button>
    <label class="row count">
      <span class="muted">Last</span>
      <select value={prefs.explorerCount} onchange={(e) => { savePrefs({ explorerCount: Number((e.currentTarget as HTMLSelectElement).value) }); blocksPage = 0; }} aria-label="Blocks listed">
        {#each blockPoints as n (n)}<option value={n}>{n === 0 ? t('All') : formatCount(n)}</option>{/each}
      </select>
      <span class="muted">blocks</span>
    </label>
    <button type="button" class="btn" onclick={() => { cache.clear(); blocks = []; refreshBlocks(); refreshMempool(); }}>Refresh</button>
  </form>

  <BlockRibbon blocks={ribbon} selected={selectedBlock} {fresh} now={nowSecs} onselect={pickBlock} onmore={moreDetails} />

  <div class="list-bar">
  <div class="tabs" role="tablist" aria-label="Lists" id="explorer-lists">
    {#each TABS as tb (tb.id)}
      <button type="button" role="tab" aria-selected={list === tb.id} onclick={() => (list = tb.id as 'blocks' | 'mempool')}>{tb.id === 'mempool' ? `${t(tb.label)} (${mempoolTotal})` : t(tb.label)}</button>
    {/each}
  </div>
    {#if list === 'blocks'}
      <div class="block-pages"><Pager total={sortedBlocks.length} bind:page={blocksPage} size={prefs.explorerPageSize} sizes={BLOCK_PAGE_SIZES} label="blocks" onsize={(n) => savePrefs({ explorerPageSize: n })} /></div>
    {/if}
  </div>
  {#if list === 'blocks'}
    <!-- As tall as the rows of the page (10 to 100, user decision): the page scrolls, not the list. -->
    <div class="table-box fit" use:css={{ height: `${(Math.max(1, blocksShown.length) + 1) * ROW + 2}px` }}>
      <DataTable fit={pageWidth < 1000} rows={blocksShown} columns={blockColumns} key={(b) => b.hash} bind:selected={selectedBlock} bind:sortKey={blockSort} bind:sortDir={blockDir} label="Blocks" empty="Loading…" onactivate={(b) => openBlock(b.hash)}>
        {#snippet cell(b, column)}
          {#if column.key === 'time'}{formatTime(b.time, true)}
          {:else if column.key === 'size'}{#if b.size === undefined}<span class="faint">…</span>{:else}{formatBytes(b.size)}{/if}
          {:else if column.key === 'weight' && b.weight === undefined}<span class="faint">…</span>
          {:else if column.key === 'difficulty'}{b.difficulty.toPrecision(8)}
          {:else if column.key === 'reward' || column.key === 'fees' || column.key === 'bmm' || column.key === 'coinbase'}
            {#if b.summary === undefined}<span class="faint">…</span>
            {:else if b.summary === null}<span class="faint">–</span>
            {:else if column.key === 'reward'}<Amount sats={b.summary.reward} masked={false} />
            {:else if column.key === 'fees'}<Amount sats={b.summary.fees} masked={false} />
            {:else if column.key === 'bmm'}{#if b.summary.bmm.length}<span class="bmm-count" title={b.summary.bmm.map((s) => t('slot %1', s)).join(', ')}>{b.summary.bmm.length}</span>{:else}<span class="faint">0</span>{/if}
            {:else}<span class="cb" title={`${b.summary.payout} ${summaryText(b.summary)}`}>{#if b.summary.payout}<AddressLink address={b.summary.payout} short={8} onopen={openAddress} />{/if} <span class="faint">{summaryText(b.summary)}</span></span>{/if}
          {:else if column.key === 'height'}<button type="button" class="mono linkish" onclick={() => openBlock(b.hash)}>{formatCount(b.height)}</button>
          {:else if column.key === 'nTx' || column.key === 'weight'}{formatCount(Number(column.value(b)))}
          {:else}<button type="button" class="mono linkish cell-link" onclick={() => openBlock(b.hash)}>{b.hash}</button>{/if}
        {/snippet}
      </DataTable>
    </div>
  {:else}
    <div class="table-box" use:fillHeight>
      <DataTable rows={mempool} columns={mempoolColumns} key={(m) => m.txid} bind:selected={selectedTx} label="Waiting transactions" empty="No transaction is waiting" onactivate={(m) => openTx(m.txid, null)}>
        {#snippet cell(m, column)}
          {#if column.key === 'time'}{formatTime(m.time)}
          {:else if column.key === 'fee'}<Amount sats={coinsToSats(m.fee)} masked={false} />
          {:else if column.key === 'vsize'}{formatCount(m.vsize)} vB
          {:else}<button type="button" class="mono linkish cell-link" onclick={() => openTx(m.txid, null)}>{m.txid}</button>{/if}
        {/snippet}
      </DataTable>
    </div>
  {/if}
  <p class="faint hint">Double-click a block or a transaction to see its details, or click a link.</p>

  {#snippet backButton()}
    {#if trail.length}<button type="button" class="btn small ghost" onclick={back} title={t('Back to what was shown before')}><Icon name="receive" size={14} /> Back</button>{/if}
  {/snippet}

  {#if shown?.kind === 'block'}
    {@const b = shown.block}
    <section class="panel detail" aria-label="Block {b.height}">
      <div class="panel-head">
        <h2 id="explorer-title">Block {formatCount(b.height)}</h2>
        <div class="row">
          {@render backButton()}
          {#if b.previousblockhash}<button type="button" class="btn small" onclick={() => openBlock(b.previousblockhash!)}><Icon name="receive" size={14} /> Previous</button>{/if}
          {#if b.nextblockhash}<button type="button" class="btn small" onclick={() => openBlock(b.nextblockhash!)}>Next <Icon name="send" size={14} /></button>{/if}
          <button type="button" class="btn small ghost" aria-pressed={raw} onclick={() => (raw = !raw)}>{raw ? 'Details' : 'Raw JSON'}</button>
        </div>
      </div>
      {#if raw}
        <pre class="raw mono panel-body">{rawJson(b, ['tx'])}</pre>
      {:else}
        <div class="panel-body block-grid">
          <dl class="facts">
            <dt>Hash</dt><dd class="mono break">{b.hash} <CopyButton text={b.hash} label="Copy the block hash" /></dd>
            <dt>Time</dt><dd>{formatTime(b.time)} <span class="faint">(median {formatTime(b.mediantime)})</span></dd>
            <dt>Confirmations</dt><dd class="num">{formatCount(b.confirmations)}</dd>
            <dt>Transactions</dt><dd class="num">{formatCount(b.tx.length)}</dd>
            <dt>Size</dt><dd>{formatBytes(b.size)} · weight {formatCount(b.weight)}</dd>
            <dt>Difficulty</dt><dd class="num">{b.difficulty.toPrecision(10)} <span class="faint mono">bits {b.bits}</span></dd>
            {#if b.previousblockhash}<dt>Previous block</dt><dd class="mono break"><button type="button" class="mono linkish" onclick={() => openBlock(b.previousblockhash!)}><span class="faint">{formatCount(b.height - 1)} ·</span> {b.previousblockhash}</button></dd>{/if}
            <dt>Merkle root</dt><dd class="mono break">{b.merkleroot}</dd>
            <dt>Version / nonce</dt><dd class="mono">{b.version} / {b.nonce}</dd>
            <dt>Reward</dt><dd>{#if activity}<Amount sats={activity.reward} masked={false} /> <span class="faint">(subsidy <Amount sats={activity.subsidy} masked={false} />{#if activity.fees !== null}&nbsp;+ fees <Amount sats={activity.fees} masked={false} />{/if})</span>{:else}<span class="faint">…</span>{/if}</dd>
          </dl>
          <div>
            <h3>Transactions of the block</h3>
            <ul class="txs" aria-label="Transactions of the block">
              {#each b.tx as txid, i (txid)}
                <li><button type="button" class="mono" onclick={() => openTx(txid, b.hash)}><span class="faint">{i}</span> {txid}</button></li>
              {/each}
            </ul>
          </div>
        </div>
      {/if}
    </section>
    {#if activity && !raw}<BlockActivity detail={activity} ontx={(txid) => openTx(txid, b.hash)} onaddress={openAddress} />{/if}
  {:else if shown?.kind === 'tx'}
    {@const tx = shown.tx}
    <section class="panel detail" aria-label="Transaction">
      <div class="panel-head">
        <h2 id="explorer-title" class="break">Transaction <span class="mono">{shortHash(tx.txid, 12)}</span></h2>
        <div class="row">
          {@render backButton()}
          {#if shown.blockHash}<button type="button" class="btn small" onclick={() => openBlock(shown!.kind === 'tx' && shown!.blockHash ? shown!.blockHash : '')}>Its block</button>{/if}
          <button type="button" class="btn small ghost" aria-pressed={raw} onclick={() => (raw = !raw)}>{raw ? 'Details' : 'Raw JSON'}</button>
        </div>
      </div>
      {#if raw}
        <pre class="raw mono panel-body">{rawJson(tx, ['hex'])}</pre>
      {:else}
        <div class="panel-body">
          <dl class="facts">
            <dt>Transaction id</dt><dd class="mono break">{tx.txid} <CopyButton text={tx.txid} label="Copy the transaction id" /></dd>
            <dt>Status</dt><dd>{tx.confirmations ? `${formatCount(tx.confirmations)} confirmations` : 'Waiting to be mined'}{tx.time ? `, ${formatTime(tx.time)}` : ''}</dd>
            {#if shown.blockHash}{@const at = tx.confirmations && node.tip ? node.tip.height - tx.confirmations + 1 : null}<dt>Block</dt><dd class="mono break"><button type="button" class="mono linkish" onclick={() => openBlock(shown!.kind === 'tx' && shown!.blockHash ? shown!.blockHash : '')}>{#if at !== null}<span class="faint">{formatCount(at)} ·</span> {/if}{shown.blockHash}</button></dd>{/if}
            <dt>Size</dt><dd>{formatCount(tx.size)} bytes · {formatCount(tx.vsize)} vB · weight {formatCount(tx.weight)}</dd>
            <dt>Paid out</dt><dd><Amount sats={txValueOut} masked={false} />{#if tx.fee !== undefined} <span class="faint">· fee <Amount sats={coinsToSats(tx.fee)} masked={false} /></span>{/if}</dd>
          </dl>
          <div class="io">
            <div>
              <h3>Inputs ({tx.vin.length})</h3>
              <ul class="plain">
                {#each tx.vin as input, i (i)}
                  <li class="mono break">
                    {#if input.coinbase}<span class="badge accent">coinbase</span> {input.coinbase}
                    {:else}
                      <span class="io-line">
                        <button type="button" class="mono linkish" title={t('The transaction of the coin spent')} onclick={() => openTx(input.txid ?? '', null, input.prevout?.height)}>{input.txid}:{input.vout}</button>
                        {#if input.prevout}<span class="io-what">{#if input.prevout.scriptPubKey.address}<AddressLink address={input.prevout.scriptPubKey.address} onopen={openAddress} />{:else}<span class="faint">{input.prevout.scriptPubKey.type}</span>{/if} <Amount sats={coinsToSats(input.prevout.value)} masked={false} /></span>{/if}
                      </span>
                    {/if}
                  </li>
                {/each}
              </ul>
            </div>
            <div>
              <h3>Outputs ({tx.vout.length})</h3>
              <ul class="plain">
                {#each tx.vout as out (out.n)}
                  <li>
                    <span class="faint">{out.n}</span>
                    <span class="mono break">{#if out.scriptPubKey.address}<AddressLink address={out.scriptPubKey.address} onopen={openAddress} />{:else}{out.scriptPubKey.type}{/if}{#if spenders[out.n]}{@const by = spenders[out.n]}<br /><span class="faint spent">spent by</span> <button type="button" class="mono linkish" title={by.txid} onclick={() => openTx(by.txid, by.blockHash)}>{shortHash(by.txid, 8)}</button>{/if}</span>
                    <Amount sats={coinsToSats(out.value)} masked={false} />
                  </li>
                {/each}
              </ul>
            </div>
          </div>
        </div>
      {/if}
    </section>
  {:else if shown?.kind === 'address'}
    {@const a = shown}
    <AddressView address={a.address} info={a.info} ontx={(txid, height) => openTxAt(txid, height)} onblock={openHeight} actions={backButton} />
  {/if}
</div>

<style>
  .search {
    margin-top: 0.6rem;
    flex-wrap: wrap;
  }
  .box {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    padding: 0 0.6rem;
    border: 1px solid var(--line-strong);
    border-radius: var(--radius-s);
    background: var(--surface);
    color: var(--text-muted);
  }
  .box input {
    border: 0;
    padding-left: 0;
    background: transparent;
  }
  .box input:focus-visible {
    outline: none;
  }
  .box:has(input:focus-visible) {
    outline: 2px solid var(--focus);
    outline-offset: 2px;
  }
  .count select {
    width: auto;
  }
  .list-bar {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: 0.5rem 1rem;
  }
  .block-pages {
    margin-left: auto;
  }
  .block-pages :global(.pager) {
    padding: 0;
  }
  /* The list takes the height the window has left (fillHeight), 16rem at least. */
  .table-box {
    height: 21rem;
    min-height: 16rem;
  }
  .table-box.fit {
    min-height: 0;
  }
  .cb {
    display: inline-flex;
    gap: 0.4rem;
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .bmm-count {
    font-weight: 600;
    color: var(--accent);
  }
  .hint {
    margin: 0.4rem 0 1rem;
    font-size: 0.86rem;
  }
  .detail {
    margin-bottom: 1rem;
  }
  .block-grid {
    display: grid;
    grid-template-columns: minmax(0, 1.2fr) minmax(0, 1fr);
    gap: 1.5rem;
  }
  .txs {
    margin: 0.5rem 0 0;
    padding: 0;
    list-style: none;
    max-height: 18rem;
    overflow: auto;
    border: 1px solid var(--line);
    border-radius: var(--radius-s);
  }
  .txs button,
  .linkish {
    display: block;
    width: 100%;
    padding: 0.3rem 0.6rem;
    border: 0;
    background: none;
    color: var(--link);
    text-align: left;
    cursor: pointer;
    overflow-wrap: anywhere;
  }
  .linkish {
    padding: 0;
    display: inline;
    width: auto;
  }
  .linkish:hover,
  .linkish:focus-visible {
    text-decoration: underline;
  }
  .cell-link {
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    max-width: 100%;
  }
  .io-line {
    display: flex;
    flex-direction: column;
    gap: 0.1rem;
  }
  .io-what {
    display: inline-flex;
    flex-wrap: wrap;
    gap: 0.5rem;
    align-items: baseline;
  }

  .txs button:hover {
    background: var(--raised);
  }
  .raw {
    margin: 0;
    max-height: 30rem;
    overflow: auto;
    white-space: pre;
  }
  .io {
    display: grid;
    grid-template-columns: minmax(0, 1fr) minmax(0, 1fr);
    gap: 1.5rem;
    margin-top: 1rem;
  }
  .plain {
    margin: 0.5rem 0 0;
    padding: 0;
    list-style: none;
    display: flex;
    flex-direction: column;
    gap: 0.35rem;
  }
  .plain li {
    display: flex;
    gap: 0.6rem;
    align-items: baseline;
    font-size: 0.9rem;
  }
  .plain li .break {
    flex: 1;
  }
  @media (max-width: 1000px) {
    .block-grid,
    .io {
      grid-template-columns: minmax(0, 1fr);
    }
  }
</style>
