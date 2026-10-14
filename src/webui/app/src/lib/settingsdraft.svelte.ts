// The changes made on the Settings page wait for Apply, or go on Cancel (the OK and Cancel of the
// options dialog of chains-qt): one stray click never takes effect.
//
// The options of the node are kept by the host: each change is written at once and what it replaced is
// kept (options.apply forgets that, options.cancel puts it back, and so does the next start when they were
// never applied). The preferences of the app (the look, the window, the wallet pages) are kept here: shown
// at once while they wait, and back as they were on Cancel.
import { confirm, errorText, inform } from './dialogs.svelte';
import { host, type NodeOptions } from './rpc';
import { prefs, savePrefs } from './state.svelte';

export type Prefs = Omit<typeof prefs, 'loaded'>;

export const draft = $state({
  /** Preferences of the app waiting to be applied (already shown). */
  prefs: {} as Partial<Prefs>,
  /** What those preferences were before, to come back to on Cancel. */
  original: {} as Partial<Prefs>,
  /** The options of the node as they were when the changes began (what the risky ones are measured against). */
  baseline: null as NodeOptions | null,
});

/** The number of changes waiting: the options the host keeps, and the preferences kept here. */
export function pendingCount(options: Pick<NodeOptions, 'pending'> | null): number {
  return (options?.pending ?? 0) + Object.keys(draft.prefs).length;
}

/** The options of the node before the first change: kept once, until applied or cancelled. */
export function noteBaseline(options: NodeOptions | null): void {
  if (options && (!draft.baseline || options.pending === 0)) draft.baseline = options;
}

/** A preference of the app changed on the page: shown at once, saved when applied. */
export function stagePrefs(changes: Partial<Prefs>): void {
  for (const [key, value] of Object.entries(changes) as [keyof Prefs, unknown][]) {
    if (!(key in draft.original)) (draft.original as Record<string, unknown>)[key] = prefs[key];
    if (sameValue(draft.original[key], value)) {
      delete draft.prefs[key];
      delete draft.original[key];
    } else {
      (draft.prefs as Record<string, unknown>)[key] = value;
    }
  }
  Object.assign(prefs, changes);
}

function sameValue(a: unknown, b: unknown): boolean {
  return a === b || JSON.stringify(a) === JSON.stringify(b);
}

function clearPrefs(): void {
  for (const k of Object.keys(draft.prefs)) delete draft.prefs[k as keyof Prefs];
  for (const k of Object.keys(draft.original)) delete draft.original[k as keyof Prefs];
}

/** The changes that are hard to take back, with what they do (asked before they are applied). */
export function riskyChanges(before: NodeOptions | null, now: NodeOptions | null): string[] {
  if (!before || !now) return [];
  const out: string[] = [];
  if (now.prune && !before.prune) {
    out.push(`Prune block storage to ${now.pruneSize} GB: at the next start the node deletes the oldest blocks. Reverting this setting requires re-downloading the entire blockchain.`);
  } else if (now.prune && before.prune && now.pruneSize < before.pruneSize) {
    out.push(`Prune block storage to ${now.pruneSize} GB instead of ${before.pruneSize} GB: more of the oldest blocks are deleted at the next start, and only downloading them again brings them back.`);
  }
  if (now.dbcache > Math.max(4096, before.dbcache)) out.push(`A database cache of ${now.dbcache} MiB: the node may use that much more memory.`);
  const proxy = (o: NodeOptions) => `${o.proxyUse}:${o.proxyIp}:${o.proxyPort}:${o.torUse}:${o.torIp}:${o.torPort}`;
  if (proxy(now) !== proxy(before)) out.push('The proxy settings: every connection of the node to its peers goes out another way. A wrong proxy leaves the node without peers.');
  if (!now.server && before.server) out.push('The RPC server off: command-line tools, and the sidechain nodes run from here, can no longer reach this node.');
  if (!now.listen && before.listen) out.push('Incoming connections off: other nodes can no longer connect to this one.');
  return out;
}

/**
 * Apply the changes, once the risky ones are confirmed. Resolves with the options as the host has them
 * after, or null when the user did not confirm (the changes still wait then).
 */
export async function applyChanges(now: NodeOptions | null): Promise<NodeOptions | null> {
  const risky = riskyChanges(draft.baseline, now);
  if (risky.length > 0) {
    const ok = await confirm('Confirm options', 'Some of these changes are hard to take back. Apply them?', {
      yes: 'Apply',
      level: 'warning',
      informative: risky.map((r) => `• ${r}`).join('\n'),
    });
    if (!ok) return null;
  }
  let options = now;
  try {
    if ((now?.pending ?? 0) > 0) options = await host.applyOptions();
  } catch (e) {
    await inform('Settings', errorText(e), 'error');
    return null;
  }
  if (Object.keys(draft.prefs).length > 0) savePrefs({ ...draft.prefs });
  clearPrefs();
  draft.baseline = options;
  return options;
}

/** Throw the changes away: the options of the node and the look come back as they were. */
export async function cancelChanges(now: NodeOptions | null): Promise<NodeOptions | null> {
  Object.assign(prefs, draft.original);
  clearPrefs();
  let options = now;
  try {
    if ((now?.pending ?? 0) > 0) options = await host.cancelOptions();
  } catch (e) {
    await inform('Settings', errorText(e), 'error');
  }
  draft.baseline = options;
  return options;
}

/** The user leaves the Settings page with changes waiting: apply them, or throw them away. */
export async function askOnLeave(now: NodeOptions | null): Promise<void> {
  if (pendingCount(now) === 0) return;
  const apply = await confirm('Settings', 'Some changes of the settings are not applied yet.', {
    yes: 'Apply',
    no: 'Discard',
    level: 'question',
    informative: 'Apply them now, or discard them? Changes that are not applied are discarded when the app stops.',
  });
  if (apply && (await applyChanges(now)) !== null) return;
  await cancelChanges(now);
}
