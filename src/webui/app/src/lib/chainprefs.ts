// The stored preferences of the chains of the app's pages (lib/chains.svelte.ts), checked when read back.

const CHAIN_ID = /^[a-z0-9]{1,32}$/;

/** The preferences of the chains of each page (or of the wallet of each chain), checked: another version's are dropped. */
export function cleanChainViews(raw: unknown, values: RegExp = /^(all|[a-z0-9]{1,32})$/): Record<string, string> {
  const out: Record<string, string> = {};
  if (!raw || typeof raw !== 'object' || Array.isArray(raw)) return out;
  for (const [k, v] of Object.entries(raw as Record<string, unknown>).slice(0, 40)) {
    if (CHAIN_ID.test(k) && typeof v === 'string' && values.test(v)) out[k] = v;
  }
  return out;
}

/** A wallet name a chain's calls can carry (webui/chainadapter.cpp). */
export const WALLET_NAME = /^[A-Za-z0-9_-][A-Za-z0-9._-]{0,63}$/;

/** The chain selected (prefs.chain): 'all' or a chain's id; anything else is All. */
export function cleanChainId(raw: unknown): string {
  return typeof raw === 'string' && /^(all|[a-z0-9]{1,32})$/.test(raw) ? raw : 'all';
}

/** The chains' groups of the sidebar folded (prefs.chainGroupsFolded): chain ids only, at most 40. */
export function cleanChainGroups(raw: unknown): string[] {
  if (!Array.isArray(raw)) return [];
  return [...new Set(raw.filter((v): v is string => typeof v === 'string' && CHAIN_ID.test(v)))].slice(0, 40);
}
