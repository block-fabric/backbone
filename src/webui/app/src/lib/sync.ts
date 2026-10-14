// What the status bar says about the sync of the node (BitcoinGUI::setNumBlocks).
import { niceTimeOffset } from './format';

/** A tip older than this is "behind" (MAX_BLOCK_TIME_GAP of chains-qt). */
export const MAX_BLOCK_TIME_GAP = 90 * 60;
/** Headers further behind than this many blocks are being synced (HEADER_HEIGHT_DELTA_SYNC). */
export const HEADER_HEIGHT_DELTA_SYNC = 24;

export interface SyncInput {
  tipHeight: number;
  tipTime: number;
  progress: number;
  loadingBlocks: boolean;
  headerHeight: number;
  headerTime: number;
  presyncHeight?: number;
  presyncTime?: number;
  connections: number;
  networkActive: boolean;
  /** Seconds between blocks on this network. */
  spacing: number;
  /** Now, in seconds. */
  now: number;
}

export interface SyncStatus {
  upToDate: boolean;
  /** What the node does: "Synchronizing with network…". Empty when up to date. */
  label: string;
  /** "3 hours behind", when behind. */
  behind: string;
  /** Verification progress, 0 to 1. */
  progress: number;
  /** Longer text for a tooltip. */
  detail: string;
}

function percent(height: number, left: number): string {
  return ((100 * height) / (height + left)).toFixed(1);
}

export function syncStatus(s: SyncInput): SyncStatus {
  const secs = s.now - s.tipTime;
  const processed = `Processed ${s.tipHeight} block${s.tipHeight === 1 ? '' : 's'} of transaction history.`;
  if (secs < MAX_BLOCK_TIME_GAP) {
    return { upToDate: true, label: '', behind: '', progress: 1, detail: `Up to date. ${processed}` };
  }
  let label: string;
  if (s.loadingBlocks) {
    label = 'Processing blocks on disk…';
  } else if (s.connections > 0 && s.networkActive) {
    label = 'Synchronizing with network…';
    const headersLeft = Math.floor((s.now - s.headerTime) / s.spacing);
    if (s.presyncHeight !== undefined && s.presyncTime !== undefined && s.presyncHeight > s.headerHeight) {
      const left = Math.floor((s.now - s.presyncTime) / s.spacing);
      if (left > HEADER_HEIGHT_DELTA_SYNC) label = `Pre-syncing Headers (${percent(s.presyncHeight, left)}%)…`;
    } else if (headersLeft > HEADER_HEIGHT_DELTA_SYNC) {
      label = `Syncing Headers (${percent(s.headerHeight, headersLeft)}%)…`;
    }
  } else if (!s.networkActive) {
    label = 'Network activity disabled';
  } else {
    label = 'Connecting to peers…';
  }
  const behind = `${niceTimeOffset(secs)} behind`;
  return {
    upToDate: false,
    label,
    behind,
    progress: Math.max(0, Math.min(1, s.progress)),
    detail: `Catching up… ${processed} Last received block was generated ${niceTimeOffset(secs)} ago. Transactions after this will not yet be visible.`,
  };
}
