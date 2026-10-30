<script lang="ts">
  // Back and forward of the app (lib/history.svelte.ts): its pages and the modules' as one history.
  import { t } from '../lib/i18n.svelte';
  import { goBack, goForward, history } from '../lib/history.svelte';
  import Icon from './Icon.svelte';
</script>

<div class="history" role="group" aria-label={t('History')}>
  <button type="button" class="btn ghost icon small" disabled={!history.canBack} title={t('Back (Alt+Left)')} aria-label={t('Back')} aria-keyshortcuts="Alt+ArrowLeft" data-history="back" onclick={goBack}><Icon name="back" size={16} /></button>
  <button type="button" class="btn ghost icon small" disabled={!history.canForward} title={t('Forward (Alt+Right)')} aria-label={t('Forward')} aria-keyshortcuts="Alt+ArrowRight" data-history="forward" onclick={goForward}><Icon name="forward" size={16} /></button>
</div>

<style>
  .history {
    display: inline-flex;
    gap: 0.1rem;
    flex: none;
  }
  .history button:disabled {
    opacity: 0.35;
  }
</style>
