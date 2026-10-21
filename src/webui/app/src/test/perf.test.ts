import { readdirSync, readFileSync, statSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { every } from '../lib/poll';
import { balanceHistory, newest, sortNewestFirst } from '../lib/wallet';

// What keeps the UI fast on a long history and a hidden window (doc/webui-layouts.md, "Performance").

const record = (time: number, key: string) => ({ time, sortKey: key });

describe('the newest records without sorting the history', () => {
  it('are those of the full sort, in its order', () => {
    const records = Array.from({ length: 3000 }, (_, i) => record(1000 + ((i * 7919) % 50), `${String((i * 104729) % 9973).padStart(10, '0')}-0-${i}`));
    for (const count of [1, 8, 25]) expect(newest(records, count)).toEqual(sortNewestFirst(records).slice(0, count));
    expect(newest(records.slice(0, 3), 8)).toEqual(sortNewestFirst(records.slice(0, 3)));
    expect(newest([], 8)).toEqual([]);
  });

  it('the sort key compared as it is: the order of the locale for the keys of the host', () => {
    const keys = ['0000000120-0-0001700000-002-1', '0000000120-0-0001700000-010-1', '0000000099-1-0001699999-000-0', '-000000001-0-0001700001-000-1'];
    const byLocale = [...keys].sort((a, b) => b.localeCompare(a));
    expect(sortNewestFirst(keys.map((k) => record(5, k))).map((r) => r.sortKey)).toEqual(byLocale);
  });

  it('the balance over time is unchanged', () => {
    const rows = [
      { time: 2, sortKey: 'b', amount: 5n, status: 'confirmed' },
      { time: 1, sortKey: 'a', amount: 3n, status: 'confirmed' },
      { time: 2, sortKey: 'a', amount: -1n, status: 'abandoned' },
    ];
    expect(balanceHistory(rows as never)).toEqual([
      { time: 1, balance: 3n },
      { time: 2, balance: 8n },
    ]);
  });
});

describe('timers of the pages', () => {
  const setHidden = (hidden: boolean) => {
    Object.defineProperty(document, 'hidden', { configurable: true, get: () => hidden });
    document.dispatchEvent(new Event('visibilitychange'));
  };
  afterEach(() => {
    setHidden(false);
    vi.useRealTimers();
  });

  it('rest while the window is hidden, and catch up when it is shown', () => {
    vi.useFakeTimers();
    const fn = vi.fn();
    const stop = every(1000, fn, { now: true });
    expect(fn).toHaveBeenCalledTimes(1);
    vi.advanceTimersByTime(3000);
    expect(fn).toHaveBeenCalledTimes(4);
    setHidden(true);
    vi.advanceTimersByTime(10000);
    expect(fn).toHaveBeenCalledTimes(4);
    setHidden(false);
    expect(fn).toHaveBeenCalledTimes(5);
    vi.advanceTimersByTime(1000);
    expect(fn).toHaveBeenCalledTimes(6);
    stop();
    vi.advanceTimersByTime(5000);
    setHidden(true);
    setHidden(false);
    expect(fn).toHaveBeenCalledTimes(6);
  });
});

describe('the styles stay cheap to paint (software GL, as under WSLg)', () => {
  const files: { file: string; css: string }[] = [];
  const walk = (dir: string) => {
    for (const name of readdirSync(dir)) {
      const path = join(dir, name);
      if (statSync(path).isDirectory()) {
        if (name !== 'test') walk(path);
      } else if (name.endsWith('.css') || name.endsWith('.svelte')) files.push({ file: path, css: readFileSync(path, 'utf8') });
    }
  };
  walk(resolve(__dirname, '..'));

  it('no shadow, filter or blur animated; no backdrop blur; no fixed background', () => {
    for (const { file, css } of files) {
      for (const m of css.matchAll(/transition:([^;]*);/g)) expect(m[1], file).not.toMatch(/box-shadow|filter|all\b/);
      expect(css, file).not.toMatch(/backdrop-filter/);
      expect(css, file).not.toMatch(/background-attachment:\s*fixed/);
    }
  });

  it('the ambient light is a layer of its own, painted once', () => {
    const base = readFileSync(resolve(__dirname, '../styles/base.css'), 'utf8');
    const layer = /body::before \{([^}]*)\}/.exec(base)?.[1] ?? '';
    expect(layer).toMatch(/position:\s*fixed/);
    expect(layer).toMatch(/var\(--ambient\)/);
    expect(layer).toMatch(/will-change:\s*transform/);
  });
});

describe('the frames of the sidechains at the scale of the hub (doc/unified-app.md §21.3)', () => {
  afterEach(() => vi.useRealTimers());

  it('only the newest of their node\'s state, once a second; everything while hidden waits, then the newest', async () => {
    vi.useFakeTimers();
    const { deliver, registerFrame, setFrameHidden, FRAME_STATE_MS } = await import('../lib/packs.svelte');
    const posted: { type?: string; data?: unknown; batch?: string }[] = [];
    const win = { postMessage: (m: Record<string, unknown>) => posted.push(m as never) } as unknown as Window;
    registerFrame('layers', win);
    const batch = (...events: [string, unknown][]) => JSON.stringify(events.map(([type, data]) => ({ type, data })));
    const sent = () => posted.splice(0).map((m) => (m.batch ? JSON.parse(m.batch).map((e: { type: string; data: { height?: number } }) => `${e.type}${e.data?.height ?? ''}`) : `${m.type}:${JSON.stringify(m.data)}`));
    // Shown: the transaction at once, the tip at once (the first), the next ones once a second, the newest.
    deliver('layers', batch(['tip', { height: 1 }], ['wallet.tx', { txid: 'a' }]));
    expect(sent()).toEqual([['wallet.tx'], ['tip1']]);
    deliver('layers', batch(['tip', { height: 2 }]));
    deliver('layers', batch(['tip', { height: 3 }], ['mempool', { size: 1 }]));
    expect(sent()).toEqual([]);
    vi.advanceTimersByTime(FRAME_STATE_MS);
    expect(sent()).toEqual([['tip3', 'mempool']]);
    // Hidden: told so (its pages rest), its state kept, the transactions of its wallet still handed.
    setFrameHidden('layers', true);
    expect(sent()).toEqual(['visibility:{"hidden":true}']);
    for (let h = 4; h < 40; h++) deliver('layers', batch(['tip', { height: h }]));
    deliver('layers', batch(['wallet.tx', { txid: 'b' }]));
    vi.advanceTimersByTime(60000);
    expect(sent()).toEqual([['wallet.tx']]);
    // Shown again: the newest tip alone.
    setFrameHidden('layers', false);
    expect(sent()).toEqual(['visibility:{"hidden":false}', ['tip39']]);
    registerFrame('layers', null);
  });
});

describe('the parts of a page that every block and payment changes (doc/unified-app.md §21.3)', () => {
  afterEach(() => vi.useRealTimers());

  it('follow their data at most once a period, the newest (at once while they start)', async () => {
    vi.useFakeTimers();
    const { render } = await import('@testing-library/svelte');
    const { flushSync } = await import('svelte');
    const { WARMUP_MS } = await import('../lib/throttle.svelte');
    const Harness = (await import('./ThrottleHarness.svelte')).default;
    const { component } = render(Harness, { ms: 10000 });
    const set = (n: number) => (component as unknown as { set: (n: number) => void }).set(n);
    const text = () => document.querySelector('[data-throttled]')!.textContent;
    set(1);
    flushSync();
    expect(text()).toBe('1');
    vi.advanceTimersByTime(WARMUP_MS);
    // Then a change a period after the one before at most, the newest.
    for (let i = 2; i < 20; i++) {
      set(i);
      flushSync();
      vi.advanceTimersByTime(300);
    }
    expect(text()).toBe('1');
    vi.advanceTimersByTime(10000);
    flushSync();
    expect(text()).toBe('19');
    // Quiet for a period: the next at once.
    vi.advanceTimersByTime(10000);
    set(20);
    flushSync();
    expect(text()).toBe('20');
  });

  it('a sidechain whose pages are not shown raises a notice at most every 30 s (the bell has them all)', async () => {
    const { allowHiddenRaise, HIDDEN_RAISE_MS } = await import('../lib/modules.svelte');
    expect(allowHiddenRaise('thunder', 1000)).toBe(true);
    expect(allowHiddenRaise('thunder', 2000)).toBe(false);
    expect(allowHiddenRaise('layers', 2000)).toBe(true);
    expect(allowHiddenRaise('thunder', 1000 + HIDDEN_RAISE_MS)).toBe(true);
  });

  it('a text of the outside: its direction set once, again only when it changes (not at each update of the row)', async () => {
    const { autoDir } = await import('../lib/actions');
    const el = document.createElement('span');
    const seen: string[] = [];
    new MutationObserver((rs) => rs.forEach((r) => seen.push(r.attributeName ?? ''))).observe(el, { attributes: true });
    autoDir('Alice')(el);
    autoDir('Alice')(el);
    autoDir('Alice')(el);
    autoDir('Bob')(el);
    await Promise.resolve();
    expect(el.getAttribute('dir')).toBe('auto');
    expect(seen).toEqual(['dir', 'dir']);
  });
});
