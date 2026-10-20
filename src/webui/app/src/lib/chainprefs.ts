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
