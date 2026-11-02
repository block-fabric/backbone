// Peers and bans of the node window (PeerTableModel, BanTableModel and RPCConsole::updateDetailWidget
// of chains-qt), from getpeerinfo and listbanned.
import { formatBytes, shortDuration } from './format';
import { t } from './i18n.svelte';

export interface PeerInfo {
  id: number;
  addr: string;
  addrlocal?: string;
  network: string;
  mapped_as?: number;
  services: string;
  servicesnames?: string[];
  relaytxes: boolean;
  lastsend: number;
  lastrecv: number;
  last_transaction: number;
  last_block: number;
  bytessent: number;
  bytesrecv: number;
  conntime: number;
  timeoffset: number;
  pingtime?: number;
  minping?: number;
  pingwait?: number;
  version: number;
  subver: string;
  inbound: boolean;
  bip152_hb_to: boolean;
  bip152_hb_from: boolean;
  startingheight?: number;
  presynced_headers?: number;
  synced_headers: number;
  synced_blocks: number;
  connection_type: string;
  transport_protocol_type?: string;
  session_id?: string;
  permissions?: string[];
  addr_relay_enabled?: boolean;
  addr_processed?: number;
  addr_rate_limited?: number;
}

export interface BanEntry {
  address: string;
  ban_created: number;
  banned_until: number;
  ban_duration: number;
  time_remaining: number;
}

const NETWORKS: Record<string, string> = { ipv4: 'IPv4', ipv6: 'IPv6', onion: 'Onion', i2p: 'I2P', cjdns: 'CJDNS', not_publicly_routable: 'Unroutable', internal: 'Internal' };

/** The network of a peer as chains-qt names it (GUIUtil::NetworkToQString). */
export function networkName(network: string): string {
  return NETWORKS[network] ?? network;
}

const TYPES: Record<string, string> = {
  'outbound-full-relay': 'Full Relay',
  'block-relay-only': 'Block Relay',
  manual: 'Manual',
  feeler: 'Feeler',
  'addr-fetch': 'Address Fetch',
  inbound: '',
};

/** The type of a connection (GUIUtil::ConnectionTypeToQString), with its direction when asked. */
export function connectionType(type: string, inbound: boolean, withDirection = false): string {
  const name = TYPES[type] ?? type;
  if (!withDirection) return name || 'Inbound';
  const direction = inbound ? 'Inbound' : 'Outbound';
  return name ? `${direction} ${name}` : direction;
}

/** The node software a peer runs, from its user agent: "/eCash Backbone:0.2.5/", "/eCash Satoshi:1.2.0(Bitcoin Core 32.1.0)/". */
export interface PeerClient {
  /** Data of the peer (shown with SafeText): "Backbone 0.2.5", "eCash 1.2.0", or the user agent's first part. */
  name: string;
  backbone: boolean;
  /** The Bitcoin Core release it builds on (in brackets, or its own version when it has none): what "newer" compares. */
  core: number[];
}

function versionParts(v: string): number[] {
  return v.split('.').map((n) => Number.parseInt(n, 10) || 0);
}

export function peerClient(subver: string): PeerClient {
  const m = /^\/?([^:/()]+):([0-9][0-9.]*)(?:\(([^)]*)\))?/.exec(subver ?? '');
  if (!m) return { name: subver || '?', backbone: false, core: [] };
  const product = m[1].trim();
  const backbone = /\bBackbone\b/.test(product);
  const core = /Bitcoin Core ([0-9][0-9.]*)/.exec(m[3] ?? '')?.[1];
  // eCash's client calls itself "eCash Satoshi": eCash, with its own version.
  const name = backbone ? 'Backbone' : product.replace(/^eCash Satoshi$/, 'eCash');
  return { name: `${name} ${m[2]}`, backbone, core: versionParts(core ?? m[2]) };
}

/**
 * The order of the Node column: Backbone first (the newest client of the network), then the other
 * clients from the newest Bitcoin Core they build on, the unknown last.
 */
export function clientOrder(subver: string): number {
  const c = peerClient(subver);
  if (!c.core.length) return 3e9;
  const [a = 0, b = 0, d = 0] = c.core;
  const newer = Math.min(a, 999) * 1e6 + Math.min(b, 999) * 1e3 + Math.min(d, 999);
  return (c.backbone ? 1e9 : 2e9) - newer;
}

/**
 * Whether a peer keeps the whole chain: "Full" (it serves every block, NODE_NETWORK), "Pruned" (only the
 * last 288 blocks, NODE_NETWORK_LIMITED alone), or "No blocks" (it serves none).
 */
export function blocksKindText(p: Pick<PeerInfo, 'servicesnames'>): string {
  const services = p.servicesnames ?? [];
  if (services.includes('NETWORK')) return t('Full');
  if (services.includes('NETWORK_LIMITED')) return t('Pruned');
  return t('No blocks');
}

/** A ping time (GUIUtil::formatPingTime): "N/A" when there is none. */
export function pingText(seconds: number | undefined): string {
  if (seconds === undefined || seconds <= 0 || !Number.isFinite(seconds)) return t('N/A');
  return t('%1 ms', Math.round(seconds * 1000));
}

/** How long ago something happened, "Never" if it did not (RPCConsole TimeDurationField). */
export function sinceText(now: number, time: number): string {
  return time ? shortDuration(now - time) : 'Never';
}

export interface DetailRow {
  label: string;
  value: string;
}

/** The detail panel of a peer, in the order of chains-qt. */
export function peerDetails(p: PeerInfo, now: number): DetailRow[] {
  const hb = [p.bip152_hb_to ? 'To' : '', p.bip152_hb_from ? 'From' : ''].filter(Boolean).join('/') || 'No';
  const rows: DetailRow[] = [
    { label: 'Whitelisted', value: p.permissions && p.permissions.length ? p.permissions.join(' & ') : 'N/A' },
    { label: 'Direction/Type', value: connectionType(p.connection_type, p.inbound, true) },
    { label: 'Transport', value: p.transport_protocol_type ?? 'N/A' },
  ];
  if (p.transport_protocol_type === 'v2' && p.session_id) rows.push({ label: 'Session ID', value: p.session_id });
  rows.push(
    { label: 'Network', value: networkName(p.network) },
    { label: 'Version', value: p.version ? String(p.version) : 'N/A' },
    { label: 'User Agent', value: p.subver || 'N/A' },
    { label: 'Services', value: p.servicesnames && p.servicesnames.length ? p.servicesnames.map((s) => s.charAt(0) + s.slice(1).toLowerCase().replace(/_/g, ' ')).join(' & ') : 'None' },
    { label: 'Chain', value: blocksKindText(p) },
    { label: 'Transaction Relay', value: p.relaytxes ? 'Yes' : 'No' },
    { label: 'High Bandwidth', value: hb },
    { label: 'Starting Block', value: p.startingheight !== undefined && p.startingheight >= 0 ? String(p.startingheight) : 'Unknown' },
    { label: 'Synced Headers', value: p.synced_headers > -1 ? String(p.synced_headers) : 'Unknown' },
    { label: 'Synced Blocks', value: p.synced_blocks > -1 ? String(p.synced_blocks) : 'Unknown' },
    { label: 'Address Relay', value: p.addr_relay_enabled ? 'Yes' : 'No' },
    { label: 'Addresses Processed', value: String(p.addr_processed ?? 0) },
    { label: 'Addresses Rate-Limited', value: String(p.addr_rate_limited ?? 0) },
    { label: 'Mapped AS', value: p.mapped_as ? String(p.mapped_as) : 'N/A' },
    { label: 'Connection Time', value: shortDuration(now - p.conntime) },
    { label: 'Last Block', value: sinceText(now, p.last_block) },
    { label: 'Last Transaction', value: sinceText(now, p.last_transaction) },
    { label: 'Last Send', value: sinceText(now, p.lastsend) },
    { label: 'Last Receive', value: sinceText(now, p.lastrecv) },
    { label: 'Sent', value: formatBytes(p.bytessent) },
    { label: 'Received', value: formatBytes(p.bytesrecv) },
    { label: 'Ping Time', value: pingText(p.pingtime) },
    { label: 'Min Ping', value: pingText(p.minping) },
    { label: 'Ping Wait', value: pingText(p.pingwait) },
    { label: 'Time Offset', value: `${p.timeoffset} seconds` },
  );
  return rows;
}

/** Ban times of the context menu of a peer (RPCConsole: 1 hour, 1 day, 1 week, 1 year). */
export const BAN_TIMES = [
  { label: '1 hour', seconds: 60 * 60 },
  { label: '1 day', seconds: 60 * 60 * 24 },
  { label: '1 week', seconds: 60 * 60 * 24 * 7 },
  { label: '1 year', seconds: 60 * 60 * 24 * 365 },
] as const;
