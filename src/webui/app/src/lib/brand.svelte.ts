// The names the interface shows, never written into it:
// - the product: the host's (CLIENT_NAME, app.info.name), the build's until the host says (vite.config.ts reads
//   CLIENT_NAME from CMakeLists.txt: __CLIENT_NAME__);
// - the chain and its coin: the network's (app.info.chainName and app.info.coin; eCash and ECX on the eCash
//   networks, Chains and CHN on the Chains networks of -devnetworks), eCash's before the network is known
//   (the default network).
// Messages name them with {product}, {chain} and {coin} (the ticker, units.ts ticker()): t() and tn() put them in,
// scripts/i18n.mjs writes the catalogs so.
import { app } from './state.svelte';
import { isEcashNetwork, ticker } from './units';

declare const __CLIENT_NAME__: string | undefined;

/** The product's name as the build has it (CMakeLists.txt CLIENT_NAME). */
export const BUILD_PRODUCT: string = typeof __CLIENT_NAME__ === 'string' && __CLIENT_NAME__ ? __CLIENT_NAME__ : 'Backbone';

/** The name of the product: "Backbone". */
export function product(): string {
  return app.info?.name || BUILD_PRODUCT;
}

/** The name of the chain of the network, and of its coin: "eCash" on the eCash networks. */
export function chainName(): string {
  const info = app.info;
  if (info?.chainName) return info.chainName;
  return !info?.network || isEcashNetwork(info.network) ? 'eCash' : 'Chains';
}

/** The initial of the product, the text mark of the brand (there is no logo of the product). */
export function productInitial(): string {
  return [...product().trim()][0]?.toLocaleUpperCase() ?? '';
}

/** {product}, {chain} and {coin} in a text that t() does not show: the names. */
export function nameTokens(text: string): string {
  return text.replace(/\{(product|chain|coin)\}/g, (_, k: string) => (k === 'product' ? product() : k === 'chain' ? chainName() : ticker()));
}
