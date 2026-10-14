// Text reaches the 4.5:1 contrast of WCAG AA in every theme (audit M5): each colour text is drawn in,
// on each colour it is drawn on.
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { describe, expect, it } from 'vitest';
import { THEMES } from '../lib/themes';

const CSS = readFileSync(join(__dirname, '../styles/themes.css'), 'utf8');

function themes(): Record<string, Record<string, string>> {
  const out: Record<string, Record<string, string>> = {};
  for (const m of CSS.matchAll(/(:root[^{]*)\{([^}]*)\}/g)) {
    if (/data-font|data-money/.test(m[1])) continue;
    const name = m[1].match(/data-theme='(\w+)'/)?.[1] ?? 'light';
    const vars: Record<string, string> = {};
    for (const v of m[2].matchAll(/--([\w-]+):\s*(#[0-9a-fA-F]{6})/g)) vars[v[1]] = v[2];
    out[name] = { ...(out[name] ?? {}), ...vars };
  }
  return out;
}

function luminance(hex: string): number {
  const [r, g, b] = [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16) / 255).map((v) => (v <= 0.04045 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4));
  return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

export function contrast(a: string, b: string): number {
  const [hi, lo] = [luminance(a), luminance(b)].sort((x, y) => y - x);
  return (hi + 0.05) / (lo + 0.05);
}

const TEXT = ['text', 'text-muted', 'accent-text', 'accent-strong', 'link', 'positive', 'negative', 'warning'];
const SURFACES = ['bg', 'surface', 'surface-2', 'raised'];

describe('the contrast of text', () => {
  const all = themes();

  it('is defined for every theme of the menus', () => {
    for (const t of THEMES.filter((t) => t.id !== 'system')) expect(Object.keys(all)).toContain(t.id);
  });

  for (const [name, v] of Object.entries(all)) {
    it(`reaches 4.5:1 in the ${name} theme`, () => {
      const failing: string[] = [];
      const check = (fg: string, bg: string) => {
        const r = contrast(v[fg], v[bg]);
        if (!(r >= 4.5)) failing.push(`${fg} on ${bg}: ${r.toFixed(2)}`);
      };
      for (const fg of TEXT) for (const bg of SURFACES) check(fg, bg);
      // Captions and hints: never drawn on controls (raised).
      for (const bg of ['bg', 'surface', 'surface-2']) check('text-faint', bg);
      // Buttons, and the badges.
      check('on-accent', 'accent');
      check('warning', 'warning-soft');
      check('accent-strong', 'accent-soft');
      check('negative', 'danger-soft');
      expect(failing).toEqual([]);
    });
  }
});
