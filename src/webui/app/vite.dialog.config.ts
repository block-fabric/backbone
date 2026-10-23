// The page of the dialogs of the host (src/webui/dialog.h): built apart from the interface, into the same folder
// (scripts/build.mjs), with its own script and style sheet named dialog-*: the origin of the dialogs serves those, the
// fonts and dialog.html, nothing of the main page (dialog::Serves).
import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';
// The name of the product (CMakeLists.txt CLIENT_NAME): __CLIENT_NAME__, %CLIENT_NAME%.
import product from './scripts/product.mjs';

export default defineConfig({
  // The runes of the modules it shares with the interface (lib/i18n.svelte.ts).
  plugins: [product(), svelte()],
  base: '/',
  build: {
    outDir: 'dist',
    emptyOutDir: false,
    target: 'es2022',
    cssCodeSplit: false,
    modulePreload: { polyfill: false },
    assetsInlineLimit: 0,
    sourcemap: false,
    reportCompressedSize: false,
    rolldownOptions: {
      input: 'dialog.html',
      output: {
        codeSplitting: false,
        entryFileNames: 'assets/dialog-[hash].js',
        assetFileNames: (info) => (info.names.some((n) => n.endsWith('.css')) ? 'assets/dialog-[hash][extname]' : 'assets/[name]-[hash][extname]'),
      },
    },
  },
});
