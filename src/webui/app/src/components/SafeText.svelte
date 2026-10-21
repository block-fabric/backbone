<script lang="ts">
  // Text that comes from the chain, peers, the wallet or the user (a label, a memo, the title of a
  // sidechain, the version string of a peer, a payment URI): shown exactly as it is. It is never looked up
  // in the catalogs of the app (src/lib/i18n.svelte.ts), and translate="no" keeps the translator of the
  // browser off it too. src/test/i18n-lint.test.ts finds such fields shown without it.
  // dir="auto" isolates it as to direction (unicode-bidi: isolate): a mark that turns text around (U+202E)
  // inside it cannot turn the words of the app around it (audit port-1 #2).
  //
  // The direction is set by `autoDir`, again only when the text changes: a static dir="auto" makes Svelte assign it
  // again at each update of the element (a fix of its for Chromium), and a list of rows read again (every block of every
  // chain) then wrote it on every row, each a change of the page (doc/unified-app.md §21.3).
  import { autoDir } from '../lib/actions';

  let { text, class: className = '', title = undefined }: { text: string | null | undefined; class?: string; title?: string } = $props();
</script>

<span class={className || undefined} translate="no" {title} {@attach autoDir(text ?? '')}>{text ?? ''}</span>
