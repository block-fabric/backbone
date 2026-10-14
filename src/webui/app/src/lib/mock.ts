// A pretend host, for working on the interface in a browser (npm run dev) and for the tests.
// Never part of the build of chains-app (only loaded when import.meta.env.DEV is set).
import { dispatch, setTransport, type BridgeRequest } from './bridge';

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
      info: { name: 'Chains', version: 'v32.0.0-dev (preview)', bugreport: '', network: 'regtest', networkLabel: 'regtest', datadir: '/tmp/preview', walletEnabled: true, devtools: true, test: false, platform: 'browser', blockSpacing: 600 },
    }),
    'app.help': () => ({ usage: 'Usage: chains-app [options] [URI]', options: 'Options:\n\n  -?\n       Print this help message and exit' }),
    'app.about': () => ({ name: 'Chains', version: 'preview', license: 'Distributed under the MIT software license.' }),
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
    'clipboard.write': () => true,
    'clipboard.read': () => '',
    'intro.space': () => ({ exists: true, isDirectory: true, free: 512e9 }),
  };

  const rpc: Record<string, (params: unknown[]) => unknown> = {
    getbalances: () => ({ mine: { trusted: 12.5, untrusted_pending: 0.25, immature: 50 } }),
    getblockheader: (p) => {
      const h = parseInt(String(p[0]), 16);
      return { height: h, time: start - (height - h) * 600, nTx: (h * 7) % 13 + 1, previousblockhash: h > 0 ? hash(h - 1) : undefined };
    },
    getblockchaininfo: () => ({ chain: 'regtest', blocks: height, headers: height, time: start, size_on_disk: 3_200_000, pruned: false }),
    getnetworkinfo: () => ({ subversion: '/Chains:32.0.0/', connections: 8, connections_in: 2, connections_out: 6, localaddresses: [] }),
    getmempoolinfo: () => ({ size: 3, usage: 4096 }),
    uptime: () => 3600,
    validateaddress: (p) => ({ isvalid: String(p[0]).startsWith('rchn1') }),
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
