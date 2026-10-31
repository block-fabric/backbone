<script lang="ts">
  // A drop-in's wallet (Thunder on Backbone, doc/unified-app.md §26) is its node's own, in its own pages: sending,
  // receiving, its payments, its backup phrase and its coins in and out of it are there. Said where the app's own page
  // would act on it, with its pages a click away.
  import { t } from '../lib/i18n.svelte';
  import SafeText from './SafeText.svelte';
  import { openModule } from '../lib/modules.svelte';

  let { stem, name, page = 'home', what = 'Its wallet' }: { stem: string; name: string; page?: string; what?: string } = $props();
</script>

<section class="panel own-wallet" data-own-wallet={stem}>
  <p><SafeText text={t('The wallet of %1 is in its own pages: send, receive, its payments, its backup phrase, and coins moved in and out of it.', name)} /></p>
  <button type="button" class="btn" onclick={() => openModule(stem, page)} data-open-own={stem}>{t(what)}: <SafeText text={name} /></button>
</section>

<style>
  .own-wallet {
    padding: 1rem 1.3rem;
    display: flex;
    flex-direction: column;
    gap: 0.7rem;
    align-items: flex-start;
  }
  .own-wallet p {
    margin: 0;
  }
</style>
