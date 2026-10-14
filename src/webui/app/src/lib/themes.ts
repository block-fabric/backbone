// The looks of the app: the themes of chains-qt (theme.cpp), as sets of CSS
// variables (styles/themes.css), plus the look of the desktop.

export type ThemeId = 'system' | 'light' | 'dark' | 'ocean' | 'matrix' | 'qdarkstyle';

export interface Theme {
  id: ThemeId;
  name: string;
  /** Whether the theme is dark (for the colour scheme of form controls). */
  dark: boolean | 'auto';
}

export const THEMES: readonly Theme[] = [
  { id: 'system', name: 'Same as the desktop', dark: 'auto' },
  { id: 'light', name: 'Light', dark: false },
  { id: 'dark', name: 'Dark', dark: true },
  { id: 'ocean', name: 'Ocean Teal', dark: true },
  { id: 'matrix', name: 'Matrix Green', dark: true },
  { id: 'qdarkstyle', name: 'Dark (classic Drivechain)', dark: true },
];

export const MIN_FONT_ADJUST = -6;
export const MAX_FONT_ADJUST = 12;
export const BASE_FONT_PX = 14;

export function isTheme(value: unknown): value is ThemeId {
  return typeof value === 'string' && THEMES.some((t) => t.id === value);
}

/** The theme drawn for a choice: "system" follows the desktop. */
export function resolveTheme(id: ThemeId, prefersDark: boolean): Exclude<ThemeId, 'system'> {
  if (id !== 'system') return id;
  return prefersDark ? 'dark' : 'light';
}

export function clampFontAdjust(n: number): number {
  if (!Number.isFinite(n)) return 0;
  return Math.max(MIN_FONT_ADJUST, Math.min(MAX_FONT_ADJUST, Math.round(n)));
}

/** Put a look on the document. */
export function applyTheme(doc: Document, id: ThemeId, fontAdjust: number, prefersDark: boolean): void {
  const resolved = resolveTheme(id, prefersDark);
  const root = doc.documentElement;
  root.dataset.theme = resolved;
  root.dataset.themeChoice = id;
  const theme = THEMES.find((t) => t.id === resolved);
  root.style.colorScheme = theme?.dark ? 'dark' : 'light';
  root.style.fontSize = `${BASE_FONT_PX + clampFontAdjust(fontAdjust)}px`;
}
