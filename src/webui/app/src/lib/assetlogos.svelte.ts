// The logos of the assets of every chain that has them (Layers, BitAssets), on the app's own pages: the logo the chain's
// own pages show, given by the host (chain.assetLogo: the chain's host checked the bytes as its rules do, the app checks
// again) as a data: URL of PNG or JPEG for an <img> only, never markup. Asked once per chain and asset, a few at a time,
// and kept; an asset without one is shown by its first letter (components/AssetLogo.svelte).
import { request } from './bridge';

export const MAX_CONCURRENT = 4;
export const MAX_KEPT = 500;
const DATA_URL = /^data:image\/(png|jpeg);base64,[A-Za-z0-9+/]+={0,2}$/;
const ID = /^[0-9a-f]{64}$/;
const STEM = /^[a-z0-9]{1,32}$/;

const shown = $state<Record<string, string>>({});
const asked = new Set<string>();
const queue: (() => Promise<void>)[] = [];
let running = 0;

/** Whether a value is a data: URL the pages show as a logo: PNG or JPEG in base64, bounded, nothing else. */
export function isLogoUrl(url: unknown): url is string {
  return typeof url === 'string' && url.length <= 44 * 1024 && DATA_URL.test(url);
}

function pump(): void {
  while (running < MAX_CONCURRENT && queue.length) {
    const job = queue.shift()!;
    running++;
    void job().finally(() => {
      running--;
      pump();
    });
  }
}

/** The logo of an asset of a chain ('' until the host gave it, and when it has none): a reactive read that asks once. */
export function assetLogo(stem: string, id: string): string {
  if (!STEM.test(stem) || !ID.test(id)) return '';
  const key = `${stem}:${id}`;
  const url = shown[key];
  if (url !== undefined) return url;
  if (!asked.has(key)) {
    asked.add(key);
    queue.push(async () => {
      try {
        const got = await request<{ asset: string; url: string } | null>('host', 'chain.assetLogo', { stem, asset: id });
        shown[key] = got && got.asset === id && isLogoUrl(got.url) ? got.url : '';
      } catch {
        shown[key] = '';
      }
      const keys = Object.keys(shown);
      if (keys.length > MAX_KEPT) {
        for (const k of keys.slice(0, keys.length - MAX_KEPT)) {
          delete shown[k];
          asked.delete(k);
        }
      }
    });
    pump();
  }
  return '';
}

/** For the tests: forget every logo. */
export function forgetLogos(): void {
  for (const k of Object.keys(shown)) delete shown[k];
  asked.clear();
}
