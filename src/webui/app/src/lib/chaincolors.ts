// The colour of each chain wherever a chain is shown by colour (its mark, its line in a chart): one per chain, always
// the same whatever is shown with it (a filter never repaints the others). Its pack's own colour if it has one; else
// a slot of the categorical palette, fixed by the chain's place in the catalog (styles/base.css --chain-<stem>);
// this app's own chain wears the accent of the theme.
import { packs } from './packs.svelte';

export const HOST_ID = 'chains';

/** The catalog's chains in its order: each takes the slot of its place (webui/sidechains.json). */
export const CHAIN_SLOTS = ['layers', 'fastblocks', 'thunder', 'bitnames', 'bitassets', 'zside', 'hivemind'] as const;

const HEX = /^#[0-9a-fA-F]{3,8}$/;

/** The CSS colour of a chain: a value for `fill`, `stroke`, `background`. */
export function chainColor(id: string): string {
  if (id === HOST_ID) return 'var(--accent)';
  const brand = packs.list.find((p) => p.stem === id)?.brand?.color ?? '';
  if (HEX.test(brand)) return brand;
  const slot = CHAIN_SLOTS.indexOf(id as (typeof CHAIN_SLOTS)[number]);
  return slot >= 0 ? `var(--chain-${CHAIN_SLOTS[slot]})` : 'var(--text-muted)';
}
