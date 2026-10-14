# chains-app: the HTML interface of Chains

`chains-app` is the node of Chains with its wallet and an HTML interface in a
native web view, in one program, without Qt. It replaces `chains-qt` (src/qt,
kept in the tree but not built on this branch). What it must do is listed in
[webui-parity.md](webui-parity.md); the audit of it, and how each finding was
closed, in [webui-audit.md](webui-audit.md).

This document says how to build and run it, and how it is made, so that the
sidechains can take it over for their own apps.

## Build

```
cmake -B build -DBUILD_GUI=OFF -DBUILD_WEBUI=ON -DENABLE_WALLET=ON -DBUILD_TESTS=ON
cmake --build build --target chains-app test_chains-app
```

* **Needs**: GTK 3 and WebKitGTK 4.1 (`libwebkit2gtk-4.1-dev`), Node.js with
  npm. The first build runs `npm ci` in src/webui/app (the exact versions of
  package-lock.json); later builds need no network.
* `-DWEBUI_PREBUILT_DIST=<dir>` takes an interface built elsewhere
  (`npm run build` in src/webui/app) and needs no npm.
* The interface is built by `npm run build`: the catalogs of the languages
  first (`scripts/i18n.mjs`, from src/qt/locale when it is there; the catalogs
  made are committed in `public/locales`), then Vite. The result is embedded in
  the binary (cmake/embed.cmake → `assets_data.cpp`).
* Windows (WebView2) and macOS (WKWebView) build the generic window
  (window_generic.cpp); their port (file choosers, tray, frame, session end,
  App Nap) is the next phase, see the parity list.

Working on the interface alone: `npm install` then `npm run dev` in
src/webui/app opens it in a browser with a pretend node (lib/mock.ts).

## Run

```
build/bin/chains-app [options of chainsd] [chains:URI]
```

* The options of chainsd, and the GUI options of chains-qt: `-choosedatadir`,
  `-lang=<lang>`, `-min`, `-resetguisettings`, `-splash`.
* Settings: those of the node stay in `settings.json` of the data directory,
  as with chains-qt. Those of the app (the data directory chosen in the intro,
  the window, the look, the unit, the language of the menus...) are in
  `~/.config/Chains/chains-app.json`, one set per network.
* One app per data directory: a second start hands its `chains:` URIs to the
  first one (single-instance socket in a private folder of the user).
* Debug options, from the command line only (a chains.conf that sets them is
  ignored): `-webuidevtools` (web inspector), `-webuiconfig=<file>` (another
  settings file of the app), `-webuitestscript=<file>` and
  `-webuitestshots=<dir>` (the tests: a script run in the page, with test
  methods on the bridge).

## How it is made

```
page (Svelte, chains-app://app/, UI thread)            the user
  │ window.__chains(request)          ▲ window.__chainsEvents(batch)
  ▼                                   │
Bridge                                EventHub (batches, coalesces, throttles)
  ├─ quick lane (4 workers)           ▲
  ├─ long lane (6 workers)            NodeEvents ◄── signals of the node and wallets
  ├─ UI thread (window, clipboard)
  ├─ rpc  → RPC policy → interfaces::Node::executeRpc
  └─ host → methods of the app (app.cpp, walletapi.cpp, nodeapi.cpp, ...)

App, on a thread of its own: intro, configuration, init, then shutdown
```

### The host (src/webui, C++)

| File | What it does |
|------|--------------|
| main.cpp | The command line, as chains-qt: options, payment URIs, `-help`; starts App; translations of the node (`G_TRANSLATION_FUN`) |
| app.cpp | The life of the app: intro, `InitConfig`, single instance, node init and shutdown on a thread, the phases the page follows, dialogs of the node, the host methods of the app (files chosen by the user, clipboard, preferences, options, the window, the tray, notifications), the language, the frame |
| bridge.cpp | Requests of the page: the RPC policy, the lanes, cancellation, answers made valid UTF-8, secrets wiped after use, the internal error hook |
| rpcpolicy.cpp | The RPC methods the pages may call, and which of them run on the long lane |
| events.cpp | The events to the page: batched, a key coalesces the ones that change fast (the tip during a sync) |
| nodeevents.cpp | Signals of the node and of each wallet made into events; balances polled as WalletModel does |
| walletapi.cpp | The wallet functions of chains-qt (WalletModel, WalletController, the wallet dialogs) as host methods, with the same interfaces::Wallet calls, rules and messages |
| nodeapi.cpp, console.cpp | The node window: the console (RPCParseCommandLine and RPCExecutor without Qt), peers and bans, tools (hashes, merkle trees, decoders, deposit addresses, stamps) |
| options.cpp | The options of the node as OptionsModel keeps them, and `OptionsJournal`: the changes of the Settings page wait for Apply |
| sidechainnodes.cpp | The Sidechain Nodes window: install, start, stop the nodes of sidechains |
| i18n.cpp | The language: chosen as chains-qt chooses it, the catalog of the page, the translation of the node's messages |
| icontint.cpp | The icon of the network (NetworkStyle) |
| assets.cpp | The embedded interface, its content security policy, the inline document of the other platforms |
| appconfig.cpp, singleinstance.cpp | The settings file of the app; one app per data directory |
| window_gtk.cpp, tray_gtk.cpp | The window on Linux: WebKitGTK through webview/webview, file choosers, clipboard, the frame drawn by the page, the tray (StatusNotifierItem) and notifications |
| window_generic.cpp | The window of the other platforms, through webview/webview (to be completed by their port) |

**The bridge.** The page sends `{rid, kind: rpc|host|cancel, method, params,
wallet}`. RPC calls go to the in-process dispatcher, as the console of
chains-qt does: no HTTP server, no credentials. They are limited to the
methods of `rpcpolicy.cpp` (a frontend test checks that every method the pages
name is there); the console (`console.run`) is the one way to run any command.
Calls never run on the UI thread. Calls that may take long (a command of the
console, a download, a rescan, a file chooser, loading a wallet,
`scantxoutset`) run on a lane of their own and never hold up the quick ones. A
call can be cancelled. Every answer and every event batch is made valid UTF-8
(text of the chain is any bytes, and WebKit drops a script that is not UTF-8).
Strings named `passphrase`, `old`, `new` are wiped once a request is handled.
A host method that throws something it did not expect shows the internal error
dialog of chains-qt.

**Keys.** Calls that need the keys of an encrypted wallet take its passphrase
for that call: the host unlocks the wallet, runs the call and locks it again
(`UnlockScope`, as WalletModel::UnlockContext). A locked wallet answers -13 and
the page asks for the passphrase. RPC methods that need keys (deposits, stamps,
signing a PSBT, proofs of funds) go through `wallet.withKeys` the same way; the
page never leaves a wallet unlocked for a while.

**The history.** `wallet.transactions` gives the whole history once (records as
TransactionRecord makes them, as arrays), the records of some transactions
(`txids`: the ones an event named), or the newest (`latest`). Each record says
the height of its block and its state, so the page brings the status of every
record up to date with each block by itself
(TransactionRecord::updateStatus); only transactions out of a block are asked
for again. The balance chart is made by the host (`wallet.balanceHistory`).

**Settings.** The options of the node are written to settings.json as they
change, and what they replaced is kept by `OptionsJournal` (in chains-app.json):
Apply forgets it, Cancel puts it back, and the next start puts back what was
never applied, before the node reads its settings.

**Languages.** The catalogs of chains-qt (src/qt/locale, about 100 languages)
are made into catalogs of the app by `scripts/i18n.mjs`: the messages the
interface shows (normalized as it writes them: no mnemonic `&`, `…`, Chains for
Bitcoin, labels without their colon) with their plural forms, and the messages
of the node. The host chooses the language (-lang, the option, the locale of
the system) and gives the catalog to the page (`app.locale`); the page
translates what it shows, and the node's messages go through
`G_TRANSLATION_FUN`.

### The interface (src/webui/app, Svelte 5 + TypeScript + Vite)

```
src/
  main.ts, App.svelte     start: hello, language, frame, phases (intro, splash, shell, fatal)
  views/                  Shell (menus, sidebar, page, status bar), Intro, Splash, Fatal
  pages/                  one per page of chains-qt (Overview, Send, Receive, Transactions,
                          Sidechains, Mine, Explorer, NodeWindow, Settings, Wallets, ...)
  components/             shared parts: DataTable (virtual, sortable), Dialog, DialogHost,
                          AmountInput, AddressInput, AppMenu, Sidebar, StatusBar, SyncOverlay,
                          Toasts, WindowButtons, FrameBar, FrameEdges, ...
  lib/
    bridge.ts             the transport, requests, events
    rpc.ts                typed RPC and host calls
    state.svelte.ts       the state of the app, the node, the wallets, the preferences
    walletdata.svelte.ts  the current wallet: info, history kept up to date, address book
    wallet.ts             wallet calls, record status, filters, CSV
    i18n.svelte.ts        the language: t(), tn(), the translator of the page
    frame.svelte.ts       the frame of the window drawn by the page
    settingsdraft.svelte.ts  the changes of Settings until Apply
    nav.svelte.ts, keyboard.ts, dialogs.svelte.ts, units.ts, format.ts, ...
  styles/                 themes.css (colour tokens of the themes), base.css (shared rules)
  test/                   vitest: pages, components, the audit fixes, contrast, translations
public/locales/           the catalogs of the languages (made by scripts/i18n.mjs)
```

Rules the code keeps:

* **Text is text.** No `{@html}`, no `innerHTML`: what comes from the chain,
  the node or the user shows as text (a test checks the sources). The content
  security policy allows nothing but the app's own files: no remote content,
  no inline scripts or styles, no `eval`.
* **Translations.** Write the interface in English, with the words of
  chains-qt where it has them (they are then translated by its catalogs).
  Elements that show text of the user or of the chain take `translate="no"`.
  Text built in code can use `t()` and `tn()` (lib/i18n.svelte.ts). After a
  change of the texts, `npm run i18n` makes the catalogs again (a test checks
  they are up to date).
* **Themes.** Colours are tokens of themes.css (`--text`, `--text-muted`,
  `--text-faint`, `--accent`, `--accent-text`, ...): every text token reaches
  4.5:1 on every surface of every theme (test/contrast.test.ts measures it).
  Use `--accent-text`, not `--accent`, for text.
* **Narrow windows.** Pages wrap rather than clip; grids use
  `minmax(min(Xrem, 100%), 1fr)`; below 900 px the sidebar shows icons; menus
  that do not fit fold into "More"; tables can `fit` the width.
* **Long lists.** Keep them as raw state (`$state.raw`), replaced as a whole,
  and draw them with DataTable (only the rows in view).

## Taking it over for a sidechain

What is generic, and moves as it is: the bridge (and its lanes, policy hook,
UTF-8 repair), the events, the window and the tray of GTK, the assets and their
policy, the settings file of the app, the single instance, the languages, the
frame, and in the interface: lib/ (bridge, state, i18n, frame, nav, keyboard,
dialogs, units, format), the components, the themes, the shell and the
settings page.

What each chain sets: `CLIENT_NAME` and the scheme of its payment URIs
(main.cpp), its RPC policy (rpcpolicy.cpp: the methods its pages call), its
pages (src/webui/app/src/pages) and their menus (views/Shell.svelte), its
host methods (walletapi.cpp holds the ones of a Bitcoin-like wallet), and in
`scripts/i18n.mjs` the name that takes the place of "Bitcoin" in the catalogs.

## Tests

| Test | What it covers |
|------|----------------|
| `test_chains-app` | The host: the bridge (also in front of a real node), its lanes, policy, UTF-8 repair, wiped secrets and internal errors; events; app settings; assets and the inline document; single instance; node events; the wallet functions on a regtest chain (also without wallets, the history in parts, keys for one call, dust); the node window, options and their journal; the languages; the icon of the network |
| `npm test` (vitest) | The interface: units, addresses, URIs, sync labels, tables, the bridge client, the state, components, wallet helpers, dialogs, pages (send, transactions, settings, sidechains, node window...), the history kept up to date, the RPC policy, the contrast of every theme, the translations, narrow layouts, the fixes of the audit |
| `npm run check` | Types (svelte-check) |
| `e2e/run_smoke.py` | chains-app on regtest in a virtual display, driven through its page: the bridge, a wallet, events, preferences, escaping, shortcuts, a URI handed over by a second start; pictures of every page in each theme |
| `e2e/run_wallet.py` | The wallet pages as a user drives them |
| `e2e/run_node.py` | The node and Chains pages, with a pretend desktop (tray, notifications) and a second node |
| `e2e/run_audit.py` | The fixes of the audit: the sync overlay, a block built by hand with a title that is not UTF-8, long calls, a history kept up to date in parts, a proof with a locked wallet, contrast measured on what the page draws, aligned tables, the frame, settings that wait for Apply (and the next start), `-disablewallet`, windows 700 and 480 px wide, a hardware wallet (the mock signer of the functional tests), `-lang=fr` |

Each e2e script takes `--app build/bin/chains-app --tmp <dir> --shots <dir>`;
all are CTest tests (`webui_tests`, `webui_frontend_tests`,
`webui_frontend_types`, `webui_e2e_smoke`, `webui_e2e_wallet`,
`webui_e2e_node`, `webui_e2e_audit`).

## Credits

The menus and pages follow chains-qt. The translations are the work of the
translators of Bitcoin Core (Transifex), as chains-qt ships them. The tray
speaks the StatusNotifierItem and dbusmenu protocols, and the notifications
the Desktop Notifications specification (freedesktop.org); no code of other
projects is in them. The classic dark theme takes its colours from
QDarkStyleSheet (MIT), as chains-qt does. Fonts: Inter, Nunito, JetBrains Mono
(SIL Open Font License 1.1). webview/webview: MIT.
