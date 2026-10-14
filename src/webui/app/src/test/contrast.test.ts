import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';
import { contrastRatio, themeTokens } from '../lib/contrast';
import { DEFAULT_THEME, THEMES } from '../lib/themes';

const tokens = themeTokens(readFileSync(join(dirname(fileURLToPath(import.meta.url)), '../styles/themes.css'), 'utf8'));

/** Text colours on the backgrounds they are drawn on, and the ratio they need (WCAG AA: 4.5 for text, 3 for parts of controls and icons). */
const PAIRS: [fg: string, bg: string, min: number][] = [
  ...['bg', 'surface', 'surface-2', 'raised'].flatMap((bg): [string, string, number][] => [
    ['text', bg, 4.5],
    ['text-muted', bg, 4.5],
  ]),
  ...['bg', 'surface', 'surface-2'].map((bg): [string, string, number] => ['text-faint', bg, 4.5]),
  ...['bg', 'surface', 'surface-2'].flatMap((bg): [string, string, number][] => [
    ['link', bg, 4.5],
    ['positive', bg, 4.5],
    ['negative', bg, 4.5],
    ['warning', bg, 4.5],
    ['accent-fg', bg, 4.5],
  ]),
  ['on-accent', 'accent', 4.5],
  ['accent-strong', 'accent-soft', 4.5],
  ['text', 'accent-soft', 4.5],
  ['warning', 'warning-soft', 4.5],
  ['negative', 'danger-soft', 4.5],
  ['text', 'danger-soft', 4.5],
  ['positive', 'accent-soft', 4.5],
  ['bg', 'text', 4.5],
];

describe('the themes', () => {
  it('are all in the style sheet, Midnight the default', () => {
    const drawn = THEMES.filter((t) => t.id !== 'system').map((t) => t.id);
    expect(Object.keys(tokens).sort()).toEqual([...drawn].sort());
    expect(DEFAULT_THEME).toBe('midnight');
  });

  for (const [theme, vars] of Object.entries(tokens)) {
    it(`${theme}: every text colour passes WCAG AA on its backgrounds`, () => {
      const failures: string[] = [];
      for (const [fg, bg, min] of PAIRS) {
        expect(vars[fg], `--${fg}`).toMatch(/^#/);
        expect(vars[bg], `--${bg}`).toMatch(/^#/);
        const ratio = contrastRatio(vars[fg], vars[bg]);
        if (ratio < min) failures.push(`--${fg} on --${bg}: ${ratio.toFixed(2)} < ${min}`);
      }
      expect(failures).toEqual([]);
    });
  }
});
