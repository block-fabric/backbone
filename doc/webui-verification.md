# chains-app: independent verification (branch webui-windows)

Checked on 2026-10-14, at `e0d9800895` (after the merge of `webui` and the three security fixes),
with template `4c516d7e49` and chains `6ec9f92df7`. Everything was rebuilt incrementally first.
This round changed no code: only this report.

## Results

### Test suites

| Suite | Result |
|-------|--------|
| vitest (src/webui/app) | PASS: 166/166, 21 files |
| svelte-check `--fail-on-warnings` | PASS: 448 files, 0 errors, 0 warnings |
| test_chains-app (Linux) | PASS, but **flaky**: 1 failure in 9 full runs (see "Failed") |
| page_rpc_policy alone | PASS 25/25 |
| e2e smoke / wallet / node | PASS: 20, 36 and 69 checks |
| e2e audit (7 runs) | PASS: audit 24, restart 2, nowallet 15, narrow700 22, narrow480 22, signer 7, lang 4 |
| e2e layouts / titlebar / refresh | PASS: 3 (and 53 pictures), 21, 4 |
| Windows `run_windows.py` (WSL interop) | PASS: test_chains-app.exe, smoke 20 (URI handed over a named pipe), layouts 3, titlebar 21, refresh 4. Ran in `%TEMP%\chains-app-test` with `-webuiregisteruri=0`. The folder was removed afterwards and the demo folder was not touched |
| chains: ctest (test_bitcoin, secp256k1) | PASS: 379/379 (script_assets_tests skipped, as usual) |
| chains: test_bitcoin-qt, offscreen | PASS: every QTest class |
| template + 6 sidechains: test_bitcoin (ctest) | PASS: 381 / 384 / 383 / 383 / 384 / 383 / 382 |
| template + 6 sidechains: feature_sidechain* | PASS: all 7 tests in each (8 in bitassets, which adds payouts), with mainchain `chains/build/bin` |

### The user's decisions

| Decision | Test | Picture | Verdict |
|----------|------|---------|---------|
| Dashboard is the default layout | layouts.test.ts; refresh.js "the dashboard is the layout of a first start" | refresh/dashboard-home-midnight; on Windows, window-frame.png | PASS |
| Midnight is the default theme | contrast.test.ts; refresh.js "Midnight is the look of a first start" | same | PASS |
| Chain before tools | layouts.test.ts (menu groups, sidebar sections, workspace areas) | the sidebar and the Menus button | **PARTLY**: see "Failed" 2 |
| Integrated title bar with minimize, maximize and close | titlebar.js (21 checks, Linux and Windows) | titlebar-*, window-frame.png (Windows) | PASS |
| In Compact, the logo grows the sidebar | layouts.test.ts "compact: the logo grows the sidebar, and shrinks it again" | layouts/sidebar-expanded-light | PASS |
| All functions of chains-qt kept | parity list: 118/120 `[x]`; A15 (macOS) `[ ]`; B09 `[~]` (macOS) | | PASS, apart from macOS |
| No credits to the inspiration | nothing under src/webui, doc/ or README names it; the About box and doc/webui.md list only the licences (webview, Svelte, QDarkStyleSheet, fonts, translators) | | PASS |

### Parity sample (30 items picked at random with seed 20261014, checked against src/qt)

A08 B02 B05 C03 C07 C12 C13 C15 D02 D12 E01 E02 E06 E10 E12 F02 G03 H01 I03 I06 J01 K02
L04 L06 L07 L09 M02 M03 N05 T03.

- 30 of 30 do what chains-qt does. Checks included: NetworkStyle hues 70/30, 35/15 and 160/30
  on the window and tray icons (GTK and Win32); the `chains://` message; every item of the Qt
  "Your Wallet" menu; history disabled while values are masked; the HD and lock icons; the
  create-wallet options; the context menu of the transactions; the console's list of sensitive
  commands; the traffic ranges from 5 minutes to 24 hours; font sizes from -6 to +12.
- **Wrong in the document, not in the code:**
  - **L04** says downloads are "https/http only", but the code now takes https only.
  - **D12** cites `Send.svelte` for an out-of-sync warning. chains-qt shows that warning on the
    overview only, and the send page shows the fee-estimation warning, as Qt does.
  - **N05** still mentions "the themed frame (C16)". It now means the title-bar option under
    Appearance.

### Audit items (doc/webui-audit.md)

All 22 findings are listed as closed: C1, H1–H2, M1–M7 and L1–L12. Every test the closure table
names exists. The e2e audit runs cover C1, H1, H2, M1, M4, M5, M6, L3, L6 and L11, and all pass.
Spot checks: `Planned.svelte` is gone (L1), `node.setSetting` is gone (M3), and `-webuidevtools`
and `-webuitestscript` are taken from the command line only (L11).

Two parts of the document are out of date:
- **M4** still says "menus fold into More". The merge removed that; on narrow windows the menus
  are now in the sidebar drawer. The narrow-window e2e runs still pass.
- **The last paragraph** says the Windows window "is not built nor tested yet". It is now built
  and tested, and A14 is `[x]`.

## Security findings: attempts to break them

### 1. RPC password on the command line: CLOSED

- chains-app and chains-qt pass only `-mainchainrpcpasswordfile=<sidechain datadir>/mainchain.rpcpass`.
  No caller passes `-mainchainrpcpassword=` any more (grep over chains-app and chains src/qt).
- `WriteSecretFile` creates the file with `O_EXCL|O_NOFOLLOW` and mode 0600, fchmods it, fsyncs
  it and renames it over the old one. Where the file goes is safe: the stem is limited to
  `[A-Za-z0-9_-]`, so `../evil` is refused, and the file name is fixed.
- On the reading side (template and all six sidechains), the file must be a regular file, owned
  by this user, with no group or other bits, of at most 4 KB, with a non-empty first line. Setting
  both the password and the file is refused, and so is the file without `-mainchainrpcuser`
  (feature_sidechain_record passes in all seven repos).
- Left open, as documented: no ACL check on Windows, where the file is written with default ACLs.
  Minor: `open()` without `O_NONBLOCK`, so a FIFO given by the user blocks startup.

### 2. Downloads and the signer: CLOSED for what was reported, with one weakness left

- An address that does not start with https, a hash that is not 64 hex characters, a URL with
  quotes or whitespace, a declined dialog, or a wrong hash: each one stops before curl or tar
  runs. curl also gets `--proto =https --proto-redir =https`.
- The `test.confirmAnswer` and `test.fileAnswer` hooks exist only with `-webuitestscript`, which
  is taken from the command line only. The page cannot answer the dialog.
- The page can no longer set `-signer` (`options.set` with a value is refused; only clearing it
  works). The `*notify` options are not settable. The options journal (Cancel) is kept outside
  the page's `prefs.set` area, so a forged undo entry cannot restore a signer. No RPC in the page
  policy can run a program.
- **Weakness left (LOW–MEDIUM):** the page can make the confirmation dialog misleading.
  `sidechainNodes.add` accepts any `name`: it is only trimmed, with no length limit and newlines
  allowed. That name goes unchanged into the title and the first line of the native dialog
  ("Download and install programs of <name>?"). A hostile page can add a sidechain whose name
  holds fake "From:/SHA-256:" lines and many blank lines, which push the real address and the
  line "an address typed in the app" out of view.
  - **Fix:** limit names to one line of about 64 printable characters, and put the real URL
    first in the dialog.

### 3. Translator: CLOSED

- Nothing scans the page: no MutationObserver or TreeWalker in i18n; the only MutationObserver
  left is the sync overlay's check for an open dialog. No `{@html}`, `innerHTML` or `eval` in
  the page.
- Speed: no regular expression matching any more. A probe with the real French catalog:
  100 × 10 KB texts took 2.9 ms, and 1000 × 300-character texts made of " and " repeats took
  17 ms.
- Chain, peer and user text goes through SafeText (`translate="no"`), and i18n-lint guards it.
- **Leftover (LOW, not a vulnerability):** `tx()` still runs on messages the app builds, such as
  toasts, dialogs, the fatal view, and PSBT/sign statuses. Those messages can carry outside text
  (host errors, curl stderr, RPC messages). Generic catalog patterns then rewrite them partly. In
  French:
  - "%1 and %2" turns "The name of the sidechain and of its programs are needed." into
    "… sidechain et of its programs …";
  - "%1 behind" turns "3 hours behind" into "en retard de 3 hours".

  The values are spliced in unchanged, so nothing can be spoofed, but the wording is broken.
  - **Fix:** drop patterns whose literal parts are under ~6 characters from `tx()`, or translate
    with `t()` at the place the message is built.

## Failed

1. **The bridge tests are flaky.** One full test_chains-app run in nine failed with
   `webui_bridge_tests/cancel: bridge.Pending() == 0 [1 != 0]` (bridge_tests.cpp:317). Cause:
   in `Bridge::Handle`, a worker calls `Reply()` before `Finish()` removes the request. A test
   that reads `Pending()` as soon as it has the reply can still see the request. This is the
   same race as the earlier one-off `page_rpc_policy` "Pending" failure. Only tests read
   `Pending()`, so the app is not affected.
   - **Fix:** in the tests, wait until `Pending()` reaches 0. Alternatively, finish before
     replying.
2. **Chain before tools is not followed on the Home quick actions** (views/home/Home.svelte:203–209).
   The order is Send, Receive, **Sign message, Load PSBT**, Deposit to a sidechain, Block
   explorer, Mine. Sign message and PSBT are tools: they belong to the Tools sidebar section, and
   Sign message is in the Tools menu. No test checks this row.

## Missing

- macOS: A15, and the macOS part of B09; the window keeps the desktop's frame. None of it is
  built or tested.
- No automated test covers the order of the Home quick actions, or a sidechain name holding
  several lines in the download dialog.
- The built-in release list is empty, so only addresses the user types can be downloaded.
- The "Use the title bar of the desktop" option applies at once, not on Apply (known, deferred).
- Document fixes are needed: parity L04, D12 and N05; audit M4 and the closing paragraph.

## Where things are

Logs and pictures of this round are in the session scratchpad, under `verify/`: `e2e_*.log`,
`t_*` and `shots/{smoke,wallet,node,audit,layouts,titlebar,refresh,windows}`.
