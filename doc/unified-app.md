# The unified Chains app

One app for Chains and its sidechains. Status: **phase 1 (supervisor, switcher, wizards) and phase 2
(the sidechains' own pages in the app, spends through the app's trusted dialog)** on branch
`unified-app`, not merged. Written 2026-10-20; phase 2 in §13.

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
| 17 | **Found:** a crash of `<stem>d` at shutdown (a flush of events scheduled on a freed host) | Stopped at Interrupt, freed in Shutdown once the scheduler stopped | every functional test stops its nodes |

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
  coins into a sidechain (the deposit exists, its screens do not); a calmer layout around a pack (the app's
  sidebar and the pack's in one, one status bar).
- **Phase 4:** download-on-enable with signed manifests (#9, `sidechainnodes.cpp` reused), upgrade/fork
  banners, a cap of BMM spending per sidechain (§13.5 #12), liveness and stall detection, the move-data wizard
  with progress, `supervisor.log` rotation, the end-to-end runs of the five other chains' pages.
- **Phase 5:** Windows (Job objects, WebView2 origins for packs), macOS, hardware wallets / xpub-only mode,
  resource caps (cgroups / Job memory), pause on battery.
