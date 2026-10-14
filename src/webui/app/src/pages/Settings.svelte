<script lang="ts">
  // Settings (optionsdialog.cpp). The look is here now; the options of the node come next.
  import { nav } from '../lib/nav.svelte';
  import { prefs, savePrefs } from '../lib/state.svelte';
  import { clampFontAdjust, MAX_FONT_ADJUST, MIN_FONT_ADJUST, THEMES, type ThemeId } from '../lib/themes';
  import { UNITS, type Unit } from '../lib/units';

  const TABS = [
    { id: 'display', label: 'Display' },
    { id: 'main', label: 'Main' },
    { id: 'wallet', label: 'Wallet' },
    { id: 'network', label: 'Network' },
    { id: 'window', label: 'Window' },
  ];
  const tab = $derived(TABS.some((t) => t.id === nav.tab) ? nav.tab : 'display');
</script>

<div class="page">
  <header class="page-head"><h1>Settings</h1></header>
  <div class="tabs" role="tablist" aria-label="Settings">
    {#each TABS as t (t.id)}
      <button type="button" role="tab" aria-selected={tab === t.id} onclick={() => (nav.tab = t.id)}>{t.label}</button>
    {/each}
  </div>

  {#if tab === 'display'}
    <section class="panel">
      <div class="panel-body form">
        <fieldset class="field">
          <legend class="label">Theme</legend>
          <div class="themes">
            {#each THEMES as theme (theme.id)}
              <label class="theme" class:chosen={prefs.theme === theme.id} data-swatch={theme.id}>
                <input type="radio" name="theme" class="sr-only" checked={prefs.theme === theme.id} onchange={() => savePrefs({ theme: theme.id as ThemeId })} />
                <span class="swatch" aria-hidden="true"><span></span><span></span><span></span></span>
                <span>{theme.name}</span>
              </label>
            {/each}
          </div>
        </fieldset>

        <div class="field">
          <label for="font-adjust">Text size</label>
          <div class="row">
            <input id="font-adjust" type="range" min={MIN_FONT_ADJUST} max={MAX_FONT_ADJUST} step="1" value={prefs.fontAdjust} oninput={(e) => savePrefs({ fontAdjust: clampFontAdjust(Number((e.currentTarget as HTMLInputElement).value)) })} />
            <span class="num">{prefs.fontAdjust > 0 ? '+' : ''}{prefs.fontAdjust} pt</span>
          </div>
          <span class="hint">How much larger, or smaller, than the default. Also Ctrl+ and Ctrl− anywhere.</span>
        </div>

        <div class="field">
          <label for="unit">Unit to show amounts in</label>
          <select id="unit" value={prefs.unit} onchange={(e) => savePrefs({ unit: (e.currentTarget as HTMLSelectElement).value as Unit })}>
            {#each UNITS as u (u.id)}<option value={u.id}>{u.short} — {u.long}</option>{/each}
          </select>
          <span class="hint">The default subdivision unit to show in the interface and when sending coins.</span>
        </div>

        <label class="check">
          <input type="checkbox" checked={prefs.mask} onchange={(e) => savePrefs({ mask: (e.currentTarget as HTMLInputElement).checked })} />
          <span>Mask values (privacy mode)</span>
        </label>
      </div>
    </section>
  {:else}
    <div class="panel planned">
      <p>The {TABS.find((t) => t.id === tab)?.label.toLowerCase()} options of chains-qt come here next (parity items N01–N06).</p>
    </div>
  {/if}
</div>

<style>
  .form {
    display: flex;
    flex-direction: column;
    gap: 1.4rem;
    padding-top: 1.1rem;
    max-width: 52rem;
  }
  fieldset {
    margin: 0;
    padding: 0;
    border: 0;
  }
  .themes {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(11rem, 1fr));
    gap: 0.6rem;
  }
  .theme {
    display: flex;
    flex-direction: column;
    gap: 0.45rem;
    padding: 0.6rem;
    border: 1px solid var(--line);
    border-radius: var(--radius-m);
    cursor: pointer;
  }
  .theme:has(input:focus-visible) {
    outline: 2px solid var(--focus);
    outline-offset: 2px;
  }
  .theme.chosen {
    border-color: var(--accent);
    box-shadow: inset 0 0 0 1px var(--accent);
  }
  .swatch {
    display: flex;
    height: 2.4rem;
    border-radius: var(--radius-s);
    overflow: hidden;
    border: 1px solid var(--line);
  }
  .swatch span {
    flex: 1;
  }
  /* The colours of each look, shown whatever the look of the app. */
  [data-swatch='system'] .swatch span:nth-child(1) { background: #eef1f5; }
  [data-swatch='system'] .swatch span:nth-child(2) { background: #181b20; }
  [data-swatch='system'] .swatch span:nth-child(3) { background: #4d9fff; }
  [data-swatch='light'] .swatch span:nth-child(1) { background: #eef1f5; }
  [data-swatch='light'] .swatch span:nth-child(2) { background: #ffffff; }
  [data-swatch='light'] .swatch span:nth-child(3) { background: #0a8f5e; }
  [data-swatch='dark'] .swatch span:nth-child(1) { background: #111316; }
  [data-swatch='dark'] .swatch span:nth-child(2) { background: #262b32; }
  [data-swatch='dark'] .swatch span:nth-child(3) { background: #4d9fff; }
  [data-swatch='ocean'] .swatch span:nth-child(1) { background: #04202a; }
  [data-swatch='ocean'] .swatch span:nth-child(2) { background: #123f4e; }
  [data-swatch='ocean'] .swatch span:nth-child(3) { background: #2ec4b6; }
  [data-swatch='matrix'] .swatch span:nth-child(1) { background: #000000; }
  [data-swatch='matrix'] .swatch span:nth-child(2) { background: #061e0c; }
  [data-swatch='matrix'] .swatch span:nth-child(3) { background: #00ff41; }
  [data-swatch='qdarkstyle'] .swatch span:nth-child(1) { background: #19232d; }
  [data-swatch='qdarkstyle'] .swatch span:nth-child(2) { background: #32414b; }
  [data-swatch='qdarkstyle'] .swatch span:nth-child(3) { background: #148cd2; }
  input[type='range'] {
    width: 18rem;
    accent-color: var(--accent);
  }
  .planned {
    padding: 1.5rem;
    color: var(--text-muted);
  }
</style>
