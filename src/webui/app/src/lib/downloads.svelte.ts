// The programs of the modules (doc/unified-app.md §15): downloaded from the releases of their repositories (a fixed
// list in the app, https only), checked (the SHA-256 of the release's sums and of its manifest, a signature when the
// release is signed), unpacked to <datadir>/sidechains/bin/<stem>/<version>/, the version before kept to go back to.
// The host does all of it (webui/moduleinstaller.cpp, modules.*); here the words and the state of the pages.
import { on, request } from './bridge';

export interface ReleaseInfo {
  tag: string;
  name: string;
  prerelease: boolean;
  published: string;
  asset: { name: string; size: number } | null;
  sums: boolean;
  manifest: boolean;
  signed: boolean;
  compatible: boolean;
  /** No release key is built into this app yet: checked by its checksums alone (§15.4). */
  unsigned?: boolean;
  /** Why it cannot be used ('' when it can). */
  reason: string;
}

export interface ReleaseList {
  repo: string;
  platform: string;
  hostApi: number;
  signatures: 'auto' | 'require';
  current: string | null;
  previous: string | null;
  source: 'download' | 'bundled' | 'folder' | 'none';
  installed: string[];
  releases: ReleaseInfo[];
  newestCompatible: string | null;
  updateAvailable: boolean;
}

export type InstallState = 'idle' | 'listing' | 'downloading' | 'verifying' | 'unpacking' | 'installing' | 'done' | 'failed' | 'cancelled';

export interface InstallProgress {
  state: InstallState;
  tag: string;
  done: number;
  total: number;
  error: string;
}

export interface UpdateInfo {
  stem: string;
  current: string | null;
  tag: string | null;
}

export const moduleHost = {
  releases: (stem: string) => request<ReleaseList>('host', 'modules.releases', { stem }),
  install: (stem: string, tag: string, allowOlder = false) => request<boolean>('host', 'modules.install', { stem, tag, ...(allowOlder ? { allowOlder } : {}) }),
  progress: (stem: string) => request<InstallProgress>('host', 'modules.progress', { stem }),
  cancel: (stem: string) => request<boolean>('host', 'modules.cancel', { stem }),
  update: (stem: string, tag: string, allowOlder = false) => request<boolean>('host', 'modules.update', { stem, tag, ...(allowOlder ? { allowOlder } : {}) }),
  rollback: (stem: string) => request<boolean>('host', 'modules.rollback', { stem }),
  checkUpdates: (force = false) => request<{ updates: { stem: string; name: string; current: string | null; latest: string }[]; errors: { stem: string; error: string }[] }>('host', 'modules.checkUpdates', { force }),
};

export const downloads = $state({
  progress: {} as Record<string, InstallProgress>,
  releases: {} as Record<string, ReleaseList>,
  updates: {} as Record<string, string>,
});

let watching = false;
/** Follow the host's progress events (once). */
export function watchDownloads(): void {
  if (watching) return;
  watching = true;
  on<{ stem: string }>('modules', ({ stem }) => {
    if (typeof stem !== 'string') return;
    moduleHost.progress(stem).then(
      (p) => (downloads.progress[stem] = p),
      () => undefined,
    );
  });
}

/** Bytes in words: "12.4 MB". */
export function sizeText(bytes: number): string {
  if (!Number.isFinite(bytes) || bytes <= 0) return '0 MB';
  return `${(bytes / 1_000_000).toFixed(bytes >= 10_000_000 ? 0 : 1)} MB`;
}

/** The share done, 0 to 1 (unknown size: 0). */
export function fraction(p: InstallProgress | undefined): number {
  if (!p || !p.total) return 0;
  return Math.max(0, Math.min(1, p.done / p.total));
}

export function busy(p: InstallProgress | undefined): boolean {
  return !!p && ['listing', 'downloading', 'verifying', 'unpacking', 'installing'].includes(p.state);
}

const STATE_WORDS: Record<InstallState, string> = {
  idle: 'Not started',
  listing: 'Looking at the releases…',
  downloading: 'Downloading…',
  verifying: 'Checking what came…',
  unpacking: 'Unpacking…',
  installing: 'Installing…',
  done: 'Installed',
  failed: 'It did not work',
  cancelled: 'Cancelled',
};
export function stateWords(s: InstallState): string {
  return STATE_WORDS[s] ?? s;
}

/** The release the wizard proposes: the newest it can use, else none. */
export function proposedRelease(list: ReleaseList | undefined): ReleaseInfo | null {
  if (!list) return null;
  return list.releases.find((r) => r.tag === list.newestCompatible) ?? list.releases.find((r) => r.compatible) ?? null;
}

/** Where its program comes from, in words. */
export function sourceWords(list: ReleaseList | undefined): string {
  switch (list?.source) {
    case 'download':
      return 'Downloaded release %1';
    case 'bundled':
      return 'Came with this app';
    case 'folder':
      return 'From the folder you chose';
    default:
      return 'Not on this computer yet';
  }
}

let checked = false;
/**
 * Look for updates of the modules turned on, once a run (the host keeps its answer a few hours): each one found is said
 * in the bell and in a notice; the user approves it in Settings → Sidechains (stop, swap, start).
 */
export async function checkModuleUpdates(say: (stem: string, tag: string, current: string | null) => void, force = false): Promise<void> {
  if (checked && !force) return;
  checked = true;
  try {
    const found = await moduleHost.checkUpdates(force);
    const list: UpdateInfo[] = (found?.updates ?? []).map((u) => ({ stem: u.stem, current: u.current, tag: u.latest }));
    for (const u of list) {
      if (!u || typeof u.stem !== 'string' || !u.tag) continue;
      if (downloads.updates[u.stem] === u.tag) continue;
      downloads.updates[u.stem] = u.tag;
      say(u.stem, u.tag, u.current ?? null);
    }
  } catch {
    // Offline, or GitHub not reachable: asked again next run (or from Check for updates).
    checked = false;
  }
}
