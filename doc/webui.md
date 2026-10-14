# chains-app: the HTML interface of Chains

`chains-app` is the node of Chains with its wallet and an HTML interface in a
native web view, in one program. It replaces `chains-qt` (src/qt, kept in the
tree but not built on this branch). What it must do is listed in
[webui-parity.md](webui-parity.md).

## How it is made

```
page (Svelte, served from chains-app://app/ in the web view, UI thread)
  │ window.__chains(request)                    ▲ window.__chainsEvents(batch)
  ▼                                             │
Bridge, on worker threads                       EventHub (batches, throttles)
  ├─ rpc  → interfaces::Node::executeRpc         ▲
  └─ host → methods of the app                  NodeEvents ◄── signals of the node and wallets

App, on a thread of its own: intro, configuration, init, then shutdown
```

* **Start and stop** (src/webui/app.cpp) follow chains-qt: command line,
  intro and `-choosedatadir`, chains.conf and settings.json (`InitConfig`, with
  the reset-or-abort question), one app per data directory (a second start
  hands its `chains:` URIs over), `-min`, `-resetguisettings`, `baseInitialize`
  and `appInitMain` on a thread of their own, wallets, a shutdown that shows
  its progress, and a stop asked from elsewhere (RPC `stop`, a signal).
* **The bridge** (bridge.cpp): the page sends `{rid, kind: rpc|host|cancel,
  method, params, wallet}`. RPC calls go to the in-process dispatcher, as the
  console of chains-qt does: no HTTP server, no credentials. Calls run on
  worker threads, never on the UI thread; a call can be cancelled.
* **Events** (nodeevents.cpp, events.cpp): tip, headers, peers, network
  activity, alerts, init messages, progress, wallets loaded and unloaded,
  their status, transactions, balances (polled as WalletModel does), address
  book. The page gets them in batches; the block tip during a sync is
  coalesced (the newest one every 250 ms).
* **The interface** (src/webui/app): Svelte 5, TypeScript, Vite. Built by
  CMake through npm, embedded in the binary (cmake/embed.cmake) and served
  from `chains-app://app/` with a content security policy that allows nothing
  but its own files: no remote content, no inline code or styles, no eval.
  Text that comes from the chain or the node is always shown as text. On
  Windows and macOS (not built yet) the interface is one self-contained
  document (assets.cpp `InlineDocument`), its script and style allowed by hash.
* **Settings**: the node's stay in settings.json in the data directory, as
  with chains-qt. The app's own (the chosen data directory, and the look,
  unit, text size, privacy mode, per network) are in
  `~/.config/Chains/chains-app.json` (as QSettings keeps them for chains-qt).

## Build

```
cmake -B build -DBUILD_GUI=OFF -DBUILD_WEBUI=ON -DENABLE_WALLET=ON
cmake --build build --target chains-app
```

Needs GTK 3 and WebKitGTK 4.1 (`libwebkit2gtk-4.1-dev`) and Node.js with npm.
The first build runs `npm ci` (exact versions of package-lock.json); later
builds need no network. `-DWEBUI_PREBUILT_DIST=<dir>` uses an interface built
elsewhere (`npm run build`) and needs no npm.

Debug options: `-webuidevtools` (web inspector), `-webuiconfig=<file>`,
`-webuitestscript=<file>` and `-webuitestshots=<dir>` (tests).

## Tests

* `test_chains-app`: the host (bridge, also in front of a real node; events;
  app settings; assets and the inline document; the single-instance socket;
  node events).
* `npm test` in src/webui/app (vitest): amounts and units, addresses, payment
  URIs, the sync label, tables, the bridge client, the state, components, and
  checks that no code renders HTML from text or loads anything from elsewhere.
  `npm run check`: types (svelte-check).
* `src/webui/test/e2e/run_smoke.py --app build/bin/chains-app --tmp <dir>
  --shots <dir>`: chains-app on regtest in a virtual display (xvfb-run), driven
  through its page; also saves pictures of every page in each theme.

All three are CTest tests (`webui_tests`, `webui_frontend_tests`,
`webui_e2e_smoke`).

## Credits

The menus and pages follow chains-qt. The classic dark theme takes its colours
from QDarkStyleSheet (MIT), as chains-qt does. Fonts: Inter, Nunito, JetBrains
Mono (SIL Open Font License 1.1). webview/webview: MIT.
