// The colour of each chain wherever a chain is shown by colour (its mark, its line in a chart): one per chain, always
// the same whatever is shown with it (a filter never repaints the others): the slot of the categorical palette of the
// chain's place in the catalog (styles/base.css --chain-<stem>), so that no two chains look alike (the packs' own
// colours do: Layers and Thunder are both violet); this app's own chain wears the accent of the theme.

export const HOST_ID = 'chains';

/** The catalog's chains in its order: each takes the slot of its place (webui/sidechains.json). */
export const CHAIN_SLOTS = ['layers', 'fastblocks', 'thunder', 'bitnames', 'bitassets', 'zside', 'hivemind'] as const;

/** The CSS colour of a chain: a value for `fill`, `stroke`, `background`. */
export function chainColor(id: string): string {
  if (id === HOST_ID) return 'var(--accent)';
  const slot = CHAIN_SLOTS.indexOf(id as (typeof CHAIN_SLOTS)[number]);
  return slot >= 0 ? `var(--chain-${CHAIN_SLOTS[slot]})` : 'var(--text-muted)';
}
