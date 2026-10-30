// The pages of the sidechains the app runs, in frames (doc/unified-app.md §13): each sidechain's own pages (a pack
// built into the app, /packs/<stem>/), whose requests go through the app's bridge to the host, which sends them to the
// node of that sidechain or answers them itself (webui/sidechainpages.h), and whose events come back from it.
import { on, request, sendRaw } from './bridge';
import { onModuleMessage, setTeller } from './modules.svelte';

export interface PackEntry {
  stem: string;
  name: string;
  hostApi: number;
  commit: string;
  brand?: { color?: string; mark?: string[] };
}

export interface PackInfo {
  stem: string;
  pack: PackEntry | null;
  running: boolean;
  compatible: boolean;
  /** Why not: 'nopack' (not built in), 'pack' (built for another host), 'daemon' (its node is older or newer), 'starting'. */
  reason: string;
  hostApi?: number;
  version?: string;
}

interface PackRequest {
  rid?: string;
  kind: 'rpc' | 'host' | 'cancel' | 'testrpc';
  method?: string;
  params?: unknown;
  wallet?: string;
  exact?: boolean;
}

export const packs = $state({ list: [] as PackEntry[], loaded: false });

export const packHost = {
  list: () => request<PackEntry[]>('host', 'pack.list', {}),
  info: (stem: string) => request<PackInfo>('host', 'pack.info', { stem }),
};

export async function loadPacks(): Promise<void> {
  if (packs.loaded) return;
  try {
    packs.list = await packHost.list();
    packs.loaded = true;
  } catch {
    // Asked again next time.
  }
}

/** Whether a sidechain's pages are built into the app. */
export function hasPack(stem: string): boolean {
  return packs.list.some((p) => p.stem === stem);
}

/** The request of a frame, as the app's bridge sends it: its own request id kept apart from the app's own. */
export function wrap(stem: string, req: PackRequest): Record<string, unknown> {
  const rid = req.rid != null ? `pack:${stem}:${req.rid}` : undefined;
  if (req.kind === 'cancel') return { kind: 'cancel', rid };
  const inner: Record<string, unknown> = { kind: req.kind, method: req.method, params: req.params ?? (req.kind === 'rpc' ? [] : {}) };
  if (req.wallet != null) inner.wallet = req.wallet;
  const body: Record<string, unknown> = { kind: 'host', method: 'pack.call', params: { stem, request: inner } };
  if (rid) body.rid = rid;
  if (req.exact) body.exact = true;
  return body;
}

/** The origin a sidechain's pages are served from (assets.h PACK_SCHEME): an origin of each, never the app's. */
export function packOrigin(stem: string): string {
  return `chains-pack://${stem}`;
}

/** The address of the pages of a sidechain, under their own origin. */
export function packUrl(stem: string): string {
  return `${packOrigin(stem)}/packs/${stem}/index.html`;
}

/** What a frame shows, as the page registered it: its sidechain is known from the frame, never from its messages. */
interface Frame {
  stem: string;
  win: Window;
}
const frames = new Map<string, Frame>();

/**
 * The events of a sidechain's node its frame needs only the newest of (its tip, its headers, its mempool, its traffic,
 * its peers): a chain catching up or a busy one sends many, and each is work for every part of its pages that shows the
 * chain (doc/unified-app.md §21.3). The newest of each is handed at most once a FRAME_STATE_MS while the frame is shown,
 * and once when it is shown again while it waits hidden; every other event (its wallet's transactions, its dialogs) is
 * handed at once.
 */
export const LATEST_ONLY = new Set(['tip', 'headerTip', 'mempool', 'traffic', 'connections', 'networkActive', 'progress', 'alert', 'bans']);
export const FRAME_STATE_MS = 1000;
interface Held {
  hidden: boolean;
  latest: Map<string, unknown>;
  timer: ReturnType<typeof setTimeout> | undefined;
  sentAt: number;
}
const held = new Map<string, Held>();
function heldOf(stem: string): Held {
  let h = held.get(stem);
  if (!h) held.set(stem, (h = { hidden: false, latest: new Map(), timer: undefined, sentAt: 0 }));
  return h;
}
function flushLatest(stem: string): void {
  const h = heldOf(stem);
  if (h.timer !== undefined) clearTimeout(h.timer);
  h.timer = undefined;
  if (h.latest.size === 0) return;
  const frame = frames.get(stem);
  const events = [...h.latest.values()];
  h.latest.clear();
  h.sentAt = Date.now();
  if (frame) post(frame, { chainsPackEvents: 1, batch: JSON.stringify(events) });
}
function scheduleLatest(stem: string): void {
  const h = heldOf(stem);
  if (h.hidden || h.timer !== undefined) return;
  const wait = h.sentAt + FRAME_STATE_MS - Date.now();
  if (wait <= 0) flushLatest(stem);
  else h.timer = setTimeout(() => flushLatest(stem), wait);
}

/**
 * A frame hidden or shown again by the app (ModuleFrame): its pages rest as in a window not seen (the shim's
 * document.hidden), and the newest state its node sent meanwhile is handed to it when it is shown.
 */
export function setFrameHidden(stem: string, hidden: boolean): void {
  const h = heldOf(stem);
  if (h.hidden === hidden) return;
  h.hidden = hidden;
  tellFrame(stem, 'visibility', { hidden });
  if (!hidden) flushLatest(stem);
}

/** Requests of each frame so far (the tests wait for its pages to start). */
export const traffic = $state<Record<string, number>>({});

export function registerFrame(stem: string, win: Window | null): void {
  if (win) frames.set(stem, { stem, win });
  else {
    frames.delete(stem);
    const h = held.get(stem);
    if (h?.timer !== undefined) clearTimeout(h.timer);
    held.delete(stem);
  }
}

function post(frame: Frame, message: unknown): void {
  try {
    frame.win.postMessage(message, packOrigin(frame.stem));
  } catch {
    // The frame went (another sidechain shown): nothing to tell.
  }
}

/** Hand a frame a batch of events (JSON text of [{type, data}]), as the host hands the app its own (LATEST_ONLY aside). */
export function deliver(stem: string, batch: string): boolean {
  const frame = frames.get(stem);
  if (!frame) return false;
  let events: unknown;
  try {
    events = JSON.parse(batch);
  } catch {
    events = null;
  }
  if (!Array.isArray(events)) {
    post(frame, { chainsPackEvents: 1, batch });
    return true;
  }
  const now: unknown[] = [];
  let kept = false;
  for (const e of events) {
    const type = e && typeof e === 'object' ? (e as { type?: unknown }).type : undefined;
    if (typeof type === 'string' && LATEST_ONLY.has(type)) {
      const h = heldOf(stem);
      h.latest.delete(type);
      h.latest.set(type, e);
      kept = true;
    } else {
      now.push(e);
    }
  }
  if (now.length) post(frame, { chainsPackEvents: 1, batch: now.length === events.length ? batch : JSON.stringify(now) });
  if (kept) scheduleLatest(stem);
  return true;
}

/** Tell the pages of a module something of the app (its look, the page to show, the keyboard). */
export function tellFrame(stem: string, type: string, data: unknown): boolean {
  const frame = frames.get(stem);
  if (!frame) return false;
  post(frame, { chainsModuleHost: 1, type, data });
  return true;
}
setTeller(tellFrame);

/** Tell a frame its node is ready (or not): its pages leave their splash, or go back to it. */
export function tellPhase(stem: string, phase: 'ready' | 'init'): boolean {
  return deliver(stem, JSON.stringify([{ type: 'phase', data: { phase, data: {} } }]));
}

/** A request of the pages of a sidechain, sent to the host for that sidechain. */
export function route(stem: string, req: PackRequest): Promise<unknown> {
  if (typeof stem !== 'string' || !/^[a-z0-9]+$/.test(stem)) return Promise.reject({ code: -32600, message: 'No such sidechain' });
  if (!req || typeof req !== 'object' || (req.kind !== 'rpc' && req.kind !== 'host' && req.kind !== 'cancel')) {
    return Promise.reject({ code: -32600, message: 'Not a request the pages of a sidechain make here' });
  }
  traffic[stem] = (traffic[stem] ?? 0) + 1;
  return sendRaw(wrap(stem, req) as never);
}

/**
 * A message of a frame: taken only from a frame the page registered, for the sidechain of that frame (whatever the
 * message says), answered to that frame alone. Anything else is dropped. Returns whether it was taken.
 */
export function onFrameMessage(event: { source: unknown; data: unknown }): boolean {
  const frame = [...frames.values()].find((f) => f.win === event.source);
  if (!frame) return false;
  const data = event.data as { chainsPack?: unknown; chainsModule?: unknown; type?: unknown; data?: unknown; id?: unknown; request?: unknown } | null;
  // What the pages of a module tell the app (lib/modules.svelte.ts): for the module of that frame, checked there.
  if (data && typeof data === 'object' && data.chainsModule === 1) return onModuleMessage(frame.stem, data.type, data.data);
  if (!data || typeof data !== 'object' || data.chainsPack !== 1 || typeof data.id !== 'number') return false;
  const id = data.id;
  route(frame.stem, data.request as PackRequest).then(
    (value) => post(frame, { chainsPackReply: 1, id, ok: true, value }),
    (e: unknown) => {
      const err = e && typeof e === 'object' ? (e as { code?: unknown; message?: unknown }) : {};
      post(frame, { chainsPackReply: 1, id, ok: false, error: { code: typeof err.code === 'number' ? err.code : -1, message: typeof err.message === 'string' ? err.message : String(e) } });
    },
  );
  return true;
}

let installed = false;
/** Take the frames' messages and route the node's events to them (once, for the life of the page). */
export function installPackRoute(): void {
  if (installed || typeof window === 'undefined') return;
  installed = true;
  window.addEventListener('message', (event) => onFrameMessage(event));
  on<{ stem: string; batch: string; reset: boolean }>('pack.events', ({ stem, batch, reset }) => {
    // Events were missed (its node started again): the pages ask for the state anew, as when their node gets ready.
    if (reset) tellPhase(stem, 'ready');
    else if (batch) deliver(stem, batch);
  });
}
