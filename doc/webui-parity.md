# chains-app: parity contract with chains-qt

`chains-app` replaces `chains-qt`. It must do everything the Qt GUI does. This list
is the contract: every function of `src/qt` is one line, with the Qt source it
comes from. A line is ticked once `chains-app` does it and a test or a screenshot
shows it. `src/qt` stays in the tree (not built) so it can be read while this
list is worked through.

Legend: `[x]` done in chains-app, `[~]` partly done (the note says what is left),
`[ ]` not done yet. IDs are stable: later commits and tests refer to them.

How chains-app is built and tested: doc/webui.md. Tests that cover an item:
unit tests of the host (`test_chains-app`: bridge, events, app settings, assets,
single instance, node events), of the interface (`npm test` in src/webui/app:
units, addresses, URIs, sync label, tables, bridge, state, components) and the
end-to-end smoke test (src/webui/test/e2e: A05, A12, B01, D01, D04, D05, E11,
E12, F01, F05, T01–T03, the bridge and the escaping of chain text), and the
end-to-end wallet test (src/webui/test/e2e/wallet.js: E01–E04, E06–E10, F02, G01–G05,
G07, G08, H01–H03, I01–I06, J01, J02, K01–K03, N02). The wallet functions of the
host are tested in `test_chains-app` (walletapi_tests: the same calls as chains-qt,
its rules and messages), the pages in `npm test` (wallet, dialogs, send page,
transactions page).

## A. Start-up, shutdown, process

| ID | Function | Qt source | Status |
|----|----------|-----------|--------|
| A01 | Parse the command line like chainsd; errors shown and exit 1 | bitcoin.cpp `GuiMain` | [x] |
| A02 | `-help` / `-version` print the help and exit (Qt shows a dialog; chains-app prints to stdout) | bitcoin.cpp, utilitydialog.cpp `HelpMessageDialog::showOrPrint` | [x] |
| A03 | Reject unexpected tokens on the command line, and options after a payment URI | bitcoin.cpp `GuiMain` | [x] |
| A04 | GUI options `-choosedatadir`, `-lang`, `-min`, `-resetguisettings`, `-splash` (`-uiplatform` is Qt only) | bitcoin.cpp `SetupUIArgs` | [~] `-lang` accepted, the UI is English only for now |
| A05 | Intro: choose the data directory on first start or with `-choosedatadir`; default or custom dir; size estimate; prune choice; create dir and `wallets/` | intro.cpp `Intro::showIfNeeded` | [x] |
| A06 | Remember the chosen data directory (Qt: QSettings `strDataDir`, `fReset`) | intro.cpp | [x] (app config file, see design) |
| A07 | Read chains.conf and settings.json (`InitConfig`); on a corrupt settings file ask to reset or abort; on a write failure say so | bitcoin.cpp `ErrorSettingsRead`, `ErrorSettingsWrite` | [x] |
| A08 | Network style: window title and app name per network (`[testnet]`, `[signet]`, `[regtest]`), icon tint | networkstyle.cpp | [~] title and badge done; per-network icon later |
| A09 | Splash / init progress: init messages, wallet loading progress, "press q to shut down" | splashscreen.cpp | [x] (init view; Esc/q asks to shut down) |
| A10 | Node init in-process on its own thread (`baseInitialize`, `appInitMain`), runaway exception dialog | bitcoin.cpp, initexecutor.cpp | [x] |
| A11 | Load the wallets at start (`LoadWalletsActivity`), `-min` keeps it minimized | walletcontroller.cpp | [x] |
| A12 | Clean shutdown: hide windows, show "shutting down, do not shut down the computer", `appShutdown` on a thread, exit status of the node | bitcoin.cpp `requestShutdown`, utilitydialog.cpp `ShutdownWindow` | [x] |
| A13 | Poll for a shutdown requested from elsewhere (RPC `stop`, signal) | bitcoin.cpp `pollShutdownTimer`, bitcoingui.cpp `detectShutdown` | [x] |
| A14 | Windows: block the session end until the node has stopped | winshutdownmonitor.cpp | [ ] (Windows port) |
| A15 | macOS: App Nap off while syncing, dock icon click shows the window | macos_appnap.mm, macdockiconhandler.mm | [ ] (macOS port) |
| A16 | Prune setting chosen in the intro applied (`InitPruneSetting`) | bitcoin.cpp | [x] |
| A17 | Fatal error / non-fatal internal error dialogs with the bug report link | bitcoin.cpp `handleRunawayException`, `handleNonFatalException` | [~] fatal error view done; non-fatal toast |
| A18 | `-resetguisettings` resets the GUI settings (and shows the intro again) | optionsmodel.cpp `Reset`, intro.cpp | [x] |

## B. Single instance, URIs, files

| ID | Function | Qt source | Status |
|----|----------|-----------|--------|
| B01 | A second start with a `chains:` URI hands the URI to the running app and exits | paymentserver.cpp `ipcSendCommandLine` | [x] |
| B02 | The running app listens for URIs from other starts (one listener per data directory and network) | paymentserver.cpp `startLocalServer` | [x] |
| B03 | URIs given at start are kept until the UI is ready, then handled | paymentserver.cpp `uiReady`, `savedPaymentRequests` | [x] |
| B04 | Parse BIP21 `chains:` URIs (address, amount, label, message, unknown `req-` params rejected) | guiutil.cpp `parseBitcoinURI` | [x] |
| B05 | `chains://` rejected with "use chains: instead" | paymentserver.cpp | [x] |
| B06 | Open URI dialog (Banking > Open URI…) | openuridialog.cpp | [x] Send page, "Open URI…" (pages/Send.svelte) |
| B07 | URI fills the Send page, after checking the address is valid for this network | paymentserver.cpp `handleURIOrFile`, bitcoingui.cpp `handlePaymentRequest` | [x] pages/Send.svelte `pasteUri` (fills the first empty entry, checks the network) |
| B08 | Drag and drop a URI onto the window | bitcoingui.cpp `dropEvent` | [ ] |
| B09 | Register as the handler of `chains:` (desktop file / registry) | share/, paymentserver.cpp | [ ] |
| B10 | A second start without URI brings the running window to the front (Qt fails on the datadir lock) | (improvement) | [x] |

## C. Main window, navigation, menus, shortcuts

| ID | Function | Qt source | Status |
|----|----------|-----------|--------|
| C01 | Sidebar: Wallet (Overview, Send, Receive, Transactions), Chain (Sidechains, Sidechain Nodes, Mine, Block Explorer), Node (Node Window, Settings), Theme | bitcoingui.cpp `createToolBars` | [x] shell; pages land in later phases |
| C02 | Shortcuts Alt+1..5 for the pages, Ctrl+Q quit, Ctrl+Shift+C console, Ctrl+Shift+D node window, Ctrl+Shift+M mask values, Ctrl+M minimize, Ctrl+W closes a dialog, Ctrl+/Ctrl- font size | bitcoingui.cpp, guiutil.cpp | [~] page, quit, console, mask, font, Esc closes dialogs; Ctrl+M later |
| C03 | Menu "Your Wallet": create, open (list of wallets on disk), close, close all, migrate (list + restore-and-migrate a file), sending addresses, receiving addresses, backup, restore, export watch-only, encrypt, change passphrase, mask values | bitcoingui.cpp `createMenuBar`, `createActions` | [x] lib/commands.svelte.ts (the menus in every layout and the palette, doc/webui-layouts.md) → pages/Wallets.svelte, pages/Addresses.svelte |
| C04 | Menu "Banking": send, receive, transactions, open URI, proof of funds, multisig lounge, load PSBT from file, from clipboard | bitcoingui.cpp | [~] send, receive, transactions, open URI, PSBT from file/clipboard done (pages/Psbt.svelte); proof of funds and multisig lounge later |
| C05 | Menu "Use Chains": timestamp file, sign message, verify message, sidechains | bitcoingui.cpp | [~] sign and verify message done (pages/Messages.svelte); timestamp and sidechains later |
| C06 | Menu "Chains": sidechain nodes, solo mine, sidechain proposals and votes | bitcoingui.cpp | [~] menu present |
| C07 | Menu "Crypto Tools": block explorer, hash calculator, merkle tree, address decoder | bitcoingui.cpp | [~] menu present |
| C08 | Menu "Window": minimize, (macOS zoom, main window), node window tabs with their shortcuts | bitcoingui.cpp | [~] |
| C09 | Menu "This Node": node window, options, theme (looks + custom…), font size +/-, command-line options, about, quit | bitcoingui.cpp | [~] about/help/quit/theme/font done |
| C10 | Wallet selector when more than one wallet is loaded | bitcoingui.cpp `m_wallet_selector` | [x] |
| C11 | Window title: client name, network, current wallet | bitcoingui.cpp `updateWindowTitle` | [x] |
| C12 | Actions enabled only with a wallet loaded; history disabled in privacy mode | bitcoingui.cpp `setWalletActionsEnabled`, `enableHistoryAction` | [~] |
| C13 | Remember the window geometry | bitcoingui.cpp `MainWindowGeometry` | [ ] |
| C14 | Status tips of every action (shown in the status bar on hover) | bitcoingui.cpp | [~] titles/tooltips |
| C15 | About dialog (version, licence) and command-line options dialog | utilitydialog.cpp `HelpMessageDialog` | [x] |
| C16 | Themed window frame with the menus in the title bar | themedframe.cpp | [ ] (webview window keeps the desktop frame; the menus are in the Menus button of each layout and the palette, doc/webui-layouts.md) |

## D. Status bar and notifications

| ID | Function | Qt source | Status |
|----|----------|-----------|--------|
| D01 | Unit selector in the status bar (click to choose CHN, mCHN, µCHN, sat) | bitcoingui.cpp `UnitDisplayStatusBarControl` | [x] |
| D02 | HD and encryption status icons of the current wallet (locked / unlocked) | bitcoingui.cpp `setHDStatus`, `setEncryptionStatus` | [~] encryption in the status bar; HD and lock state on pages/Wallets.svelte |
| D03 | Proxy icon when a proxy is used | bitcoingui.cpp `updateProxyIcon` | [ ] |
| D04 | Connections icon with peer count; menu: show peers tab, enable/disable network activity | bitcoingui.cpp `updateNetworkState`, `setNetworkActive` | [x] |
| D05 | Sync icon and label: connecting, syncing headers (%), pre-syncing headers, processing blocks on disk, reindexing, "x behind", up to date | bitcoingui.cpp `setNumBlocks`, `updateHeadersSyncProgressLabel` | [x] |
| D06 | Progress bar for long operations (rescan, wallet load) via `showProgress` | bitcoingui.cpp `showProgress` | [x] |
| D07 | Modal sync overlay: blocks left, last block time, progress, progress per hour, estimated time; hide; opened from the out-of-sync warning | modaloverlay.cpp | [~] sync label, progress and "behind" in the status bar; the overlay with its estimate later |
| D08 | Message boxes from the node (`ThreadSafeMessageBox`, `ThreadSafeQuestion`), errors, warnings | bitcoingui.cpp `message`, `ThreadSafeMessageBox` | [x] |
| D09 | Desktop notification for incoming and sent transactions (date, amount, wallet, type, label, address) | bitcoingui.cpp `incomingTransaction`, notificator.cpp | [~] notices in the window for new transactions (components/TxNotifier.svelte: date, amount, type, wallet, label/address; many make one); desktop notifications with the tray |
| D10 | Alerts / warnings from the node (`getWarnings`) | clientmodel.cpp | [x] |
| D11 | Tray icon with menu (show/hide, send, receive, sign, verify, options, node window, quit); minimize to tray; click toggles | bitcoingui.cpp `createTrayIcon`, `createTrayIconMenu` | [ ] |
| D12 | Out-of-sync warning labels on overview and send | overviewpage.cpp, walletview.cpp | [x] pages/Overview.svelte, pages/Send.svelte |

## E. Wallet management

| ID | Function | Qt source | Status |
|----|----------|-----------|--------|
| E01 | Create wallet: name, encrypt, disable private keys, blank, external signer | createwalletdialog.cpp, walletcontroller.cpp `CreateWalletActivity` | [x] pages/Wallets.svelte (options and their rules of CreateWalletDialog), src/webui/walletapi.cpp `wallets.create` |
| E02 | Open wallet from the list of wallets in the data directory | walletcontroller.cpp `OpenWalletActivity` | [x] pages/Wallets.svelte list, src/webui/walletapi.cpp `wallets.list`/`wallets.open` |
| E03 | Close wallet / close all wallets (with the pruning warning) | walletcontroller.cpp `closeWallet`, `closeAllWallets` | [x] pages/Wallets.svelte, src/webui/walletapi.cpp `wallets.close` (as WalletController: not loaded at start any more) |
| E04 | Restore wallet from a backup file | walletcontroller.cpp `RestoreWalletActivity` | [x] pages/Wallets.svelte, src/webui/walletapi.cpp `wallets.restore` |
| E05 | Migrate a legacy wallet; restore-and-migrate a file; watch-only/solvable wallets reported | walletcontroller.cpp `MigrateWalletActivity` | [~] pages/Wallets.svelte, src/webui/walletapi.cpp `wallets.migrate`/`wallets.restoreMigrate` (load option, passphrase, watch-only/solvables reported); no test: v32 cannot make a legacy wallet to migrate |
| E06 | Backup wallet to a file | walletview.cpp `backupWallet` | [x] pages/Wallets.svelte, src/webui/walletapi.cpp `wallet.backup` |
| E07 | Export a watch-only wallet | bitcoingui.cpp `m_export_watchonly_action` | [x] pages/Wallets.svelte, src/webui/walletapi.cpp `wallet.exportWatchOnly` |
| E08 | Encrypt wallet (passphrase twice, strength hint, caps-lock warning, final warning) | askpassphrasedialog.cpp | [x] components/DialogHost.svelte (twice, caps lock, show) and pages/Wallets.svelte (the warnings); no strength meter, as Qt has none |
| E09 | Change passphrase | askpassphrasedialog.cpp | [x] pages/Wallets.svelte `changePassphrase` |
| E10 | Unlock on demand for an operation that needs the keys | askpassphrasedialog.cpp, walletmodel.cpp `requestUnlock` | [x] lib/dialogs.svelte.ts `withUnlock`: unlocked for the call and locked again (UnlockContext); also "Unlock for a while" (walletpassphrase with a timeout) and "Lock now" |
| E11 | Mask values (privacy mode) | bitcoingui.cpp `m_mask_values_action`, overviewpage.cpp | [x] |
| E12 | Wallet list follows wallets loaded/unloaded by RPC | walletcontroller.cpp | [x] |

## F. Overview page

| ID | Function | Qt source | Status |
|----|----------|-----------|--------|
| F01 | Balances: available, pending, immature, total | overviewpage.cpp, forms/overviewpage.ui | [x] |
| F02 | Recent transactions of the wallet, click opens transactions | overviewpage.cpp, transactionoverviewwidget.cpp | [x] pages/Overview.svelte (5 latest; a click opens the transactions with it selected); and a balance chart (components/BalanceChart.svelte) |
| F03 | Latest blocks and latest mempool transactions of the chain; double click opens the block explorer | chainactivity.cpp | [~] latest blocks; mempool transactions and the explorer link later |
| F04 | Out-of-sync warning, click shows the sync overlay | overviewpage.cpp | [x] pages/Overview.svelte (opens the node window until the sync overlay comes) |
| F05 | Privacy mode masks the amounts | overviewpage.cpp `setPrivacy` | [x] |

## G. Send page

| ID | Function | Qt source | Status |
|----|----------|-----------|--------|
| G01 | Several recipients: address (validated, paste, address book), label, amount, subtract fee, use available balance, remove entry | sendcoinsentry.cpp, sendcoinsdialog.cpp | [x] pages/Send.svelte (address book, paste, label, unit, subtract fee, use available balance, remove; URI message shown) |
| G02 | Fee: recommended with confirmation target, custom fee per kvB, minimum fee warning, fee section collapsible | sendcoinsdialog.cpp | [x] pages/Send.svelte (folding fee section, targets 2–1008 blocks, smart fee and its fallback warning, custom fee per kvB not below the required fee), src/webui/walletapi.cpp `wallet.fee` |
| G03 | Coin control: inputs dialog (tree/list, lock/unlock, copy), custom change address, quantity/bytes/amount/fee/after fee/change labels with copy | coincontroldialog.cpp, sendcoinsdialog.cpp | [x] components/CoinControlDialog.svelte (tree/list, sort, (un)select all, lock/unlock, copy), components/CoinSummary.svelte, custom change address with its checks, src/webui/walletapi.cpp `wallet.coins`/`wallet.coinControl.summary` |
| G04 | Confirmation dialog with total, fee and a send delay | sendcoinsdialog.cpp `SendConfirmationDialog` | [x] pages/Send.svelte → components/DialogHost.svelte (fee, kvB, total in every unit, recipients, 3 s delay) |
| G05 | Create unsigned (PSBT): copy or save | sendcoinsdialog.cpp | [x] pages/Send.svelte `presentPsbt` (copied, save as binary .psbt), src/webui/walletapi.cpp `wallet.send.psbt` |
| G06 | External signer (hardware wallet) sign on device | sendcoinsdialog.cpp | [~] src/webui/walletapi.cpp `wallet.send.commit` signs on the device as Qt (fill, sign, finalize; partial → PSBT), "Sign on device" button; no device to test with |
| G07 | Errors: invalid address, duplicate, amount exceeds balance, absurd fee, creation failed | sendcoinsdialog.cpp `processSendCoinsReturn` | [x] src/webui/walletapi.cpp `wallet.send.prepare` (same checks and messages as prepareTransaction) |
| G08 | Clear all; balance display | sendcoinsdialog.cpp | [x] pages/Send.svelte (clear all, balance or "External balance") |

## H. Receive page

| ID | Function | Qt source | Status |
|----|----------|-----------|--------|
| H01 | Request form: label, amount, message, address type (bech32, bech32m, p2sh-segwit, legacy) | receivecoinsdialog.cpp | [x] pages/Receive.svelte (address types as Qt, taproot when enabled), src/webui/walletapi.cpp `wallet.newAddress` |
| H02 | Request dialog: QR code, URI, address, amount, label, message, copy, save image | receiverequestdialog.cpp, qrimagewidget.cpp | [x] components/RequestDialog.svelte (QR, URI, address, copy, save PNG with the address, verify on device) |
| H03 | Requested payments history: show, remove, copy URI/address/label/message/amount | receivecoinsdialog.cpp, recentrequeststablemodel.cpp | [x] pages/Receive.svelte, stored in the wallet in the format of chains-qt (src/webui/walletapi.cpp `EncodeReceiveRequest`) |

## I. Transactions page

| ID | Function | Qt source | Status |
|----|----------|-----------|--------|
| I01 | Table: status, date, type, label/address, amount; sortable; large histories | transactionview.cpp, transactiontablemodel.cpp | [x] pages/Transactions.svelte (records decomposed as TransactionRecord, src/webui/walletapi.cpp `wallet.transactions`; status ring, brackets for amounts that do not count yet) |
| I02 | Filters: date (today, week, month, last month, year, range), type, search (address, txid, label), min amount | transactionview.cpp, transactionfilterproxy.cpp | [x] pages/Transactions.svelte, lib/wallet.ts `dateRange`/`filterRecords`; plus totals of what is shown |
| I03 | Context menu: copy address, label, amount, txid, raw tx, full details; edit label; details; bump fee; abandon; show in third-party explorer | transactionview.cpp | [~] pages/Transactions.svelte context menu (all copies, details, bump, abandon, edit label); the third-party explorer links come with the options |
| I04 | Transaction details dialog (status, date, from/to, credit/debit, fee, sizes, inputs, debug) | transactiondesc.cpp, transactiondescdialog.cpp | [x] components/TxDetailsDialog.svelte, src/webui/walletapi.cpp `Describe` (TransactionDesc, debug section when logging) |
| I05 | Export to CSV | transactionview.cpp `exportClicked` | [x] pages/Transactions.svelte, lib/wallet.ts `transactionsCsv` (columns and quoting of CSVModelWriter) |
| I06 | Increase fee (bumpfee, PSBT for watch-only) | walletmodel.cpp `bumpFee` | [x] lib/txactions.ts `bumpFee` (confirmation, unsigned PSBT copied), src/webui/walletapi.cpp `wallet.bump.*` |

## J. Address book

| ID | Function | Qt source | Status |
|----|----------|-----------|--------|
| J01 | Sending addresses: list, new, edit, delete, copy, choose (for the send page), export CSV | addressbookpage.cpp, editaddressdialog.cpp | [x] pages/Addresses.svelte, components/EditAddressDialog.svelte, components/AddressPicker.svelte, src/webui/walletapi.cpp `wallet.addressBook.*` (rules and messages of EditAddressDialog) |
| J02 | Receiving addresses: list, edit label, copy, export CSV | addressbookpage.cpp | [x] pages/Addresses.svelte (receiving: label only) |

## K. Messages, PSBT

| ID | Function | Qt source | Status |
|----|----------|-----------|--------|
| K01 | Sign message (address, message, signature, copy) | signverifymessagedialog.cpp | [x] pages/Messages.svelte, src/webui/walletapi.cpp `wallet.signMessage` (P2PKH and P2WPKH, as chains-qt) |
| K02 | Verify message | signverifymessagedialog.cpp | [x] pages/Messages.svelte, src/webui/walletapi.cpp `message.verify` |
| K03 | Load PSBT from file / clipboard; PSBT operations dialog: sign, broadcast, copy, save, status and amounts | psbtoperationsdialog.cpp, walletframe.cpp `gotoLoadPSBT` | [x] pages/Psbt.svelte, src/webui/walletapi.cpp `psbt.decode`/`wallet.psbt.open`/`wallet.psbt.sign`/`psbt.broadcast` |

## L. Drivechain (Chains-specific)

| ID | Function | Qt source | Status |
|----|----------|-----------|--------|
| L01 | Sidechains tab: active sidechains (slot, title, description, escrow, active since); deposit to a sidechain with deposit address validation (slot, checksum, sidechain match) | sidechainpage.cpp | [ ] |
| L02 | Proposals tab: proposals in the chain (score, age, acks, acked by this node); ack / stop acking / remove; propose a sidechain (slot, title, description, hashes, replaces) | sidechainpage.cpp | [ ] |
| L03 | Withdrawals tab: pending bundles (slot, hash, score, blocks left, payable, failures); vote upvote / downvote / abstain / default | sidechainpage.cpp | [ ] |
| L04 | Sidechain Nodes window: built-in and added sidechains; locate programs, download a release with hash check, add/remove; start/stop node, start wallet window; auto mining on/off; mine one block with a fee; log | sidechainnodes.cpp | [ ] |
| L05 | Solo mine: address (new from wallet), threads, start/stop, status, hash rate, network stats; remembered address and threads | miningdialog.cpp | [ ] |
| L06 | Block explorer: search by height/hash/txid, blocks list, block details, transaction details, waiting transactions (mempool) | blockexplorer.cpp | [ ] |
| L07 | Timestamp file: fingerprint, stamp (publish), find the stamp in the last N blocks | timestampdialog.cpp | [ ] |
| L08 | Proof of funds: create (statement, sign with every funded address), copy, check (valid / partly / not) against the chain | proofoffundsdialog.cpp | [ ] |
| L09 | Multisig lounge: keys (partners, own keys), shared addresses (create, import descriptor, balances, copy), spend (prepare, sign, pass PSBT, broadcast) | multisigdialog.cpp | [ ] |
| L10 | Crypto tools: hash calculator, merkle tree (from txids or a block), address decoder | cryptotoolsdialog.cpp | [ ] |

## M. Node window

| ID | Function | Qt source | Status |
|----|----------|-----------|--------|
| M01 | Information: client version, user agent, datadir, blocksdir, startup time, network, connections, chain, blocks, last block time, mempool, wallet, debug log open | rpcconsole.cpp, forms/debugwindow.ui | [~] all but blocksdir and opening the debug log |
| M02 | Console: commands with history, wallet selector, help-console, nested commands and parentheses syntax, font size, clear, sensitive command filtering in history | rpcconsole.cpp `RPCExecutor`, `RPCParseCommandLine` | [ ] (bridge ready) |
| M03 | Network traffic graph with time ranges and totals | trafficgraphwidget.cpp | [ ] |
| M04 | Peers table and detail panel (all fields of the Qt view); context menu copy address, disconnect, ban 1h/1d/1w/1y | rpcconsole.cpp, peertablemodel.cpp | [ ] |
| M05 | Banned peers table: copy, unban | rpcconsole.cpp, bantablemodel.cpp | [ ] |

## N. Options

| ID | Function | Qt source | Status |
|----|----------|-----------|--------|
| N01 | Main: start on system login, prune and size, database cache, script verification threads, open configuration file, overridden-by-command-line list | optionsdialog.cpp, optionsmodel.cpp | [ ] |
| N02 | Wallet: coin control, spend unconfirmed change, PSBT controls, subtract fee by default, external signer path | optionsdialog.cpp | [x] pages/Settings.svelte "Wallet" (coin control, spend unconfirmed change and signer path in settings.json, PSBT controls, subtract fee by default) |
| N03 | Network: map port (PCP/NAT-PMP), listen, RPC server, SOCKS5 proxy, separate Tor proxy, reachable networks | optionsdialog.cpp | [ ] |
| N04 | Window: show tray icon, minimize to tray, minimize on close | optionsdialog.cpp | [ ] |
| N05 | Display: language, unit, third-party tx URLs, money font, colours, font, size, themed frame | optionsdialog.cpp, theme.cpp | [~] unit, colours, font size done |
| N06 | Reset options (restarts), restart-required notice, settings in settings.json as Qt does | optionsdialog.cpp, optionsmodel.cpp | [ ] (host settings API ready) |

## T. Themes and look

| ID | Function | Qt source | Status |
|----|----------|-----------|--------|
| T01 | Looks: system, Light, Dark, Ocean Teal, Matrix Green, Dark (classic Drivechain) | theme.cpp `THEMES` | [x] (plus Midnight, the default of chains-app: doc/webui-layouts.md, "The looks") |
| T02 | Look selector in the sidebar and the menu; saved and applied at once | bitcoingui.cpp, theme.cpp | [x] |
| T03 | Font family per look and a size adjustment (-6..+12) | theme.cpp `SaveFont` | [x] |
| T04 | Custom combination of colours and font | optionsdialog.cpp | [ ] |
