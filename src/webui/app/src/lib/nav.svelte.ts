// Where the user is: the page shown, and the dialog of the app that is open.
import type { IconName } from '../components/Icon.svelte';

export type PageId =
  | 'overview'
  | 'send'
  | 'receive'
  | 'transactions'
  | 'sidechains'
  | 'sidechain-nodes'
  | 'mine'
  | 'explorer'
  | 'node'
  | 'settings'
  | 'addresses'
  | 'messages'
  | 'psbt'
  | 'proof'
  | 'multisig'
  | 'timestamp'
  | 'crypto'
  | 'wallets'
  | 'kit';

export interface PageInfo {
  id: PageId;
  title: string;
  icon: IconName;
  /** What the page is for, in a line (tooltips, the planned page). */
  summary: string;
  /** Items of doc/webui-parity.md the page covers. */
  parity: string[];
  /** The page needs a loaded wallet. */
  wallet?: boolean;
  /** Built (false: the page says what it will do). */
  ready?: boolean;
  /** Shortcut, as written in menus. */
  shortcut?: string;
}

export const PAGES: Record<PageId, PageInfo> = {
  overview: { id: 'overview', title: 'Overview', icon: 'overview', summary: 'Balances of the wallet and what happens on the chain', parity: ['F01', 'F02', 'F03', 'F04', 'F05'], ready: true, shortcut: 'Alt+1' },
  send: { id: 'send', title: 'Send', icon: 'send', summary: 'Send coins to one or more addresses', parity: ['G01', 'G02', 'G03', 'G04', 'G05', 'G06', 'G07', 'G08', 'B06', 'B07'], wallet: true, ready: true, shortcut: 'Alt+2' },
  receive: { id: 'receive', title: 'Receive', icon: 'receive', summary: 'Request payments, with an address, a QR code and a payment link', parity: ['H01', 'H02', 'H03'], wallet: true, ready: true, shortcut: 'Alt+3' },
  transactions: { id: 'transactions', title: 'Transactions', icon: 'history', summary: 'Every transaction of the wallet, to search, sort and export', parity: ['I01', 'I02', 'I03', 'I04', 'I05', 'I06'], wallet: true, ready: true, shortcut: 'Alt+4' },
  sidechains: { id: 'sidechains', title: 'Sidechains', icon: 'sidechains', summary: 'Deposit to sidechains, ack proposals and vote on withdrawals', parity: ['L01', 'L02', 'L03'], shortcut: 'Alt+5', ready: true },
  'sidechain-nodes': { id: 'sidechain-nodes', title: 'Sidechain nodes', icon: 'nodes', summary: 'Install, start and stop the nodes of sidechains, and watch them', parity: ['L04'], ready: true },
  mine: { id: 'mine', title: 'Mine', icon: 'mine', summary: 'Mine blocks with the processor of this computer', parity: ['L05'], ready: true },
  explorer: { id: 'explorer', title: 'Block explorer', icon: 'explorer', summary: 'Browse blocks and the transactions waiting to be mined', parity: ['L06'], ready: true },
  node: { id: 'node', title: 'Node window', icon: 'terminal', summary: 'Information, console, network traffic and peers of this node', parity: ['M01', 'M02', 'M03', 'M04', 'M05'], ready: true, shortcut: 'Ctrl+Shift+D' },
  settings: { id: 'settings', title: 'Settings', icon: 'settings', summary: 'Options of the node, the wallet, the network, the window and the look', parity: ['N01', 'N02', 'N03', 'N04', 'N05', 'N06', 'T01', 'T02', 'T03', 'T04'], ready: true },
  addresses: { id: 'addresses', title: 'Addresses', icon: 'text', summary: 'Sending and receiving addresses of the wallet', parity: ['J01', 'J02'], wallet: true, ready: true },
  messages: { id: 'messages', title: 'Sign and verify', icon: 'shield', summary: 'Sign a message with an address of the wallet, or check a signature', parity: ['K01', 'K02'], wallet: true, ready: true },
  psbt: { id: 'psbt', title: 'Partially signed transactions', icon: 'paste', summary: 'Load, sign and broadcast a PSBT from a file or the clipboard', parity: ['K03'], wallet: true, ready: true },
  proof: { id: 'proof', title: 'Proof of funds', icon: 'shield', summary: 'Prove that this wallet holds funds, or check such a proof', parity: ['L08'], wallet: true, ready: true },
  multisig: { id: 'multisig', title: 'Multisig lounge', icon: 'peers', summary: 'Share coins with partners, in addresses that need several signatures', parity: ['L09'], wallet: true, ready: true },
  timestamp: { id: 'timestamp', title: 'Timestamp a file', icon: 'block', summary: 'Prove that a file existed, by publishing its fingerprint on the chain', parity: ['L07'], ready: true },
  crypto: { id: 'crypto', title: 'Crypto tools', icon: 'qr', summary: 'Hash calculator, Merkle tree and address decoder', parity: ['L10'], ready: true },
  wallets: { id: 'wallets', title: 'Wallets', icon: 'wallet', summary: 'Create, open, close, back up, restore, migrate and encrypt wallets', parity: ['E01', 'E02', 'E03', 'E04', 'E05', 'E06', 'E07', 'E08', 'E09', 'E10'], ready: true },
  kit: { id: 'kit', title: 'Interface kit', icon: 'overview', summary: 'The shared components of the interface, for testing them', parity: [], ready: true },
};

export type DialogId = 'about' | 'help' | 'uri' | 'sync' | null;

export const nav = $state({
  page: 'overview' as PageId,
  /** The tab of a page that has tabs (node window, settings). */
  tab: '' as string,
  dialog: null as DialogId,
  /** A payment URI waiting for the send page. */
  pendingUri: null as string | null,
  /** Counts the payment URIs given (the send page takes each once; pendingUri stays as the last one). */
  uriSeq: 0,
  /** The last of them the send page took. */
  uriHandled: 0,
  /** A transaction to show selected on the transactions page (after sending, from the overview). */
  highlightTx: null as string | null,
});

export function go(page: PageId, tab = ''): void {
  nav.page = page;
  nav.tab = tab;
}
