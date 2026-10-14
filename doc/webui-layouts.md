# chains-app: the layouts of the main window

The main window of `chains-app` comes in three layouts, switched live in
**Settings → Appearance → Layout** (or from the command palette: "Layout: …"),
and kept in the GUI preferences of the app (`prefs.layout`, per network, in the
app settings file next to the theme and the unit). The pages are the same in all
three: only the navigation, the menus and the frame around the page change.

| Layout | Id | What it looks like |
|--------|----|--------------------|
| Workspace tabs | `workspace` | Four areas across the top — **Wallet**, **Chain**, **Sidechains**, **Node** — each with a strip of sub-tabs (Wallet: Overview, Send, Receive, History, Addresses, Messages, PSBT, Wallets, Proof of funds, Multisig; Chain: Explorer, Mine, Timestamp, Crypto tools; Sidechains: Sidechains, Sidechain nodes; Node: Console, Peers, Info, Traffic, Settings). The wallet selector, the palette field and a **Menus** button sit at the right of the top bar. A **detail panel** opens at the right of the page when a row is selected (a transaction in the history; blocks, peers and sidechains as their pages publish them). The status bar stays. |
| Dashboard | `dashboard` | A **Home** of live cards: Balance (with Send / Receive), Sync and peers, Sidechains and pending bundles, Blind merged mining (blocks of the last 24 that committed to each sidechain), Mining, Recent activity, Latest blocks; a row of quick actions above them. Each card's "Open →" opens its full page; a row of a card (a transaction, a peer, a sidechain, a block) opens the detail panel. The sidebar lists every page (Home first) with the theme, the text size and the **Menus** at its foot; a light bar above the page shows where you are and the palette. On a narrow window the sidebar is a drawer. |
| Compact sidebar | `sidebar` (default) | The window of chains-qt, refined: an icon rail with tooltips that shows the names on demand (the toggle at its foot, kept in `prefs.sidebarExpanded`); no menu bar — its items are in the pages, the rail's **Menus** button and the palette; denser pages and tables (29 px rows); the status bar. |

Whatever the layout:

- **Command palette** — Ctrl+K (Cmd+K), or the search field / icon: every page and
  every item of the old menu bar, found by name (words in any order, also by
  keywords: "history", "password", "zoom"…), with their shortcuts. Up/Down choose,
  Enter runs, Escape closes. It also holds the layouts, the units and the network
  switch of the status bar.
- **Keyboard shortcuts** of chains-qt: Alt+1…5 pages, Ctrl+Shift+D node window,
  Ctrl+Shift+C console, Ctrl+I / Ctrl+T / Ctrl+N / Ctrl+P the node window tabs,
  Ctrl+Shift+M mask values, Ctrl+M minimize, Ctrl+Q quit, Ctrl+ / Ctrl− text size.
- All the looks (Light, Dark, Ocean Teal, Matrix Green, classic Drivechain, and the
  desktop's) — the layouts use only the colour variables of styles/themes.css.
- **Narrow windows** (phone width, ≤ 760 px): the workspace areas become a row of
  icons under the top bar and the sub-tabs scroll; the dashboard sidebar becomes a
  drawer (☰ in the top bar); the compact rail stays icons only; the detail panel
  covers the page (≤ 900 px); the palette fills the width.
- **Accessibility**: a "Skip to the page" link first; areas, sub-tabs and sidebars are
  `nav` landmarks with `aria-current`; arrows move between workspace areas; the
  palette is a modal `dialog` with a combobox and a listbox (`aria-activedescendant`);
  the Menus buttons are `menu`s with arrow keys, Home/End and Escape; icon-only
  buttons have names; the detail panel is a labelled `complementary` region, closed
  with Escape.

## Where things are in the code

| File | What |
|------|------|
| src/webui/app/src/lib/layout.ts | The layouts, the areas and sub-tabs of the workspace, the sections of the sidebars |
| src/webui/app/src/lib/commands.svelte.ts | Every command: the pages and the menu items of chains-qt (ids, groups as the old menus, shortcuts, when disabled), the palette's search, `setLayout` |
| src/webui/app/src/lib/detail.svelte.ts | The detail panel: `showDetail({ kind: 'tx' \| 'block' \| 'peer' \| 'sidechain', … })`, `detail.follow` |
| src/webui/app/src/views/Shell.svelte | Picks the layout; the palette and the transaction notices for all |
| src/webui/app/src/views/layouts/Workspace.svelte, Dashboard.svelte, Compact.svelte | The three frames |
| src/webui/app/src/views/home/ | The dashboard's Home and its cards |
| src/webui/app/src/views/PageView.svelte | The page shown, whatever the layout |
| src/webui/app/src/components/CommandPalette.svelte, MenuButton.svelte, DetailPanel.svelte, Sidebar.svelte, LayoutPicker.svelte | Shared pieces |

For the pages: a page with a selectable list publishes its selection with
`showDetail(…)` when `detail.follow` is on (the workspace layout), and keeps its
dialog otherwise — pages/Transactions.svelte does so (a click selects and shows
the transaction in the panel; the context menu's "Show transaction details" keeps
the dialog). The node window's own tabs are hidden in the workspace, whose Node
sub-tabs are those tabs. A new menu action goes in `buildCommands()` only: the
palette, the Menus buttons, the tests and this table follow from it.

## Every item of the old menu bar

Every item is also in the **Menus** button of each layout (the workspace's top
bar, the foot of each sidebar), under the name of its old menu, and in the
**palette** (Ctrl+K) under the same name. The table says where else it lives.
`test/layouts.test.ts` checks that each item below is a command of its menu.

| Old menu | Item | Command id | Workspace tabs | Dashboard | Compact sidebar |
|---|---|---|---|---|---|
| Your wallet | Create wallet… | `wallet.create` | Wallet ▸ Wallets (Create) | Sidebar Wallets; Home: Balance card "Wallets" | Rail Wallets |
| Your wallet | Open wallet… | `wallet.open` | Wallet ▸ Wallets (Open) | Sidebar Wallets | Rail Wallets |
| Your wallet | Close wallet… | `wallet.close` | Wallet ▸ Wallets (Close) | Sidebar Wallets | Rail Wallets |
| Your wallet | Close all wallets… | `wallet.closeAll` | Wallet ▸ Wallets (Close all) | Sidebar Wallets | Rail Wallets |
| Your wallet | Migrate wallet… | `wallet.migrate` | Wallet ▸ Wallets (Migrate) | Sidebar Wallets | Rail Wallets |
| Your wallet | Restore and migrate wallet file… | `wallet.restoreMigrate` | Wallet ▸ Wallets | Sidebar Wallets | Rail Wallets |
| Your wallet | Sending addresses | `wallet.sendingAddresses` | Wallet ▸ Addresses (Sending) | Sidebar Addresses | Rail Addresses |
| Your wallet | Receiving addresses | `wallet.receivingAddresses` | Wallet ▸ Addresses (Receiving) | Sidebar Addresses | Rail Addresses |
| Your wallet | Backup wallet… | `wallet.backup` | Wallet ▸ Wallets (Backup) | Sidebar Wallets | Rail Wallets |
| Your wallet | Restore wallet… | `wallet.restore` | Wallet ▸ Wallets (Restore) | Sidebar Wallets | Rail Wallets |
| Your wallet | Export watch-only wallet… | `wallet.exportWatchOnly` | Wallet ▸ Wallets | Sidebar Wallets | Rail Wallets |
| Your wallet | Encrypt wallet… | `wallet.encrypt` | Wallet ▸ Wallets (Encrypt) | Sidebar Wallets | Rail Wallets |
| Your wallet | Change passphrase… | `wallet.passphrase` | Wallet ▸ Wallets | Sidebar Wallets | Rail Wallets |
| Your wallet | Unlock wallet for a while… | `wallet.unlock` | Wallet ▸ Wallets | Sidebar Wallets | Rail Wallets |
| Your wallet | Mask values (Ctrl+Shift+M) | `wallet.mask` | Wallet ▸ Overview button; Settings ▸ Appearance | Overview button; Settings ▸ Appearance | Overview button; Settings ▸ Appearance |
| Banking | Send (Alt+2) | `banking.send` | Wallet ▸ Send | Sidebar Send; Home: quick action, Balance card | Rail Send |
| Banking | Receive (Alt+3) | `banking.receive` | Wallet ▸ Receive | Sidebar Receive; Home: quick action, Balance card | Rail Receive |
| Banking | Transactions (Alt+4) | `banking.transactions` | Wallet ▸ History | Sidebar Transactions; Home: Recent activity card | Rail Transactions |
| Banking | Open URI… | `banking.openUri` | Wallet ▸ Send ("Open URI…") | Send page button | Send page button |
| Banking | Proof of funds | `banking.proof` | Wallet ▸ Proof of funds | Sidebar Proof of funds | Rail Proof of funds |
| Banking | Multisig lounge | `banking.multisig` | Wallet ▸ Multisig | Sidebar Multisig lounge | Rail Multisig lounge |
| Banking | Load PSBT from file… | `banking.psbtFile` | Wallet ▸ PSBT (From file) | Sidebar PSBT; Home: "Load PSBT" | Rail PSBT |
| Banking | Load PSBT from clipboard… | `banking.psbtClipboard` | Wallet ▸ PSBT (From clipboard) | Sidebar PSBT; Home: "Load PSBT" | Rail PSBT |
| Use Chains | Timestamp file | `use.timestamp` | Chain ▸ Timestamp | Sidebar Timestamp a file; Mining card | Rail Timestamp a file |
| Use Chains | Sign message… | `use.sign` | Wallet ▸ Messages (Sign) | Sidebar Sign and verify; Home: "Sign message" | Rail Sign and verify |
| Use Chains | Verify message… | `use.verify` | Wallet ▸ Messages (Verify) | Sidebar Sign and verify | Rail Sign and verify |
| Use Chains | Sidechains (Alt+5) | `use.sidechains` | Sidechains ▸ Sidechains | Sidebar Sidechains; Sidechains card; Home: "Deposit" | Rail Sidechains |
| Chains | Sidechain nodes | `chains.nodes` | Sidechains ▸ Sidechain nodes | Sidebar Sidechain nodes; BMM card | Rail Sidechain nodes |
| Chains | Solo mine | `chains.mine` | Chain ▸ Mine | Sidebar Mine; Mining card; Home: "Mine" | Rail Mine |
| Chains | Sidechain proposals and votes | `chains.proposals` | Sidechains ▸ Sidechains (Proposals tab); sidechain panel "Vote on bundles" | Sidechains card "Proposals and votes" | Sidechains page (Proposals tab) |
| Crypto tools | Block explorer | `tools.explorer` | Chain ▸ Explorer | Sidebar Block explorer; Latest blocks card; Home quick action | Rail Block explorer; Overview "Block explorer" |
| Crypto tools | Hash calculator | `tools.hash` | Chain ▸ Crypto tools (Hash) | Sidebar Crypto tools | Rail Crypto tools |
| Crypto tools | Merkle tree | `tools.merkle` | Chain ▸ Crypto tools (Merkle) | Sidebar Crypto tools | Rail Crypto tools |
| Crypto tools | Address decoder | `tools.address` | Chain ▸ Crypto tools (Address) | Sidebar Crypto tools | Rail Crypto tools |
| Window | Minimize (Ctrl+M) | `window.minimize` | Menus; Ctrl+M | Menus; Ctrl+M | Menus; Ctrl+M |
| Window | Information (Ctrl+I) | `window.info` | Node ▸ Info | Sidebar Node window; Sync card "Open" | Rail Node window |
| Window | Console (Ctrl+T) | `window.console` | Node ▸ Console | Sync card "Console"; Node window tab | Node window tab |
| Window | Network traffic (Ctrl+N) | `window.traffic` | Node ▸ Traffic | Node window tab | Node window tab |
| Window | Peers (Ctrl+P) | `window.peers` | Node ▸ Peers; status bar peers menu | Sync card "Peers" (and a peer row → panel); status bar | Node window tab; status bar peers menu |
| This node | Node window (Ctrl+Shift+D) | `node.window` | Node area | Sidebar Node window | Rail Node window |
| This node | Options… | `node.options` | Node ▸ Settings | Sidebar Settings | Rail Settings |
| This node | Theme ▸ (each look) | `node.theme.*` | Node ▸ Settings ▸ Appearance; Menus | Sidebar theme selector; Settings | Rail theme button (sun); Settings |
| This node | Theme ▸ Custom… | `node.themeCustom` | Node ▸ Settings ▸ Appearance | Settings ▸ Appearance | Settings ▸ Appearance |
| This node | Increase font size (Ctrl++) | `node.fontUp` | Settings ▸ Appearance; Ctrl++ | Sidebar A+; Ctrl++ | Rail A+; Ctrl++ |
| This node | Decrease font size (Ctrl+−) | `node.fontDown` | Settings ▸ Appearance; Ctrl+− | Sidebar A−; Ctrl+− | Rail A−; Ctrl+− |
| This node | Command-line options | `node.help` | Menus | Sidebar Menus | Rail Menus |
| This node | About Chains | `node.about` | Menus | Sidebar Menus | Rail Menus |
| This node | Interface kit (tests, -webuidevtools) | `node.kit` | Node ▸ Interface kit | Sidebar Interface kit | Rail Interface kit |
| This node | Exit (Ctrl+Q) | `node.exit` | Menus; Ctrl+Q | Sidebar Menus; Ctrl+Q | Rail Menus; Ctrl+Q |


The status bar of chains-qt (sync, peers with "Show peers" / "Enable/disable
network activity", the unit, the wallet's lock) is the same in every layout; its
actions are in the palette too ("Show peers", "Disable network activity", "Show
amounts in …").

## Tests and pictures

- `npm test` (src/webui/app): test/layouts.test.ts — every old menu item is a
  command of its menu, every page is in each layout, the search, the layout switch
  and its preference, Ctrl+K, and each layout rendered: workspace areas, sub-tabs,
  Menus, a transaction in the detail panel; dashboard cards opening pages and the
  panel; the compact rail and its names; the palette running a command.
- `src/webui/test/e2e/run_layouts.py --app build/bin/chains-app --tmp <dir> --shots <dir>`:
  a wallet with a history and two sidechains on regtest, then each layout in the
  light and dark themes — home, send, the history with the detail panel (or its
  dialog), the sidechains, the settings, the palette — plus the Menus, the
  expanded rail and the dashboard in the other looks.
