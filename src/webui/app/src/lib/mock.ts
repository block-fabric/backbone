// A pretend host, for working on the interface in a browser (npm run dev) and for the tests.
// Never part of the build of chains-app (only loaded when import.meta.env.DEV is set).
import { dispatch, setTransport, type BridgeRequest } from './bridge';
import { BUILD_PRODUCT } from './brand.svelte';

export interface MockOptions {
  phase?: string;
  wallets?: string[];
}

export function installMock(options: MockOptions = {}): void {
  let height = 1200;
  const start = Math.floor(Date.now() / 1000);
  const prefs: Record<string, unknown> = {};
  const wallets = options.wallets ?? ['alice'];
  // The browser preview shows the integrated title bar, as chains-app on Linux.
  const frameState = { titleBar: 'integrated', supported: true, frameless: true, restartNeeded: false, maximized: false };
  const hash = (h: number) => h.toString(16).padStart(64, '0');

  const host: Record<string, (params: Record<string, unknown>) => unknown> = {
    'app.hello': () => ({
      phase: options.phase ?? 'ready',
      phaseData: {},
      dialogs: [],
      info: { name: BUILD_PRODUCT, version: 'v32.0.0-dev (preview)', bugreport: '', network: 'regtest', networkLabel: 'regtest', datadir: '/tmp/preview', walletEnabled: true, devtools: true, test: false, platform: 'browser', blockSpacing: 600 },
    }),
    'app.help': () => ({ usage: `Usage: ${BUILD_PRODUCT.toLowerCase()}-app [options] [URI]`, options: 'Options:\n\n  -?\n       Print this help message and exit' }),
    'app.about': () => ({ name: BUILD_PRODUCT, version: 'preview', license: 'Distributed under the MIT software license.' }),
    'prefs.get': () => prefs,
    'prefs.set': (p) => Object.assign(prefs, p),
    'node.state': () => ({
      connections: 8,
      networkActive: true,
      warnings: [],
      tip: { height, time: start - 30, hash: hash(height), progress: 1, state: 'synced', ibd: false, loadingBlocks: false },
      headerTip: { height, time: start - 30 },
      wallets: wallets.map((name) => ({ name, encrypted: true, locked: true, privateKeysDisabled: false, hd: true })),
    }),
    'window.setWallet': () => true,
    'window.state': () => ({ ...frameState }),
    'window.minimize': () => true,
    'window.toggleMaximize': () => {
      frameState.maximized = !frameState.maximized;
      queueMicrotask(() => dispatch([{ type: 'window', data: { maximized: frameState.maximized } }]));
      return true;
    },
    'window.close': () => true,
    'window.startMove': () => true,
    'window.startResize': () => true,
    'window.setTitleBar': (p) => {
      frameState.titleBar = p.mode === 'system' ? 'system' : 'integrated';
      frameState.frameless = frameState.titleBar === 'integrated';
      return { ...frameState };
    },
    // The unified app (doc/unified-app.md): Layers on, the others off.
    'sidechains.list': () => ({
      network: 'regtest',
      rpcServer: true,
      chainsDataDir: '/tmp/preview',
      chains: [
        ['layers', 'Layers', 'Make and trade assets on a fast chain: a block every 15 seconds.', 4, 400, true],
        ['fastblocks', 'FastBlocks', 'Quick, cheap payments: a block every 15 seconds, kept honest by stake.', 3, 400, true],
        ['thunder', 'Thunder', 'Very large blocks, for many small payments at a low fee.', 10, 600, false],
      ].map(([stem, name, description, diskGB, ramMB, staking], i) => ({
        stem, name, description, diskGB, ramMB, staking, slot: 7, enabled: i === 0, state: i === 0 ? 'running' : 'disabled', pid: i === 0 ? 4242 : 0, restarts: 0, ready: i === 0,
        dataDir: `/tmp/preview/sidechains/${stem}/data`, defaultDataDir: `/tmp/preview/sidechains/${stem}/data`, customDataDir: false, hasData: i === 0, freeGB: 180, program: `/app/${stem}d`, rpcPort: 29254 + i * 100, port: 29255 + i * 100, wallet: 'shared',
      })),
    }),
    'sidechains.overview': (p) => ({ stem: p.stem, name: 'Layers', state: 'running', mainchainHeight: height, height: 812, mainchainConnected: true, mainchainSeen: height, synced: true, blocks: Array.from({ length: 6 }, (_, i) => ({ height: 812 - i, hash: hash(9000 + i), time: start - i * 15, txs: 1 + (i % 3) })), wallet: { name: 'main', trusted: 3.25, pending: 0, immature: 0 } }),
    'clipboard.write': () => true,
    'clipboard.read': () => '',
    'intro.space': () => ({ exists: true, isDirectory: true, free: 512e9 }),
    // A wallet with a history, for the pages and the dashboard of the preview.
    'wallet.info': (p) => ({ name: p.wallet ?? '', encrypted: true, locked: true, privateKeysDisabled: false, externalSigner: false, hd: true, canGetAddresses: true, taproot: true, defaultAddressType: 'bech32', confirmTarget: 6, requiredFeePerK: 1000, maxTxFee: 10_000_000, multiwallet: wallets.length > 1, balance: 12.5, unconfirmed: 0.25, immature: 50 }),
    'wallet.transactions': () => ({ height, hash: hash(height), records: history() }),
    'wallet.transaction': (p) => ({ txid: p.txid, coinbase: false, rows: [{ k: 'Status', text: '6 confirmations' }], notes: [], ids: [{ k: 'Transaction ID', text: String(p.txid) }], messages: [] }),
    'wallet.txActions': () => ({ canAbandon: false, canBump: true, hex: '' }),
    'wallet.addresses': () => [],
    'wallet.requests': () => [],
    'wallet.fee': () => ({ feePerK: 1000, fallback: false, target: 6, requiredFeePerK: 1000 }),
  };

  /** Records as the host sends them (RecordJson): a salary, some spending, mined coins. */
  const history = () => {
    const rows: unknown[][] = [];
    const items: [string, string, string, number, number][] = [
      ['recv', 'Salary', 'rchn1qsalary', 40, 1_250_000_000],
      ['send', 'Coffee shop', 'rchn1qcoffee', 34, -42_002_820],
      ['mined', '', '', 30, 5_000_000_000],
      ['send', 'Rent', 'rchn1qrent', 24, -400_002_820],
      ['recv', 'Refund', 'rchn1qrefund', 16, 75_000_000],
      ['send', 'Savings', 'rchn1qsavings', 9, -300_002_820],
      ['recv', 'Bookshop', 'rchn1qbook', 3, 15_000_000],
      ['send', 'Bookshop', 'rchn1qbook2', 0, -15_002_820],
    ];
    items.forEach(([type, label, address, ago, amount], i) => {
      const depth = ago === 0 ? 0 : ago * 6;
      rows.push([(i + 10).toString(16).padStart(64, 'a'), 0, type, address, label, start - ago * 86400, amount > 0 ? amount : 0, amount < 0 ? amount : 0, depth === 0 ? 'unconfirmed' : type === 'mined' && depth < 100 ? 'immature' : 'confirmed', depth, 0, depth > 0, `k${i}`]);
    });
    return rows;
  };

  const rpc: Record<string, (params: unknown[]) => unknown> = {
    getbalances: () => ({ mine: { trusted: 12.5, untrusted_pending: 0.25, immature: 50 } }),
    getblockheader: (p) => {
      const h = parseInt(String(p[0]), 16);
      return { height: h, time: start - (height - h) * 600, nTx: (h * 7) % 13 + 1, previousblockhash: h > 0 ? hash(h - 1) : undefined };
    },
    getblockchaininfo: () => ({ chain: 'regtest', blocks: height, headers: height, time: start, size_on_disk: 3_200_000, pruned: false }),
    getnetworkinfo: () => ({ subversion: `/${BUILD_PRODUCT}:32.0.0/`, connections: 8, connections_in: 2, connections_out: 6, localaddresses: [] }),
    getmempoolinfo: () => ({ size: 3, usage: 4096 }),
    uptime: () => 3600,
    validateaddress: (p) => ({ isvalid: String(p[0]).startsWith('rchn1') }),
    // The user agents and services of eCash's main network (2026-11-01): Backbone, eCash's client old and new, long
    // user agents (a row as wide as the others), full and pruned nodes; more than the table shows, to scroll.
    getpeerinfo: () =>
      [
        '/eCash Satoshi:1.2.0(Bitcoin Core 32.1.0)/',
        '/eCash Backbone:0.2.5/',
        '/eCash Satoshi:31.1.0/',
        '/eCash Satoshi:1.2.0(Bitcoin Core 32.1.0; ecash-ecash; BitWindow-0.2)/',
        '/eCash Satoshi:31.1.0(ecash-ecash; BitWindow-0.2)/',
        '/eCash Satoshi:1.2.0(Bitcoin Core 32.1.0; SuperMegaUltraHyperTheftcoinDumper5Million)/',
        '/eCash Backbone:0.2.4/',
        '/eCash Backbone:0.2.5/',
      ]
        .flatMap((subver, i) => [subver, subver, subver].map((s, j) => ({ s, n: i * 3 + j + 1 })))
        .map(({ s: subver, n: id }) => ({ id, addr: id % 5 === 0 ? `[2a01:4f8:1c1c:87e::${id}]:8533` : `203.0.113.${id * 7}:8533`, inbound: id % 3 === 0, pingtime: 0.02 * id, synced_blocks: height, network: id % 5 === 0 ? 'ipv6' : 'ipv4', connection_type: id % 3 === 0 ? 'inbound' : id % 4 === 0 ? 'manual' : 'outbound-full-relay', subver, servicesnames: id % 4 === 1 ? ['NETWORK_LIMITED', 'WITNESS', 'P2P_V2'] : ['NETWORK', 'WITNESS', 'NETWORK_LIMITED', 'P2P_V2'], version: 70016, startingheight: 1, synced_headers: height, bytessent: 1000 * id, bytesrecv: 2000 * id, conntime: start - 60 * id, lastsend: start, lastrecv: start })),
    getmininginfo: () => ({ blocks: height, difficulty: 4.6565e-10, networkhashps: 12.4, pooledtx: 3 }),
    getnetworkhashps: (p) => 12.4 * (1 + 0.2 * Math.sin((Number(p[1]) < 0 ? height : Number(p[1])) / 50)),
    getdrivechaininfo: () => ({ activesidechains: 2, proposals: 1, pendingbundles: 1, escrowtotal: 7.25, withdrawalperiod: 100 }),
    listactivesidechains: () => [
      { slot: 2, title: 'Thunder', description: 'Fast payments', escrow: { txid: 'aa', n: 0, amount: 5.25 }, pendingbundles: 1 },
      { slot: 5, title: 'BitNames', description: 'Names and their records', escrow: { txid: 'bb', n: 0, amount: 2 }, pendingbundles: 0 },
    ],
    listwithdrawalbundles: () => [{ slot: 2, hash: 'cd'.repeat(32), score: 14, blocksleft: 86, payable: false }],
    getsidechainevents: (p) => Array.from({ length: Number(p[2]) }, (_, i) => ({ height: Number(p[1]) + i, bmm: (i * (Number(p[0]) + 3)) % 4 !== 0 ? 'ab' : undefined })),
  };

  setTransport(async (request: BridgeRequest) => {
    if (request.kind === 'cancel') return true;
    const params = request.params ?? [];
    if (request.kind === 'host') {
      const fn = host[request.method ?? ''];
      if (!fn) throw { code: -32601, message: `No host method ${request.method}` };
      return fn(params as Record<string, unknown>);
    }
    const fn = rpc[request.method ?? ''];
    if (!fn) throw { code: -32601, message: 'Method not found' };
    return fn(params as unknown[]);
  });

  // A block now and then.
  setInterval(() => {
    height++;
    dispatch([{ type: 'tip', data: { height, time: Math.floor(Date.now() / 1000), hash: hash(height), progress: 1, state: 'synced', ibd: false, loadingBlocks: false } }]);
  }, 15000);
}
