// A typed client of the RPC methods of the node, and of the host methods of the app.
import { request, type CallOptions } from './bridge';

export interface BlockchainInfo {
  chain: string;
  blocks: number;
  headers: number;
  bestblockhash: string;
  difficulty: number;
  time: number;
  mediantime: number;
  verificationprogress: number;
  initialblockdownload: boolean;
  size_on_disk: number;
  pruned: boolean;
  warnings: string[] | string;
}

export interface NetworkInfo {
  version: number;
  subversion: string;
  protocolversion: number;
  connections: number;
  connections_in: number;
  connections_out: number;
  networkactive: boolean;
  localaddresses: { address: string; port: number; score: number }[];
  warnings: string[] | string;
}

export interface Balances {
  mine: { trusted: number; untrusted_pending: number; immature: number; used?: number };
}

export interface WalletInfo {
  walletname: string;
  txcount: number;
  keypoolsize: number;
  unlocked_until?: number;
  private_keys_enabled: boolean;
  descriptors: boolean;
  scanning: false | { duration: number; progress: number };
}

export interface ValidateAddress {
  isvalid: boolean;
  address?: string;
  scriptPubKey?: string;
  isscript?: boolean;
  iswitness?: boolean;
  witness_version?: number;
  error?: string;
}

export interface MempoolInfo {
  loaded: boolean;
  size: number;
  bytes: number;
  usage: number;
  total_fee: number;
}

/** The RPC methods the interface uses, with their params and results. Any other goes through rpc.raw. */
export interface RpcMethods {
  getblockchaininfo: { params: []; result: BlockchainInfo };
  getnetworkinfo: { params: []; result: NetworkInfo };
  getblockcount: { params: []; result: number };
  getblockhash: { params: [height: number]; result: string };
  getbestblockhash: { params: []; result: string };
  getmempoolinfo: { params: []; result: MempoolInfo };
  uptime: { params: []; result: number };
  listwallets: { params: []; result: string[] };
  listwalletdir: { params: []; result: { wallets: { name: string }[] } };
  getbalances: { params: []; result: Balances };
  getwalletinfo: { params: []; result: WalletInfo };
  getnewaddress: { params: [label?: string, address_type?: string]; result: string };
  validateaddress: { params: [address: string]; result: ValidateAddress };
  generatetoaddress: { params: [nblocks: number, address: string]; result: string[] };
  createwallet: { params: [name: string, disable_private_keys?: boolean, blank?: boolean, passphrase?: string]; result: { name: string; warnings?: string[] } };
  loadwallet: { params: [name: string]; result: { name: string } };
  unloadwallet: { params: [name: string]; result: { warnings?: string[] } };
  help: { params: [command?: string]; result: string };
}

export type Method = keyof RpcMethods;

export const rpc = {
  /** Call a method the client knows the types of. */
  call<M extends Method>(method: M, params: RpcMethods[M]['params'], options?: CallOptions): Promise<RpcMethods[M]['result']> {
    return request<RpcMethods[M]['result']>('rpc', method, params as unknown[], options);
  },
  /** Call any method (the console, methods of the chain the client has no types for). */
  raw<T = unknown>(method: string, params: unknown[] | Record<string, unknown> = [], options?: CallOptions): Promise<T> {
    return request<T>('rpc', method, params, options);
  },
};

// --- Host methods (src/webui/app.cpp RegisterHostMethods) ---

export type Phase = 'loading' | 'intro' | 'init' | 'ready' | 'shutdown' | 'fatal';

export interface AppInfo {
  name: string;
  version: string;
  bugreport: string;
  network: string;
  networkLabel: string;
  datadir: string;
  walletEnabled: boolean;
  devtools: boolean;
  test: boolean;
  platform: string;
  blockSpacing?: number;
}

export interface Dialog {
  id?: number;
  kind: 'message' | 'question' | 'settingsError';
  level?: 'error' | 'warning' | 'info';
  title: string;
  message: string;
  question?: string;
  details?: string[];
  style?: number;
}

export interface Hello {
  phase: Phase;
  phaseData: Record<string, unknown>;
  info: AppInfo;
  dialogs: Dialog[];
}

export interface IntroData {
  defaultDir: string;
  dir: string;
  blockchainGB: number;
  chainstateGB: number;
  pruneGB: number;
  minPruneGB: number;
  pruneForced?: boolean;
}

export interface Space {
  exists?: boolean;
  isDirectory?: boolean;
  free?: number;
  error?: string;
}

export interface WalletSummary {
  name: string;
  encrypted: boolean;
  locked: boolean;
  privateKeysDisabled: boolean;
  hd: boolean;
}

export interface NodeSnapshot {
  connections: number;
  networkActive: boolean;
  warnings: string[];
  tip: { height: number; time: number; hash: string; progress: number; state: string; ibd: boolean; loadingBlocks: boolean };
  headerTip: { height?: number; time?: number; presync?: boolean };
  wallets: WalletSummary[];
}

export const host = {
  hello: () => request<Hello>('host', 'app.hello', {}),
  info: () => request<AppInfo>('host', 'app.info', {}),
  quit: () => request<boolean>('host', 'app.quit', {}),
  help: () => request<{ usage: string; options: string }>('host', 'app.help', {}),
  about: () => request<{ name: string; version: string; license: string }>('host', 'app.about', {}),
  introSpace: (dir: string) => request<Space>('host', 'intro.space', { dir }),
  introChoose: (dir: string, pruneGB: number) => request<boolean>('host', 'intro.choose', { dir, pruneGB }),
  introCancel: () => request<boolean>('host', 'intro.cancel', {}),
  answer: (id: number, answer: unknown) => request<boolean>('host', 'dialog.answer', { id, answer }),
  prefs: () => request<Record<string, unknown>>('host', 'prefs.get', {}),
  setPrefs: (changes: Record<string, unknown>) => request<Record<string, unknown>>('host', 'prefs.set', changes),
  nodeState: () => request<NodeSnapshot>('host', 'node.state', {}),
  setNetworkActive: (active: boolean) => request<boolean>('host', 'node.setNetworkActive', { active }),
  getSetting: (name: string) => request<{ value: unknown; ignored: boolean }>('host', 'node.getSetting', { name }),
  setSetting: (name: string, value: unknown) => request<boolean>('host', 'node.setSetting', { name, value }),
  setWalletTitle: (name: string | null) => request<boolean>('host', 'window.setWallet', { name }),
  minimize: () => request<boolean>('host', 'window.minimize', {}),
  copy: (text: string) => request<boolean>('host', 'clipboard.write', { text }),
  paste: () => request<string | null>('host', 'clipboard.read', {}),
};
