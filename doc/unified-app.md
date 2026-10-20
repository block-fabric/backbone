# The unified Chains app

One app for Chains and its sidechains. Status: **phase 1 (supervisor, switcher, wizards) and phase 2
(the sidechains' own pages in the app, spends through the app's trusted dialog)** on branch
`unified-app`; **sidechains as modules of one shell (§14) and their programs downloaded from GitHub releases (§15)**
on branch `unified-modules`, not merged. Written 2026-10-20; phase 2 in §13; ideas taken from BitWindow in §16.

## 0. What the user decided (2026-10-20)

- Built **into the Chains app** (`chains-app`, in-process `chainsd` + HTML UI in `src/webui`), not a separate app.
- **Chains standalone by default.** Sidechains are opt-in in *Settings → Sidechains*: enable / disable.
  Disabling stops the daemon and keeps data and keys; deleting data is a separate action.
  FastBlocks and Layers get an optional "stake on this chain".
- **Wallets:** default = **one seed for everything** (one backup phrase; each chain derives its keys
  from it on its own path). Optional per chain = a separate wallet.
- **Data directories:** default = one place, `<Chains datadir>/sidechains/<name>/`. Optional per chain =
  a custom folder (another disk), changeable later with a guided move.
- **Onboarding wizards** where useful: first run, enable a sidechain, move coins into a sidechain.

Everything below serves those decisions. Points that still need the user are collected in §12.

---

## 1. Architecture

```
┌──────────────────────────── chains-app (one process) ─────────────────────────────┐
│  window (WebKitGTK / WebView2)  ── bridge ──►  host methods (C++)                  │
│      HTML/Svelte UI                              │                                 │
│      ├─ Chains pages ──── rpc ────────────────► in-process chainsd (interfaces::Node)
│      └─ sidechain pages ─ sidechains.* ──┐       │   RPC server: OFF unless a       │
│                                          │       │   sidechain is enabled (§1.4)    │
│                                          ▼       ▼                                 │
│                              SidechainSupervisor (webui/supervisor.cpp)            │
│                              one thread "webui-sidechains", started on first enable│
└──────────────┬───────────────────────────┬────────────────────────────────────────┘
      fork/exec│ (own process group,       │ JSON-RPC 127.0.0.1 (cookie of the daemon)
      PDEATHSIG│  nice +5, no shell)       │
               ▼                           ▼
         layersd  -datadir=… -mainchainrpcconnect=127.0.0.1 -mainchainrpcport=<chains>
         fastblocksd …                       -mainchainrpcuser=chains-app-sidechains
         …                                   -mainchainrpcpasswordfile=<0600 file>
```

Each sidechain stays **its own daemon** (`<stem>d`, unchanged code, its own consensus, its own
datadir and wallet). The app *supervises* them; it never links sidechain consensus code. This keeps
the sidechain repos independent, lets a sidechain crash without taking Chains down, and lets a
daemon be upgraded on its own (§8).

### 1.1 Process supervision

- **One supervisor thread** does every `fork`/`exec`. A child gets its own process group (a Ctrl+C
  in a terminal does not reach it), `PR_SET_PDEATHSIG=SIGTERM` (Linux: if the app dies, the daemon
  shuts down cleanly instead of being orphaned — the death signal is tied to the *thread* that forked,
  hence the single long-lived thread), an empty signal mask, `nice +5`, stdin `/dev/null`, stdout and
  stderr appended to `<home>/supervisor.log`, every other fd closed. `exec` failure is reported through
  a close-on-exec pipe.
- **States:** `disabled → stopped → starting → running → stopping`, plus `crashed` and `failed`.
  `running` means the daemon answered `getblockcount` over RPC (warm-up done).
- **Restart policy:** a daemon that exits without being asked is restarted after 2 s, 5 s, 15 s,
  30 s, 60 s; after the fifth failure it is `failed` (shown in red with the end of its log) and
  stays down until the user turns it on again. Ten minutes of good running resets the count.
- **Stop:** `SIGTERM` (bitcoind-style clean shutdown: flush, wallet close), `SIGKILL` after
  `stop_timeout` (120 s). Disabling returns at once; the UI shows `stopping` until the exit is reaped.
- **Windows (phase 2):** `CreateProcess` into a Job object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`
  (the PDEATHSIG equivalent), stop via the daemon's `stop` RPC then `TerminateProcess` after the
  timeout. **macOS:** no PDEATHSIG; the daemon gets an inherited pipe and a new `-exitwithparentfd=<n>`
  option (shut down on EOF) — a small change in sidechain-template, merged down to every sidechain.

### 1.2 Ports

Every sidechain already has its own default ports: RPC `<net>9<code>54`, P2P `<net>9<code>55`
(`net`: main 0, testnet 1, regtest 2, signet 3; `code`: FastBlocks 1, Layers 2, BitAssets 3, zSide 4,
template 6, Thunder 7, Hivemind 8, BitNames 9). The supervisor uses them, checks both are free before
starting (a clear "port in use: another Layers node runs?" instead of a crash loop), and takes a
per-chain override from the settings (`rpcPort`, `port`). Tests use `-sidechainportbase=<p>`
(RPC `p+2i`, P2P `p+2i+1`).

### 1.3 Authentication

- **Daemon → Chains:** the app adds an `-rpcauth` entry for user `chains-app-sidechains` with a
  **fresh random password every run** (HMAC-hashed in the args, as `share/rpcauth` does). The password
  is written to `<datadir of the sidechain>/mainchain.rpcpass` (owner-only, atomic, via
  `WriteSecretFile`) and passed as `-mainchainrpcpasswordfile` — never on a command line (any local
  user can read those). The user's own `-rpcauth`/cookie keep working.
- **Phase 2 hardening (done, §13.5):** `-rpcwhitelist=chains-app-sidechains:<the methods sidechains use>` (BMM
  requests, `getsidechainevents`, block/header reads, deposit lookups) so a compromised sidechain
  daemon cannot call `dumpwallet`/`sendtoaddress` on the Chains wallet. Needs the exact list from the
  template's `MainchainClient` (+ `-mainchainrpcwallet` for BMM).
- **App → daemon:** phase 1 read the daemon's cookie. Phase 2: the app is a user of its own at each
  daemon, `chains-app` (an `-rpcauth` of this run, the password in memory only), limited by
  `-rpcwhitelist` to reads, the wallet setup and the host of the pages over RPC; `-rpcwhitelistdefault=0`
  keeps the cookie the user's, unrestricted (§13.5).

### 1.4 The RPC server of Chains starts late, and only when needed

A Chains-only user has no RPC server today (`-server` off). Sidechains need one. New:
`interfaces::Node::startRpcServer()` → `AppStartServersLate()` (init.cpp) runs `AppInitServers`
after `AppInitMain` — once, only when the first sidechain is enabled (or at start if one is). It
binds to localhost unless the user configured otherwise; shutdown already stops the HTTP/RPC
servers unconditionally. **Standalone users are unaffected:** no server, no supervisor thread, no
child process, no extra memory (verified by `run_unified.py`, §11).

### 1.5 Auto-wiring (exact arguments)

```
<stem>d -datadir=<dir> [-testnet|-signet|-regtest -sidechainslot=<slot>]
        -server=1 -rpcport=<R> -port=<P> -printtoconsole=0
        -mainchainrpcconnect=127.0.0.1 -mainchainrpcport=<Chains RPC port>
        -mainchainrpcuser=chains-app-sidechains -mainchainrpcpasswordfile=<dir>/mainchain.rpcpass
        -dbcache=100 -maxmempool=100   [+ -sidechainarg values, tests only]
```

### 1.6 Health, logs, resources

- **Health:** process alive + RPC answers (`running`); *in step with Chains* = the daemon's
  `getmainchaininfo` is connected and has seen Chains' tip height. Phase 2 adds liveness (RPC timing
  out for > 60 s → restart) and stall detection (Chains advanced 3 blocks, the sidechain's mainchain
  view did not → warning with its error).
- **Logs:** the daemon's own `debug.log` in its datadir; `supervisor.log` (start lines with the full
  argv, anything the daemon printed before its log opened, crash output). "Show its log" in the UI
  shows both tails. Phase 2: cap `supervisor.log` at 1 MB (rotate once).
- **Resources:** modest caches (`-dbcache=100 -maxmempool=100`), `nice +5`. Phase 2: per-chain
  prune (≥ 10 GB, the network minimum the user chose), a cgroup v2 / Job-object memory cap per daemon,
  "pause sidechains on battery".

### 1.7 Shutdown order

Quit → the page shows "shutting down" → **all sidechain daemons get `SIGTERM` at once** → the app
waits for all of them (≤ 120 s, then `SIGKILL`) → **then** the Chains node shuts down. Sidechains go
first because they depend on Chains (BMM, deposits, the follower); stopping Chains first makes them
log errors and possibly start a withdrawal/BMM step against a disappearing node. Settings stay
"enabled" so the next start brings them back.

---

## 2. Per-chain RPC routing for the pages

(Phase 2 did this differently from the sketch below: the pages of a sidechain run in a frame and their
host is the sidechain's own daemon, over RPC, §13.1.)

- **Phase 1 (done):** host methods `sidechains.list / enable / disable / chooseDataDir / setDataDir /
  deleteData / overview / log / rpc`. `sidechains.rpc {stem, method, params, wallet?}` forwards only
  an **allowlist of read methods** (`getblockchaininfo`, `getmainchaininfo`, `getbalances`,
  `listtransactions`, …). Anything else is refused in the host, not in the page.
- **Phase 2:** the bridge gains a chain on RPC requests: `{kind: 'rpc', chain: 'layers', method,
  params}`. The host routes `chain = chains|absent` to the in-process node and any other chain to the
  supervisor's client for that daemon, under **that chain's RpcPolicy** — the same table each sidechain
  app already ships (`src/webui/rpcpolicy.cpp` in each repo), compiled into the Chains app per chain.
  Methods that move coins go through the trusted dialog (§6), never straight through.
- Events: the supervisor emits `sidechains {stem}` on every state change; phase 2 adds per-chain tip
  events (`tip@layers`) from a light poll (or a ZMQ/`-blocknotify` hook) so sidechain pages refresh like
  Chains pages do.

## 3. Bundling the sidechains' pages at build time

(Done in phase 2, §13.2, with a frame per pack instead of chunks of the app's own bundle.)

Each sidechain app has its pages in `src/webui/app/src/chain/` (+ `chain.json`). The unified app
builds them in **as lazy chunks**:

1. Each sidechain repo gets `npm run pack` → `dist/pack/<stem>/` (an ES module exporting its pages,
   its nav entries and its `chain.json`) + `pack.json`:
   `{stem, version, commit, kitApi, rpcPolicy, daemon: {min, max}}`.
2. Chains has `src/webui/sidechains.json` pinning each pack to a repo **commit** (not a branch). The
   CMake step (`npm_build.cmake`) fetches/builds the pinned packs (a shallow git fetch into the build
   dir; offline builds use a local mirror path) and vite emits them as dynamic `import()` chunks.
   A Chains-only user never loads them: no memory cost.
3. **Compatibility checks.** The UI kit lives in Chains (`src/webui`) and flows down to the
   sidechains (user decision 2026-10-14). Each pack declares the `kitApi` it was built against; the
   build fails on a mismatch, so a kit change forces the packs to be bumped together. At runtime the
   host compares the daemon's version (`getnetworkinfo.subversion`, later a `getuiinfo` RPC) with the
   pack's `daemon` range; outside it, the chain is shown **read-only with "update needed"**.
4. **Where daemon binaries come from.**
   - **Phase 1:** bundled — the release package puts `layersd`, `fastblocksd`, … next to
     `chains-app` (found via `/proc/self/exe`; dev builds use `-sidechainbindir`). Cost: ≈ 25–60 MB
     per daemon. Option: two packages, "Chains" and "Chains + sidechains" (§12).
   - **Phase 4 (#9):** download on enable from a **signed manifest**: a JSON per release train
     (`stem, platform, version, url, sha256, size, minApp`) signed with an ed25519 release key compiled
     into the app. The existing download path of `sidechainnodes.cpp` (https only, sha256 before
     unpacking, native confirm dialog) is reused, plus signature verification of the manifest.
     Binaries live in `<home>/bin/<version>/`; the previous version is kept for rollback.

## 4. Wallets

### 4.1 Single seed (default)

- **Seed:** BIP39, 12 words (24 allowed on restore), optional passphrase (advanced). Master key via
  BIP32.
- **One account per chain, hardened:** `m/86'/C'/A'` (taproot, the default) and `m/84'/C'/A'`
  (segwit v0, for chains or tools that need it). `C` = **Bitcoin Core's own coin type**: `0'` on the main
  network, `1'` on the test chains (testnet, signet, regtest), as Chains' `src/wallet/walletutil.cpp`
  already derives and as eCash's betanet does (§12.1): `m/86'/0'/A'` and `m/86'/1'/A'`. One number for the
  whole family. `A` = **0 for Chains**; for a sidechain, `A = u32(SHA256("chains/account" ‖ genesis
  hash of the sidechain)) & 0x7fffffff`. Deterministic, needs no registry, distinct per chain *and*
  per network (genesis differs), and a slot later reused by another sidechain gets other keys.
- **How a daemon gets its keys:** the hub derives the **account-level xprv** and imports
  `tr(<acct xprv>/0/*)` and `tr(<acct xprv>/1/*)` (+ `wpkh` equivalents) into a **blank descriptor
  wallet** `main` of the daemon (`createwallet main … blank=true`, `importdescriptors`). The daemon
  only ever holds its own account key: hardened derivation means a compromised Layers daemon cannot
  derive Chains' or FastBlocks' keys, nor the seed.
- **The same paths as Bitcoin:** a seed used in a Bitcoin wallet too derives the **same keys** for Chains'
  account 0 (`m/86'/0'/0'`, `m/84'/0'/0'`): the same addresses on both chains, linkable, and a key that
  leaks on one leaks on the other. The first-run wizard therefore offers a **new** backup phrase by
  default, and on restore says in plain words: "Use a phrase made for Chains, not one of a Bitcoin
  wallet: the same phrase gives the same keys on both" (sidechains' accounts `A ≠ 0` do not collide).- **xpub-only alternative:** daemon wallets watch-only, the hub signs PSBTs. Stronger isolation but
  every spend, every BMM bid and every staking signature must round-trip through the hub; staking
  (FastBlocks/Layers) signs continuously. Keep xprv-in-daemon as default; offer xpub-only later with
  hardware wallets (phase 5).
- **Chain-specific key material:** zSide's Orchard keys via ZIP-32 from the same BIP39 seed (its own
  coin type path); Hivemind's Votecoin keys are ordinary wallet keys of its account. FastBlocks/Layers
  **validator keys are not derived from the seed** (§12.4): each machine makes its own, kept apart from
  the wallet (`-validatorkeyfile`), never copied; a seed leak does not expose them, and one is rotated
  without touching coins.
- **Encryption:** the seed lives in the hub, in `<datadir>/seed.dat`, encrypted (scrypt → AES-256-GCM)
  with the wallet passphrase. Every daemon wallet is encrypted with the **same passphrase**
  (`encryptwallet` right after import). "Unlock" in the app unlocks the seed and, on demand, the one
  daemon wallet that needs it (`walletpassphrase` with the same timeout); a passphrase change is applied
  to the seed and every enabled chain, with rollback if one fails. Chains' own wallet is the account
  `A=0` wallet, made the same way.
- **Backup & restore:** the 12 words restore **everything**. Restore: the user types the words
  (+ optional "about when did you create it?" to bound rescans); the hub creates the Chains wallet and,
  for each sidechain enabled, the blank wallet + imported descriptors with `timestamp` = that date
  (or the sidechain's genesis — sidechains are young, a full rescan is cheap). Rescans run in the
  background per chain, shown per chain in the switcher ("restoring, 40 %").
- **Existing users:** existing Chains wallets stay as they are. The app offers "create your backup
  phrase" → a new seed wallet, and a guided "move your coins to it" (a normal send to self). Never an
  automatic sweep.

### 4.2 Separate wallet (per chain, optional)

The daemon creates its own wallet (what phase 1 does for every chain). The UI marks it
"separate — needs its own backup", offers *Back up this wallet* (`backupwallet` to a file the user
chooses) and reminds once a week until done. Switching separate → shared later = new seed wallet on
that chain + a guided send; never automatic.

## 5. Data directories

```
<Chains datadir>/                 Chains (blocks/, chainstate/, wallets/, regtest/ …)
  sidechains/
    layers/
      data/                       -datadir of layersd (its testnet/ regtest/ inside)
        mainchain.rpcpass         0600, rewritten at every start
      bin/                        phase 4 downloads (by version)
      supervisor.log
    fastblocks/ …
```

The `data/` level is shared with the existing *Sidechains → Nodes* tab, so a sidechain started either
way finds the same chain and wallet.

- **Custom folder per chain**, chosen in the desktop's own folder chooser (the page cannot type a
  path: `setDataDir` only takes the folder last chosen there, or "default"). Checks: absolute path; not
  inside Chains' data (except `sidechains/`); not inside or around another sidechain's data; the
  nearest existing parent is writable; free space vs the chain's estimate (red below, amber within
  10 GB).
- **Move wizard** (phase 1 has the engine; the full wizard is phase 2): turn off → choose folder →
  checks (above) → "Move what it downloaded (N GB)?" → rename on the same disk (instant), else copy
  with progress, verify (file count and sizes), then remove the old copy (or keep it, user's choice)
  → turn on. A failed copy leaves everything as it was.
- **Delete data**: only while off; asked in a **native** dialog. Default keeps `wallets/` and
  `settings.json` (blocks and state go: it resyncs); "everything, wallets too" is a separate, louder
  choice (phase 2 UI) that says coins whose keys are only there are lost.

## 6. Security of spends from the unified app

(Option B done in phase 2, §13.3; the security review in §13.5.)

**Phase 1 is read-only for sidechains**, enforced in the host (allowlist; tested). The pages can
watch, never spend.

Phase 2 must give sidechain spends the same guarantee Chains spends have today (`webui/confirm.cpp`:
the host decodes what a call does, asks in a native dialog the page can neither draw nor answer, then
runs exactly the call it described). Each sidechain app already has a describer table for its own
RPCs (its `confirm.cpp`: rows for destinations, amounts, fees, two-step reveals). Two ways to use them:

- **A. Shared describer library in the hub.** Each sidechain's describers compiled into the Chains app,
  with `Call::Read` bound to that daemon's RPC.
  + no daemon change. − The hub carries chain-specific semantics for 7+ chains and must be rebuilt for
  every RPC change of any of them; a hub/daemon version skew can *mis-describe* a call (the worst
  failure for a confirmation); breaks download-on-enable of a newer daemon.
- **B. Daemon "describe for confirmation" RPC + hub-side binding (recommended).** Each daemon gets
  `describeconfirmation <method> <canonical params>` → `{rows, digest}`, built from the describers it
  already has (moved from its webui into its RPC layer). The hub: canonicalizes the params itself,
  asks for the description, **verifies what it can independently** (generic tx decoding of the
  resulting PSBT/tx for amounts, standard destinations and fee where the chain uses Bitcoin-style
  transactions), shows the rows in the native dialog with the chain's name in fixed words, then runs
  **exactly the canonical array it had described** (bound by the digest; the page cannot swap it).
  + chain knowledge stays in the chain's code, versioned with the daemon; the hub stays generic;
  works with downloaded daemons. − one RPC per chain (mechanical: the describers exist).
  The daemon is trusted code we ship and it holds the keys anyway; the adversary the dialog defends
  against is a compromised **page**, which B fully covers.

**Recommendation: B**, with A's generic parts (rows, canonical params, two-step authorizations) kept
in the shared kit so both the standalone sidechain apps and the hub use the same code.

## 7. Upgrades

- **Release trains.** An app release ships (or its manifest names) daemon versions tested together.
  At start the hub reads each daemon's version; outside the pack's range → read-only + "update needed".
- **Hard forks.** The manifest carries `activation_height` per chain for a fork; before it the hub shows
  "update Layers before Chains block N (about 3 days)"; past it with an old daemon, the chain is
  stopped with a clear message (it would follow the wrong rules).
- **Data format changes** (e.g. a store layout bump that rebuilds state): flagged in the manifest so
  the hub warns "rebuilds its data on first start, about N minutes" instead of looking stuck.
- **App vs daemon order:** the app is updated first; it accepts the previous daemon version
  (`daemon.min`) so sidechains keep running while their new binaries download (phase 4).

## 8. Wizards (screens and copy)

Plain words, one decision per screen, "recommended" marked, every choice changeable later in
Settings. The prototype has the first two (screenshots in `media/unified-app/`).

**First run** (prototype: *Settings → Sidechains → Setup guide*; becomes the first start in phase 3
when the seed exists):
1. *Welcome* — "Chains is a coin with sidechains: other chains, each good at something, that your
   Chains coins can move into and back out of." What the setup does, how long.
2. *Your wallet* — Create a new wallet / Restore from a backup phrase.
3. *Backup phrase* — 12 words in a numbered grid; "Anyone with them can take your coins; without them,
   a lost computer means lost coins." Checkbox "I wrote the words down" gates Next. (Phase 3: then a
   3-word check.) / *Restore* — word box, live "9 words · 12 or 24 are needed".
4. *Network* — which network the app is on, in words ("the test network: coins for trying things out,
   worth nothing").
5. *Pick sidechains* — cards with a one-line purpose and "about 4 GB of disk and 400 MB of memory";
   running total; programs missing are greyed with "not on this computer".
6. *Storage* (only if any picked) — "All together, with Chains" (path shown, recommended) / "I will
   choose a folder for each" (→ Settings).
7. *What happens next* — Chains syncs first; sidechains follow; the switcher shows each; then "move
   coins into a sidechain". Finish turns the picked ones on with a per-chain progress list.
Edge cases: skip at any time (nothing changes); a sidechain fails to start → shown in the list with
its error, the others continue; restore with chains already enabled → rescans are queued per chain.

**Enable a sidechain** (prototype): *What it is* (purpose, what runs, resources, free-space check) →
*Where its data goes* (with Chains / a folder of my choice + chooser, move existing data) → *Its
wallet* (my backup phrase / separate) → *Staking* (Layers, FastBlocks; phase 1 shows it disabled:
"comes with sending on sidechains") → *Ready* (review) → *On* ("Its node is starting in the background…
Show it"). Edge cases: not enough room (Next disabled, red), folder refused (reason shown), program
missing (toggle explains), port in use (error from the supervisor, stays off).

**Turn off** — page question "Its node stops. Its data and its keys stay on this computer." **Delete
data** — native dialog (blocks and state; wallets kept). **Move data** — see §5.

**Move coins into a sidechain** (phase 2; the Chains app has deposits already): pick sidechain
(only enabled + synced ones, else "turn it on first") → amount (with Chains balance, fee) → "to your
Layers wallet" (the address comes from the daemon of that chain: no copy-paste) → native
confirmation → progress: "sent on Chains" → "seen by Layers after N blocks" (the deposit tracker of
the Sidechains page) → "available on Layers". Withdrawals back to Chains are explained as slow
(bundles, votes) with the expected time.

## 9. Phasing and effort

| Phase | Content | Effort |
|---|---|---|
| **1** (this branch) | Supervisor (Linux), late RPC server, Settings → Sidechains, chain switcher, read-only overview, wizard skeletons, tests | done (≈ 3 days) |
| **2** (this branch, done 2026-10-20) | The sidechains' own pages as packs pinned by commit, in isolated frames; their host is their daemon over RPC; every spend described by the daemon (`describeconfirmation`) and asked in the app's trusted dialog, bound by a one-time token; rpcwhitelists both ways; deposits from the pages by Chains' wallet; the old Nodes tab removed (§13). Moved on: Windows supervisor and packs (phase 5), liveness/stall detection and the move-data wizard (phase 4) | done |
| **3** | Single seed: BIP39 + derivation (`m/86'/0'/A'`, `1'` on test chains) + `seed.dat`, blank-wallet import per chain, unified encryption/unlock, backup/restore with rescans, first-run wizard as the real first start, separate-wallet path, existing-user migration (offered once at first start, and in Settings), the deposit wizard | 2–3 weeks |
| **4** | Download-on-enable with signed manifests (#9), upgrade/fork banners, a BMM spending cap per sidechain, liveness/stall detection, move-data wizard with progress, end-to-end runs of every pack | 3 weeks |
| **5** | Windows (Job objects, WebView2 origins for the packs), macOS (`-exitwithparentfd`), hardware wallets / xpub-only mode, resource caps (cgroups/Job memory), battery pause | 2 weeks |

## 10. What the prototype does (phase 1)

- `src/webui/supervisor.{h,cpp}` — catalog (Layers, FastBlocks, Thunder, BitNames, BitAssets, zSide,
  Hivemind), settings per network in the app settings (`sidechains:<net>`), process supervision,
  auto-wiring, data dirs (default/custom/move/delete keeping wallets), read-only RPC, overview.
- `src/init.cpp` `AppStartServersLate`, `interfaces::Node::startRpcServer`.
- `src/webui/app.cpp` — `-rpcauth` for the sidechains user (fresh per run), supervisor made when the
  node is ready, enabled ones started, all stopped before the node at shutdown; host methods
  `sidechains.*`; options `-sidechainbindir`, `-sidechainportbase`, `-sidechainarg`.
- UI: `lib/unified.ts` (logic), `lib/sidechains.svelte.ts` (state), `components/ChainSwitcher`,
  `SidechainSettings`, `EnableWizard`, `FirstRunWizard`, `pages/SidechainView`.
- Not in phase 1: spending on sidechains, the single seed (screens only), sidechain pages, Windows/macOS
  supervision, downloads.

## 11. Tests

- `src/webui/test/supervisor_tests.cpp` — ports, auto-wiring args (no password on argv), rpcauth line,
  read-only allowlist, data dir checks/move/delete, standalone runs nothing; **with a real `layersd`**
  (`CHAINS_TEST_SIDECHAIN_BINDIR=…/layers/build_rel/bin`) wired to a real `chainsd`: start, connected to
  the mainchain, wallet made, overview, SIGKILL → restarted as another process, disable → clean
  shutdown, data moved to another folder and started from there, StopAll.
- `src/webui/app/src/test/unified.test.ts` (vitest) — words, switcher, wizard steps/plans, components.
- `src/webui/test/e2e/run_unified.py` + `unified.js` — the real app on regtest: standalone (no RPC
  port, no supervisor thread, no daemon) → Layers on through the wizard → daemon is a child of the app
  with the right arguments, Chains' RPC server now listens → the runner has Layers make 3 blocks by
  BMM (as a miner would) → switcher shows Layers in step with Chains, its blocks, keeps up with new blocks → spend refused, read allowed → first-run guide screens → off → daemon gone,
  "Shutdown done" in its log → app exits cleanly, nothing left running.

Run: `run_unified.py --app build_app/bin/chains-app --sidechain-bindir ~/skydoge/layers/build_rel/bin
[--sidechain-bindir ~/skydoge/fastblocks/build_rel/bin] --tmp … --shots …` (or ctest `webui_e2e_unified`
with `-DWEBUI_E2E_SIDECHAIN_BINDIR=…`). Pictures of the prototype (Midnight): `~/skydoge/media/unified-app/`.

Phase 2 adds:

- `src/webui/test/sidechainpages_tests.cpp` — routes; a question of the daemon shown with the chain named,
  then exactly the same request with the token; a no runs nothing; a wrong passphrase asked again; a refused
  token; a second question; files only chosen in the app's chooser; the look the app's. `assets_tests`:
  the packs' origins and policy. `supervisor_tests`: the whitelists; with a real `layersd`, the app's user
  is refused `sendtoaddress` and `dumpwallet` by the daemon.
- In the template and every sidechain: `remotehost_tests` (tokens once, other call, other question, other
  wallet, expiry; digest; events; chosen files), `rpc_describeconfirmation.py` (the kit's questions, sends,
  a locked wallet, the pages' policy, files, events) and `rpc_describeconfirmation_<chain>.py` (the chain's
  own questions; a call changed after its question refused). Functional tests take
  `TEST_RUNNER_PORT_MIN=49100 TEST_RUNNER_PORT_RANGE=200` to stay in the ports of this machine's tests.
- `src/webui/test/e2e/run_packs.py` + `packs.js` (ctest `webui_e2e_packs`, with
  `-DWEBUI_E2E_THUNDER_BINDIR`): Layers and Thunder on; Layers' pages in the app; a deposit into Layers
  (Chains' own question); a send, an asset transfer and a DEX swap on Layers, each in the trusted dialog
  "On Layers", confirmed in a block; a no sends nothing; an RPC spend, a request of the tests and quit
  refused to the pages; the frames isolated both ways; a client that changes a call after its question
  refused by the daemon (and the token not taken twice); Thunder's pages, a deposit and a send; the pages
  of Thunder cannot pay into Layers. Pictures (Midnight) in `~/skydoge/media/unified-app/phase2/`.

## 12. Decisions of the user (2026-10-20)

The seven questions of phase 1, all decided as recommended:

1. **Coin type `C`:** **no SLIP-44 registration** (changed by the user the same day: the first choice was
   to register one). Chains uses the coin type eCash's betanet uses (`ecash-com/bitcoin`, branch
   `betanet`, `src/wallet/walletutil.cpp`), Bitcoin Core's default, which Chains' own `walletutil.cpp`
   already has: `0'` on the main network, `1'` on testnet, signet and regtest. Seed paths
   `m/86'/0'/A'` (main) and `m/86'/1'/A'` (test chains), `A = 0` for Chains and per sidechain as in §4.1.
   Implication: a seed also used in a Bitcoin wallet derives the same account-0 keys on Chains (linkable
   addresses, a key leaked on one chain leaked on both): the first-run wizard advises a fresh phrase.
2. **Packages:** two, **"Chains"** (the app alone) and **"Chains + sidechains"** (with the daemons and
   the sidechains' page packs), until download-on-enable (phase 4) makes one package enough.
3. **Spend confirmations: option B.** Each daemon describes a call itself (`describeconfirmation`,
   built from its own `confirm.cpp` describers); the app shows that description in its own trusted
   dialog and then runs exactly the call it showed, bound by a one-time token the daemon checks.
   Done in phase 2: §13.
4. **Validator keys stay separate from the seed:** made on each machine, never derived from the seed nor
   copied; a seed leak does not expose them, and they are rotated without touching coins. There is no
   validator branch in the seed's paths (§4.1).
5. **The old *Sidechains → Nodes* tab goes** once the new page replaces it: removed in phase 2 (§13.4).
6. **Existing wallets:** the move to a seed wallet is offered **once, at the first start of the new
   version**, and stays available in Settings (phase 3).
7. **Testnet hub:** after phase 2 the hub machines run their sidechains through the unified app
   (separate daemons only where the app cannot, e.g. a machine without a desktop).

## 13. Phase 2: the sidechains' pages and their spends in the app (2026-10-20)

### 13.1 How it works

```
 chains-app page (chains-app://app)                         the daemon of Layers (layersd, remotehost.cpp)
  ├─ Chains' own pages ── bridge ── App (host) ──────────────►  webuihost {request} (token passphrase files)
  └─ <iframe chains-pack://layers/…>  (sandboxed)              │   kind rpc: the pages' own policy (rpcpolicy.cpp)
       Layers' own pages (its pack)                            │   kind host: wallet.*, rpc.confirmed, local.cpp …
       shim.js: __chains = postMessage(parent) ──► packs.svelte.ts (frame known by its window)
                                                    └─ pack.call {stem, request} ─► SidechainPages (sidechainpages.cpp)
                                                         LOCAL  app.hello/info/locale, prefs, window.*, file.choose, peg.deposit
                                                         HUB    clipboard, links, notices, files chosen in the app
                                                         REFUSE app.quit, options.*, intro.*, test.*, sidechains.*, pack.*
                                                         DAEMON everything else ──► webuihost; {confirm} → App::AskHost
                                                                (trusted dialog "On Layers" + its mark) → same request + token
  events: webuievents (long poll) ─► "pack.events" ─► postMessage to that frame only
```

- **The host of a sidechain's pages is its daemon** (`src/webui/remotehost.{h,cpp}` of the template, in
  `<stem>d` only): it runs the very host methods its own app runs (`WalletApi`, `ConfirmedRpc`, `NodeApi`,
  `SidechainApi`, the chain's `local::RegisterHost`), so every chain's pages, and their own describers,
  work unchanged. A daemon has no desktop: every question of those host methods (their `AskFn`/`ConfirmFn`)
  is carried out to the app instead of being asked.
- **`describeconfirmation "method" [params] (wallet)`**: the question of a confirmed RPC method, made by the
  chain's own describers, never run (`confirm::DescribeOnly`: a call that would run without a question is
  refused, no second step runs on an authorization). **`webuihost`**: one request of the pages; a question
  comes back as `{confirm: {title, message, accept, level, kind, rows, passphrase, token, digest, expiresIn}}`.
  **`webuievents`**: the node's events in order. **`webuichosenfile`**: a token for a file the user chose in
  the app's chooser.
- **The binding**: a token (32 random bytes) is bound to the request as text (kind, method, parameters,
  wallet), to the question (SHA-256 of the request and the question's JSON), to the wallet's identity (the
  SHA-256 of its public descriptors) and to 5 minutes; taken once whatever happens. The app sends exactly the
  request it described with the token and the passphrase typed in its dialog; the daemon makes the question
  again and runs only when all of it fits (`Bindings::Take`), else `-32802` with the reason.

### 13.2 The packs

- Each sidechain repo builds its pages as a pack: `npm run pack -- --outDir <dir>` (`app/scripts/pack.mjs`,
  from the template): the interface for `/packs/<stem>/`, and `pack.json {stem, name, hostApi, commit, brand}`.
- Chains pins each pack to a commit in `src/webui/sidechains.json`; `app/scripts/packs.mjs` (a step of
  CMake, `WEBUI_SIDECHAIN_PACKS`, default ON; OFF for the package "Chains") takes `src/webui/app` and
  `src/webui/locale` of that commit with `git archive` (never the work tree, never a branch) from
  `WEBUI_SIDECHAIN_REPOS` (default: next to Chains), builds it with Chains' own `node_modules`, and puts it
  in the interface with `/packs/shim.js` loaded before the pack's script and `/packs/index.json`.
- **Compatibility, at build:** the commit must exist; the pack's packages must be Chains' (the kit flows
  down: a kit change makes the packs be bumped together); its `hostApi` must be the app's (1).
  **At run time:** `pack.info` asks the daemon (`webuihost app.info` → `hostApi`); another version, or a
  daemon without the host over RPC: the chain shows read-only with "update needed".
- Size: about 5 MB a pack (its fonts and 90 languages); seven packs ≈ 35 MB in "Chains + sidechains".
- In the page: the chain chosen in the switcher shows a bar (its name, its state, "Its node": the phase-1
  overview) over the frame of its pages; until its node runs, or without a pack, the overview.

### 13.3 A spend, step by step (Layers, wallet.send)

1. The pages (in the frame) prepare it: `wallet.send.prepare` → the daemon makes the transaction, unsigned,
   kept under a token of the wallet functions.
2. `wallet.send.commit {token}` → the app sends it to `webuihost`; the daemon decodes the kept transaction
   (destinations, amounts, fee, change: `PaymentRows`), and answers `{confirm}` instead of asking.
3. The app shows it in its own trusted dialog (the themed dialog window, or the system's), with **"On
   Layers · Regtest"** and the chain's mark above the title, and a first row *Chain: Layers*; the
   passphrase of a locked wallet is typed there.
4. On a yes, the app sends the same request, the token and the passphrase; the daemon asks itself the same
   question again, checks the token, unlocks the wallet for this call alone, signs, sends. On a no, nothing
   more is sent. A wrong passphrase: the app asks again (3 times at most).
The same path serves every question of every chain: `rpc.confirmed` (assets, DEX, names, markets, votes,
shielded sends, withdrawals), the two-step reveals (`then`, authorizations kept in the daemon), bonds
(`fastblocks.bond`), BMM (`bmm.set`). A **deposit** from a sidechain's pages (`peg.deposit`) is made by the
app itself: Chains' own confirmed `createsidechaindeposit` from the wallet shown, Chains' own question, and
only to a deposit address of that sidechain's slot.

### 13.3b A sidechain that ran in its own app before

The default folder of a sidechain (`<Chains datadir>/sidechains/<stem>/data`) is the one its own app used
from the Sidechains page, so a user's existing data is **adopted as it is**: its chain (no reindex), its
wallets (a wallet loaded at its start is kept; else "main" if there is one, else its first wallet; a new
"main" only where it has none), its `<stem>.conf`, its validator key file and staking state (the app passes
none of them, and touches none). The app's own arguments take the place of the conf's for the ports, the
mainchain connection and `-server` only. If the folder is in use — its own app still runs (the lock of its
data folder is held by another program) — it is not started and the user reads "Layers is already running in
its own app (…): quit it first, then turn it on here" (`RunningElsewhere`, checked when turned on and before
each start). Not handled: a conf with `mainchainrpcpassword=` (the daemon refuses it with the app's password
file: remove that line); a sidechain whose data lives elsewhere (choose that folder in its settings).
Tested in `supervisor_tests` (`real_daemon_adopts_existing_data_and_sees_its_own_app`, on scratch data).

### 13.4 The old *Sidechains → Nodes* tab

Removed (decision 5): the page `SidechainNodes.svelte`, its tab, its command (now "Run sidechains" →
Settings → Sidechains, and a button "Run sidechains in this app" on the Sidechains page), its host
methods. Kept for later: `sidechainnodes.cpp` (its download of a release and its checks, for
download-on-enable, phase 4) and its unit tests. Not migrated (no longer offered): starting a custom
sidechain by hand, downloads from the app (phase 4).

### 13.5 Security review (phase 2)

| # | Threat | What holds | Test |
|---|---|---|---|
| 1 | **Replay** of an accepted call | The token is taken once (erased whatever the result); a used one is "not known" | remotehost_tests, rpc_describeconfirmation*.py, run_packs (tamper) |
| 2 | **Parameters changed** after the question (param smuggling, a page or a client) | The request as text is bound; the app rebuilds the request from its four fields and sends exactly the described one; the daemon refuses keys given twice and unknown fields; a type change (a number sent as a string) is a change | the same; per-chain tests change each first parameter |
| 3 | **TOCTOU**: what the call does changes between question and run | The daemon makes the question again at run time and compares its digest; a prepared send is kept (not made again), so its question is the same; anything else differs → refused "ask again". Residual: what changes between the yes and the run inside one call (as in the standalone apps) | remotehost_tests (question changed) |
| 4 | **Wallet switching** (another wallet loaded under the name) | The wallet's identity (its public descriptors) is bound | rpc_describeconfirmation.py ("another wallet…") |
| 5 | **Version drift** (pack vs daemon vs app) | `hostApi` checked at build (pack) and at run (daemon): else read-only, "update needed"; tokens live in the daemon's memory only (a restarted or upgraded daemon knows none) | vitest ("update needed") |
| 6 | **Expiry** | 5 minutes, from the question | remotehost_tests |
| 7 | **A second question in one request** (a console line of two spends: running it again would repeat the first) | Refused after a yes; a wrong passphrase is said, and the app asks anew | sidechainpages_tests |
| 8 | **Found:** a prepared send/bump was used up before its question, so a question carried out was a lost send | Fixed in the kit (`WalletApi`, `once`): used up when answered or failed, kept while carried out, one question per token | rpc_describeconfirmation.py |
| 9 | **Found by review (MEDIUM): files named by path** (`webuihost files` took paths: any caller could declare a path chosen) | Fixed: the app gets a one-time token per file right after its own chooser (`webuichosenfile`: absolute, canonical, no link, no `..`, 10 min, read or write); a request names tokens only; the app attaches tokens only for files its chooser gave that chain's pages | remotehost_tests, rpc_describeconfirmation.py, sidechainpages_tests |
| 10 | **Found by review (HIGH): packs on the app's origin** (an iframe of `chains-app://app/packs/…` could reach `window.parent.__chains` and every chain's calls) | Fixed: each pack is served under its own origin `chains-pack://<stem>/` (its files and the shim only, `frame-ancestors chains-app://app`, no network, no frames), in a sandboxed frame (`allow-scripts allow-same-origin` of its own origin: no pop-ups, no top navigation, no forms); it talks only by `postMessage`; the app takes a message only from a frame it registered, for that frame's chain (never a chain the message names), answers that frame alone; the app's origin no longer serves `/packs/`; the page offers frames no global | vitest (a frame naming another chain, a stranger window, testrpc), assets_tests (origins), run_packs (cross-origin: no reach in either way) |
| 11 | **The app's user at the daemon spends directly** (a bug of the app) | `-rpcwhitelist`: no spend method (`sendtoaddress`, `dumpwallet`… answer 403); spends only through `webuihost`, which asks | supervisor_tests with a real layersd |
| 12 | **A daemon spends Chains' wallet** | Chains' `-rpcwhitelist` for the daemons' user: the follower, bundles, BMM, the pages' reads; no deposit, send or dump; `-rpcwhitelistdefault=0` unless the user set whitelists (theirs stay as they were). **Residual:** `createbmmrequest` pays BMM bids from Chains' wallet in amounts the daemon chooses (after the user turned BMM on in that chain's pages); a cap per sidechain comes with phase 4 | supervisor_tests |
| 13 | **A deposit into another chain** from one chain's pages | The app takes only a deposit address of that chain's slot; Chains' own describer shows the slot's title | run_packs ("Thunder cannot pay into Layers") |
| 14 | **A token used by someone else** | Tokens bind, they do not authenticate: whoever has the daemon's cookie can spend anyway; the app's own password exists in its memory only | — |
| 15 | **The passphrase** | Typed in the app's dialog, sent to the daemon on 127.0.0.1 with the token, wiped from the app's memory after (as `walletpassphrase` would); the wallet is locked again after the call | rpc_describeconfirmation.py (locked wallet) |
| 16 | **Deception** in the question | The chain's name and mark come from the app (its catalog, its pack), not the daemon; the daemon's rows go through the dialog's own checks (bidi marks written out, images only PNG/JPEG checked) | dialog tests (IMAGE rows ported to Chains) |
| 18 | **Found by review (MEDIUM): too broad host methods for packs** (`app.open` of any kind, `app.quitGuard`, `tray.state`, help windows) | Least privilege: a pack gets its own chain's reads, the spend path through the trusted dialog, and harmless window things only: `app.open` of a URL only (the app asks first for one not its own; never its log or configuration), notices named after the chain, `file.read`/`file.write` only of a file chosen for that chain's pages; `app.quitGuard`, `tray.state`, `app.help`, `app.about` refused | sidechainpages_tests |
| 17 | **Found:** a crash of `<stem>d` at shutdown (a flush of events scheduled on a freed host) | Stopped at Interrupt, freed in Shutdown once the scheduler stopped | every functional test stops its nodes |
| 19 | **Found by review (MEDIUM): a module's keys** (§14: a frame forwarded any key to the app's `handleShortcut`: quit, minimize, the mask, closing a dialog) | An allowlist of navigation only (`moduleKey`, `lib/keyboard.ts`): Alt+Left/Right and the browser keys (the app's history), Alt+digit for that module's own pages, Ctrl+K opening the app's palette (what runs from it is the user's choice there); only from the frame of the module shown, while the window has the focus, 8 a second at most; never handed to `handleShortcut`. The kit sends only those (`forwardKey`) | modules.test.ts (quit, minimize, mask, sidebar, Ctrl+W, Escape refused; not shown, not in focus, a flood) |
| 20 | **Found by review (MEDIUM): a module's dim** (`modal` kept the app behind its scrim for ever) | Only the module shown may dim; the dim is the app's own button: a click on it or Escape lifts it (and refuses another for 3 s); lifted when the module is left, hidden, its frame reloaded, and after 5 minutes at most; the switcher and sidebar are reachable again at once. The app's trusted dialog is a window of its own, above everything | modules.test.ts |
| 21 | **Found by review (MEDIUM): module amounts in privacy mode** (the portfolio, the module's wallet card, every chain's transactions showed them with the mask on) | Every amount of a module goes through `ModuleAmount` (the placeholder and words of `<Amount>`); Transactions keeps its masked panel for every chain | modules.test.ts |
| 22 | **Found by review (MEDIUM): signature downgrade** of a downloaded program (in "auto" a release with no `.sig` was taken) | As soon as a release key is configured, a missing or bad signature refuses the release; checksums alone only while no key is built in (shown "unsigned (checksums only)"); the signed manifest is the one whose hash is checked, the sums must agree; an older release only by the user's explicit choice. Plan: `require` the default once the release key ships (§15.4) | moduleinstaller_tests |

### 13.6 Coverage per chain

| Chain | Pages in the app | Its own questions (describers) | Tested |
|---|---|---|---|
| Layers | pack pinned; Home, Send, Receive, Transactions, My assets, Trade (DEX), Staking, Auctions … | assets (reserve+register in one question, send, mint, burn, logo, display name, control), DEX (swap, liquidity), auctions, bonds (`fastblocks.bond/unbond`), staking, withdrawals | rpc_describeconfirmation_layers.py; run_packs: deposit, send, asset transfer, swap through the dialog, confirmed |
| Thunder | pack pinned | the kit's (sends, batch pay through `wallet.send.*`, withdrawals) | rpc_describeconfirmation.py; run_packs: deposit, send through the dialog, confirmed |
| BitAssets | pack pinned (built in) | assets, DEX, auctions | rpc_describeconfirmation_bitassets.py |
| BitNames | pack pinned (built in) | names (reserve/register, update, records, subnames, transfer, release), paymail, sign as a name | rpc_describeconfirmation_bitnames.py |
| Hivemind | pack pinned (built in) | decisions, markets, shares, Votecoin, sealed votes/ballots | rpc_describeconfirmation_hivemind.py |
| zSide | pack pinned (built in) | shielded sends, shield/unshield, melt/cast, fee bump; the viewing key export asks too | rpc_describeconfirmation_zside.py |
| FastBlocks | pack pinned (built in) | bonds, unbonds, staking off, proofs of possession | rpc_describeconfirmation_fastblocks.py |

The five last are built in and routed exactly as Layers and Thunder; their own end-to-end runs through the
app (deposit, a spend of each kind, pictures) are not done yet.

### 13.7 Windows and macOS

Linux only in phase 2. Windows: no supervisor (Spawn says so: Job objects, `CreateProcess`, the `stop` RPC),
and no packs: the Windows window serves one inline document (`InlineDocument`), so `chains-pack://`
origins need a WebView2 resource handler per origin (`AddWebResourceRequestedFilter` on
`https://<stem>.pack.chains-app.invalid/*`, as the dialog's `WIN_ORIGIN`), with the same policy. The daemon
side (`remotehost.cpp`) is portable. macOS: no supervisor (`-exitwithparentfd`), no custom-scheme packs yet.

### 13.8 What remains (phases 3–5)

- **Phase 3:** the single seed (BIP39, `m/86'/0'|1'/A'`, blank wallets of the daemons with imported
  descriptors, one passphrase), restore with rescans, the offer to move to it at the first start (decision
  6), the first-run guide as the real first start (with the fresh-phrase advice of §4.1); the wizard to move
  coins into a sidechain (the deposit exists, its screens do not). (The calmer layout around a pack is done: §14.)
- **Phase 4:** the release key made and built in, then `require` the default (§15.4); Windows progress/cancel of
  downloads; upgrade/fork
  banners, a cap of BMM spending per sidechain (§13.5 #12), liveness and stall detection, the move-data wizard
  with progress, `supervisor.log` rotation, the end-to-end runs of the five other chains' pages.
- **Phase 5:** Windows (Job objects, WebView2 origins for packs), macOS, hardware wallets / xpub-only mode,
  resource caps (cgroups / Job memory), pause on battery.

## 14. Sidechains as modules of one window (2026-10-20, branch `unified-modules`)

User feedback on phase 2: a sidechain's pack brought **its own sidebar, header and status bar inside the app's**, so it
looked like a wallet inside a wallet. Now a sidechain is a **module** of one window, and its pages look like the app's
own.

### 14.1 What the user sees

- **One shell.** There is one sidebar (dashboard layout) or one top bar (workspace layout), one header with the breadcrumbs
  and the palette, one status bar, one bell, one theme, one Simple/Advanced mode and one command palette. A module adds
  no chrome.
- **The module switcher** sits at the top of the sidebar, or at the left of the workspace's top bar. It shows the
  product's own pages first (`productName()`, never a hard-coded name), then each sidechain that is turned on, as
  round pills with a dot for how each one is doing. You can move through it with the arrow keys.
- **Choosing a module** replaces the app's own sections in the sidebar:
  - The sidebar shows that module's sections and pages (Layers: Home · Wallet: Send, Receive, Transactions · Chain:
    Staking, Browse assets, My assets, Trade, Auctions · Tools).
  - Below them comes a "Module" section with **Its node** (the app's own page: height, keeping up with the mainchain,
    the log, the folder; this was the phase-1 overview) and *Settings of the modules*.
  - The wallet card becomes the module's wallet card, with its wallet and its balance.
  - In the workspace layout, the module's sections become the areas and its pages become the tabs.
  - The breadcrumbs read *Layers / Chain / Trade*.
  - The module keeps its place when you switch away and come back: its frame stays loaded, hidden.
- **Its pages fill the main area**, with no border, the app's fonts and the app's colours (its theme tokens, below).
  Opening one of the app's own pages from a module (Settings, a link) switches back to the app.
- **Back and forward belong to the app** (`lib/history.svelte.ts`). One history covers the app's pages and the
  modules' pages together. Use the arrows before the breadcrumbs, Alt+Left/Right, or the mouse's back and forward
  buttons.
- **The status bar** keeps the mainchain's sync at the left. For the module shown, it adds a pill with its name, "in
  step with …" or "catching up", and its height. Clicking the pill opens *Its node*.
- **Views across modules:**
  - **Home → Portfolio** has one row per chain: the app's wallet, then each module's wallet, available and pending
    amounts in its own coin (never added up across chains), and the assets the module's wallet holds (Layers and
    BitAssets list theirs). A row opens that module's Home.
  - **Transactions** gets a chain filter: the app's own (the page as it was), one module, or **every chain** merged
    newest first. A module's rows are read from its node through the host's read-only `listtransactions`, and
    clicking one opens that module's Transactions.
  - **Send** has an "On …" picker. A module opens its own Send, in the same window, where the spend is asked in the
    trusted dialog (§13.3).
  - **The bell** shows the app's own transactions, then the modules' notices (their new transactions, their own
    notices and update notices), each named after its module.
  - **The palette** has "Switch to …" for each module, and each module's pages and actions as "Layers: Trade" and
    "Layers: Swap assets…".
- **Dialogs:** the trusted dialog stays the app's (§13.3, unchanged). A page's own dialog, such as transaction
  details, opens inside its frame, and the app dims everything around the frame, so it reads as one modal (§13.5 #20).
  A module's notices (toasts) are shown by the app, prefixed with the module's name. A toast with buttons stays in the
  frame.

### 14.2 How (same isolation as §13.5 #10)

The frames are unchanged in what they can reach: the origin `chains-pack://<stem>/`, the same sandbox, `postMessage`
only, and only from a frame the app registered, for that frame's chain. What changed is what flows across:

| From the module (kit `lib/module.svelte.ts`) | Checked by the app (`lib/modules.svelte.ts`) |
|---|---|
| `manifest` {sections, pages: id, title, icon, iconPath, section, summary, shortcut; commands} | ids `[a-z0-9-]`, ≤ 40 pages; titles one line ≤ 48 characters with control and bidi characters removed; icon = one of the app's or an SVG path of commands and numbers only; shortcut only `Alt+digit` |
| `route` {page, tab} (a link inside its pages) | id and tab patterns; recorded in the app's history |
| `summary` {wallet, trusted, pending, assets[]} | amounts decimal text only, ≤ 20 assets, shown masked in privacy mode (#21) |
| `toast`, `notice` | 6 per 10 s; the title prefixed with the module's name by the app; txids hex |
| `modal` {open} | only for the module shown; can always be lifted (#20) |
| `key` | an allowlist of navigation (#19) |

| From the app (`tellFrame`) | |
|---|---|
| `look` {theme, tokens, fontAdjust, font, moneyFont, mode, colorScheme} | every colour token of the theme **as the app shows it** (`getComputedStyle`), so custom themes look identical; sent again when the theme changes |
| `navigate` {page, tab} | the sidebar, the palette, back and forward |
| `focus` | the keyboard moves into the page after a navigation from the sidebar |

On the module's side, the shim (the app's `/packs/shim.js`, which loads before the pack) offers
`window.__chainsModule`. The kit sees it (`MODULE`) and renders `views/ModuleShell.svelte`, which is only the page
(PageView, the page's own detail panel, the mainchain alert of a sidechain). It shows no title strip, sidebar, top bar,
status bar, palette or resize handles. It stays hidden until the app's look arrives, so there is no flash of another
theme. Every page of the kit still works unchanged. The kit's pages that the app already has itself (Settings, Manage
wallets, Kit, Mine, Sidechains) are left out of a module's manifest.

**Height:** the frame takes the whole main area, and its page scrolls inside it, as the app's own pages scroll inside
the same area. An auto-height frame was rejected: the kit's tables and the explorer fill their height (virtualised
rows, sticky headers), which would make a self-sizing frame grow without end, and the result would look the same.

**Equal isolation, better options considered:** rendering module pages in the app from a declarative description
(JSON UI) would remove the frame but needs every chain's pages rewritten. Running module code in the app's origin is
excluded (§13.5 #10). The chromeless frame keeps the phase-2 boundary, and every new message is data the app
validates.

**Compatibility:** all seven packs are pinned at commits that merge the kit's module mode
(`sidechain-template` branch `modules`, merged on each sidechain's `modules` branch). A pack without module mode sends
no manifest: its pages would still show in the frame with their own chrome, and the sidebar would only list *Its
node*.

Tests: `src/test/modules.test.ts` covers the switcher and sidebar swap, routes, history, keys, palette, portfolio,
every chain's transactions, bell, status bar, look, the download step and updates, and the three review fixes. The
kit's own tests are in `src/test/module-mode.test.ts` of the template and of each sidechain. The e2e is
`run_modules.py` (§15.5).

## 15. Downloads and updates of the sidechains' programs (phase 4 started, 2026-10-20)

Turning a sidechain on no longer needs its daemon next to the app: the app downloads it from the sidechain's own GitHub
releases. The programs bundled next to the app (phase 1) remain the offline path.

### 15.1 Where it comes from, and what is taken

- **One table of repositories**: `ModuleInstaller::Repos()` in `moduleinstaller.cpp`, one row per chain:
  stem → owner/repo and the prefix of the release's file names. Today the rows are `block-fabric/<stem>`. Moving to
  `block-fabric/backbone-<chain>` is one line per chain.
  - The page never names a repository, an address or a file. It sends only a stem from the catalog and a tag from the
    list the app made.
- **The release list**: `https://api.github.com/repos/<owner>/<repo>/releases?per_page=10`.
  - Pre-releases are included, because the test network's releases are pre-releases. GitHub's `/releases/latest`
    leaves them out and answers 404.
  - Drafts are skipped.
- **The files**: `https://github.com/<owner>/<repo>/releases/download/<tag>/<file>`.
  - The app builds these URLs itself and does not use the API's.
  - Downloads are HTTPS only, through curl. Redirects must also be HTTPS (they go to
    release-assets.githubusercontent.com), at most 5.
- **Size limits**: the list 2 MB, `SHA256SUMS-<prefix>.txt` and the manifest 64 KB each, the signature 1 KB, the
  package 200 MB.
- **The package for this computer**: `<prefix>-<tag>-x86_64-linux-gnu.tar.gz` or `<prefix>-<tag>-win64.zip`.

### 15.2 Compatible, before anything is downloaded

A release is offered only when all of the following hold. Otherwise it is listed, greyed, with its reason.

- There is a package for this computer ("No package for this computer (x86_64-linux-gnu)").
- It has `manifest-<prefix>.json` = `{format: 1, stem, version: <tag>, hostApi, platforms: {<platform>: {file, sha256,
  size, binaries}}}`, with **hostApi = the app's (1)**.
  - A release without a manifest was "made before this app could run it". **The public Layers 20261020.1 is such a
    release:** its `layersd` has no host over RPC. The app refuses it, which was checked once against the real
    release.
- `SHA256SUMS-<prefix>.txt` gives the same SHA-256 as the manifest, and the file name and size agree with the
  release.
- `binaries` lists only `<stem>d` and `<stem>-cli`, and the daemon is among them.
- The signature rules of §15.4 hold.

The user may choose any compatible release, not only the newest. Choosing a release **older** than the one in use
needs an explicit choice (`allowOlder`); an update found by the app never goes back. At run time, `pack.info` checks
the daemon's hostApi again (§13.2).

### 15.3 Install, update, rollback

**Install** (progress, Cancel, Try again in the wizard's *Its program* step):

1. Download to `.download-<tag>.part`.
2. Check the size and the SHA-256.
3. List the archive. A member that is absolute, or contains `..` or `\`, refuses the whole package.
4. Extract only the manifest's programs, each as a plain file (no links), mode `0755`.
5. Write `release.json` (tag, hash, signed).
6. Rename the folder in one step to `<datadir>/sidechains/bin/<stem>/<tag>/`.
7. Write `current` atomically, after writing `previous`.

The version before stays for rollback; older ones are pruned. A failed or cancelled job removes its partial file and
staging folder, and what was installed stays as it was.

**Update** (Settings → Sidechains → *Its program and its updates*; the user approves):

1. The new version is installed beside the running one.
2. The node is stopped.
3. The new version is made current.
4. The node is started.

If it does not run within 3 minutes, the app goes back to the previous version, restarts it, and tells the user. The
app looks for updates once per run (`modules.checkUpdates`, cached 6 hours) and reports each one it finds in the bell
and in a notice.

**Rollback**: the same steps with the previous version.

**The program the supervisor runs**, in this order: a folder the user set, then the downloaded current version, then
the bundled programs, then the old `bin/`.

### 15.4 Security: what holds today, what comes next

- **Checksums from the release** (SHA256SUMS and the manifest) stop corruption and a swap of the package in transit or
  in GitHub's storage. They do **not** stop a compromised GitHub account or repository: whoever can publish a release
  can publish matching sums.
  - HTTPS stops a network attacker.
  - The fixed repository table stops the page from choosing the source.
- **The next step, implemented but not yet mandatory: signed manifests.**
  - `manifest-<prefix>.json.sig` is a BIP340 Schnorr signature, in hex, over
    `TaggedHash("ChainsReleaseManifest/1", manifest bytes)`.
  - It is verified against the release keys built into the app (`ModuleInstaller::ReleaseKeys()`).
  - The signed manifest is the very one whose SHA-256 is compared to the package, and the sums must agree with it.
    A valid signature therefore binds the binaries to the key, whatever happens to the GitHub account.
- **Fixed after review (MEDIUM), signature downgrade.** "auto" took a release with no `.sig`, so an attacker who
  controls the release could simply leave it out. Now:
  - As soon as any trusted key is configured, a missing or invalid signature makes the release incompatible.
  - The checksum-only path remains only while **no key is built in**. Such releases are shown as "unsigned (checksums
    only)".
  - Tested by `moduleinstaller_tests`.
- **The key**: the maintainer makes it offline (never on a build machine or in CI) and writes its x-only public key
  into `ReleaseKeys()`. The release script signs each manifest on that offline machine. **Today the list is empty.**
- **Policy**: `-sidechainsignatures=auto|require`.
  - **Plan:** once the key ships and every release carries a signature, `require` becomes the default and then the
    only mode (the option goes).
  - Rotation: the app ships the next key beside the current one before the switch.
- **Test-only options, accepted on regtest only**: `-sidechainreleaseapi` (a mirror), `-sidechaindownloadcacert`,
  `-sidechainreleasekey`.
- The pages of a sidechain cannot call any `modules.*` method; the host refuses them (sidechainpages).

### 15.5 Tests

- **`moduleinstaller_tests`** covers:
  - checksum mismatch, wrong platform, oversize package and list, partial download;
  - no manifest, host API older or newer, missing sums;
  - signatures under auto and require, an unknown key, none, and a key configured with a missing or bad signature;
  - unsafe members and links;
  - cancel, update and rollback, an older tag without an explicit choice, pruning;
  - the supervisor's search order;
  - curl against a local HTTPS server;
  - with `CHAINS_TEST_SIDECHAIN_BINDIR` set: the whole path against `e2e/release_mirror.py`, with real programs and
    signatures required.
- **`e2e/run_modules.py` + `modules.js`** (the real app on regtest, Midnight), using a local HTTPS mirror of Layers'
  releases: the real 20261020.1 (refused, no manifest) and two signed compatible releases built from a `layersd`
  that has the host over RPC.
  1. Layers is not on this computer. The wizard downloads it: the newest compatible release is proposed, the old one
     is refused with its reason, and the older compatible one is chosen and downloaded with progress.
  2. Layers becomes a module: it appears in the switcher, its pages are in the app's sidebar, and its frame fills the
     main area with no border, still on its own origin.
  3. A deposit, then a send asked in the trusted dialog "On Layers", confirmed in a block.
  4. Trade and Staking open in the shell; Back works; switching back to the app restores its own pages.
  5. The portfolio and every chain's transactions.
  6. An update is found, approved, and applied: stopped, swapped, started.

## 16. Inspiration: BitWindow (LayerTwo Labs)

This section records the ideas taken from BitWindow (`LayerTwo-Labs/drivechain-frontends` at `daaefae`, MIT; folders
`bitwindow/`, `sail_ui/`, `sidechain-orchestrator/` and `sidechain_core/`). The repository was studied read-only: no
code, assets or binaries were copied or run.

**Their design.** BitWindow is the mainchain wallet, a Flutter app with **top tabs** (Overview, Wallet, Sidechains,
Mining, Learn, Console; `bitwindow/lib/pages/root_page.dart:82-92`). A Go daemon embeds an orchestrator that
downloads, starts and supervises Core, the enforcer and the sidechain daemons. Each sidechain has its **own separate
app and window** (`thunder/`, `bitnames/`, …, launched as a `<name>-gui` process,
`sidechain-orchestrator/orchestrator.go:2268-2346`). Inside BitWindow, sidechains appear as:

- a table of the 256 slots, with Download → Start → Open window / Stop / Update on each row, inline download progress
  and a sync bar (`bitwindow/lib/pages/sidechains_page.dart:257-350, 831-1060`);
- a deposit form that fills in the address from the running sidechain (`:2070-2440`);
- withdrawals, which are made in the sidechain's own app (`sail_ui/lib/pages/sidechains/parent_chain_page.dart:307-436`).

Downloads come from one embedded chain manifest (`sidechain-orchestrator/chains_config.json`). It lists each chain's
type, slot, ports, dependencies, health check, startup-log patterns, directories and per-platform assets. Sidechain
assets are taken from GitHub `releases/latest`, matched by a regex on the asset name. Extraction is careful: zip-slip
and symlink checks, regular files only, temporary folder first (`download.go:763-893`). The status bar shows the
balance, per-chain heights, daemon connection cards and an update indicator (`root_page.dart:1194-1300`, `:695-808`,
`:942-1024`).

**Adopted here:**

- one declarative table per chain (our catalog and the fixed repository table of §15);
- per-row install state with MB progress shown inline (the program step and Settings card, §15);
- a gate before a sidechain can run (here, the wizard's program step: no program, no Next);
- per-chain heights in the status bar (the module's pill, §14.1);
- an update indicator (the bell's update notices and the Settings badge);
- safe extraction rules (only the manifest's binaries, no links or `..`, a staging folder then one rename);
- an HTTPS→HTTP downgrade refused (`--proto-redir =https`);
- a release list cached against GitHub's rate limit;
- the deposit address filled in from the sidechain itself (our §13.3 already did this);
- restart with backoff and adopting a daemon that is already running (our supervisor, §1.1 and §13.3b, already had
  these).

**Done differently, on purpose:**

- **One shell, not an app per sidechain.** BitWindow opens each sidechain in its own window and splits deposit (in
  BitWindow) from withdrawal (in the sidechain's app). The user asked for modules in one window (§14), with deposit
  and withdrawal in the same module.
- **Verification that holds.** In their manifest every entry has `"hashes": {}`. Only `liquid-signet` pins an artifact
  hash (`chains_config.json:769`, `artifact_pins.go`). Nothing is checked against SHA256SUMS or a signature, and the
  hash-mismatch UI has nothing behind it (`sidechain_core/lib/config/binaries.dart:1523-1545`). Here every archive is
  checked against the release's sums and its manifest, and signed manifests come next, mandatory later (§15).
- **No `releases/latest` plus regex**, and no Last-Modified check of mutable "latest" files (`release_check.go`). We
  pin exact tags, file names and hashes in a manifest per release, and the user picks a compatible release.
- **No user-editable overlay** of the download manifest. Theirs is deep-merged and hot-reloaded from disk
  (`config_loader.go:79-215`). Our repositories are a fixed table in the app, with test overrides on regtest only.
- **No self-update by running a script fetched from a `master` branch** (`sidechain_core/lib/providers/update_provider.dart:160-180`).
  Updates are versioned, verified releases that the user approves, with the previous version kept for rollback.
- **No third-party fast-withdrawal servers** built into the wallet.

The repository has no screenshots of their UI, only logos, so none are included here.
