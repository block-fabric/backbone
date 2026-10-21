# The unified Chains app

One app for Chains and its sidechains. Status: **phase 1 (supervisor, switcher, wizards) and phase 2
(the sidechains' own pages in the app, spends through the app's trusted dialog)** on branch
`unified-app`; **sidechains as modules of one shell (§14) and their programs downloaded from GitHub releases (§15)**
on branch `unified-modules`; **the app's own pages over every chain (§18)** on branch `unified-integrated`; **one chain
selector (§19)** on `unified-ux`; **the quality pass (§20: one coherent app, coin control, the explorer, performance)** on
`unified-ux2`; none merged.
Written 2026-10-20; phase 2 in §13; ideas taken from BitWindow in §16.

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
| 23 | **Host-built sidechain spends** (§18: the app's Send, Sidechains, Wallets make the request, not the chain's pages) | Built in C++ from typed fields only (`ChainAdapter`): the stem from the catalog, a wallet name of `[A-Za-z0-9._-]`, the address trimmed, the amount as **text** parsed exactly (no sign, exponent or float; more decimals than the asset has refused, never rounded; in `MoneyRange` for coins), an asset **by its hash** (`"0x"<hash>`, never a name that may look alike) and only one the wallet holds, not more than it holds. The request then takes the §13 path unchanged: the chain's own describer, the trusted dialog, the one-time token bound to that request | chainadapter_tests (amounts, assets, requests), run_integrated |
| 24 | **Wrong-chain sends** (an address of the app's own chain, of another sidechain, a deposit address) | Refused **before anything is prepared or asked**: the address must be valid on that chain's node; one valid on the app's own node, or only on another sidechain, is refused with the name of that chain; a deposit address and a legacy (base58) address (they read the same on every chain of the family) are refused; one of the user's own wallets on the app's chain or on another sidechain is refused (the likeliest mistake, and the only check that holds where two chains share a bech32 prefix: on regtest the sidechains that keep the template's own, `rsc`). The page checks as the user types and Send stays disabled; the host checks again. The trusted dialog names the chain first (row *Chain*, "On Layers · Regtest" and its mark from the app's catalog, never from the daemon). **Residual:** an address of a stranger's wallet on another sidechain with the same prefix (regtest only, for the chains that keep `rsc`: main, test and signet have a prefix per chain) cannot be told apart; the chain badge of the dialog is then the last line | chainadapter_tests (six kinds), chains.test.ts, run_integrated (a Chains and a Thunder address refused on Layers, no question asked) |
| 25 | **A read that asks** (a chain's host answering a read with a question) | Refused, never asked: the adapter's reads through `webuihost` drop an answer with `confirm` (no dialog can come from a read view) | chainadapter_tests |
| 26 | **Least privilege of the adapter** | No rpcwhitelist change: generic reads are the app's user's methods (`AppRpcMethods`), a chain's own reads go through its own pages' policy (`webuihost` kind `rpc`); answers bounded (rows, lengths, depth); packs cannot call `chain.*` (refused prefix in `SidechainPages::RouteOf`) | sidechainpages_tests |
| 27 | **Deposits and withdrawals from the app's pages** | A deposit pays only the deposit address the chain's node gives for its wallet (never typed), through `PackDeposit` (only that chain's slot, Chains' own question); a withdrawal pays only an address valid on the app's own node, a deposit address refused, its fee explicit, the chain's own question | chainadapter_tests, run_integrated |
| 28 | **A module's page inside an app's page** (§18 `ModuleEmbed`) | The same frame (its own origin, sandbox, messages only, §13.5 #10): only laid over a place of the page, clipped to the page's area; the page shown is checked (`setEmbed`: stem and page patterns); it may not dim the app (only the module shown may), its keys are not taken (not the module shown); lifted when the page is left | chains.test.ts, run_integrated (its own origin inside Assets → Trade) |
| 29 | **Stored choices of the pages** (`chainViews`, `chainWallets`) | Checked when read back: chain ids `[a-z0-9]`, wallet names as #23; others dropped | chains.test.ts |
| 30 | **Amounts of every chain in privacy mode** | Every sidechain amount of the new views through `ModuleAmount` (§13.5 #21) | modules.test.ts |
| 31 | **The global selector and a spend** (§19: the chain of Send set from the top bar, a link, a sidebar item) | The selection only chooses which form is shown; the spend is built and checked exactly as before (§13.5 #23, #24): the host checks the address against every chain, the trusted dialog names the chain first with its mark from the app's catalog. A selection changed while a form is filled shows another chain's form (the typed address is not carried over: the form is made anew) | ux.test.ts, run_integrated |
| 32 | **Stored choices** (`prefs.chain`, `prefs.chainGroupsFolded`) | Checked when read back: a chain id `[a-z0-9]{1,32}` or `all`, else All; folded groups chain ids only, at most 40 | chains.test.ts |
| 33 | **Asset logos** (`chain.assetLogo`) | The chain's own host's logo (`bitassets.logo`, its bytes checked as its rules do); the app accepts only `data:image/png` or `data:image/jpeg` base64 of at most 44 KB for that asset, shown in an `<img>` only (img-src `data:` of the app's CSP), never as markup | chainadapter_tests (SVG, HTML, a URL, too large, another asset refused) |
| 34 | **New reads** (`chain.positions`, `chain.activity`) | Reads only: of the chain's own pages' policy (`getwalletpositions`, `listmarkets`, `getrawmempool`) or the app's user's methods (`getblockhash`, `getblockheader`); an answer that asks is refused, not asked; bounded (200 positions, 6 blocks, 8 mempool rows of the first 5000); every amount through `ModuleAmount` (masked) | chainadapter_tests, ux.test.ts |
| 35 | **Coin control** (§20.7: `inputs` of a deposit, of a withdrawal; `chain.coins`) | Outpoints only (`txid` 64 hex, `vout` 0..2^32-1, 1–500, none twice); the host checks each is an unspent coin of the wallet that pays (Chains' `listunspent` for a deposit, the sidechain's through its pages' policy for a withdrawal) **before anything is asked**; the node checks again (own, unspent, unlocked, mature, holding coins) and adds no other coin; the trusted dialog says "Paid with only the coins you chose", what they hold and each coin with its amount | walletapi_tests `deposit_paid_with_chosen_coins`, chainadapter_tests `coin_control_only_coins_of_the_wallet`, feature_drivechain_wallet, feature_sidechain, run_integrated |
| 36 | **Searches and decodings** (`chain.search`, `explorer.outputs`) | Reads only: a query is classified by its characters (height, 64-hex hash, address) before any call; a sidechain is read with the app's user's methods (`getblockhash`, `getblockheader`) or through its pages' policy (`getrawtransaction`, its wallet's `gettransaction`, a read that asks refused); `explorer.outputs` parses scripts the page already has (at most 500, no node read) | chainadapter_tests, run_integrated |
| 37 | **A place given to a chain's explorer** (`search:<q>` handed to a module's frame) | The tab is checked by the app (`[A-Za-z0-9_]{1,120}` after `search:`) before it is sent to the frame, and taken once: it is not searched again on another chain | modules.test.ts, run_integrated |
| 38 | **Host-built swaps** (§22, `chain.dex.swap`: the app's Trade page makes the request, not the chain's pages) | Built in C++ from typed fields only: assets **by their hash** (`"0x"<hash>`, the chain's coin all zeros; never a ticker that may look alike), the amount as text parsed exactly with the decimals of what is paid (no sign, exponent or float; more decimals refused, never rounded), only what the wallet holds (its coin by `getbalance`'s count, an asset by `listmyassets`), the slippage in basis points 1 to 5000, the least to receive as text exact with the decimals of what comes. Then the host **quotes again** (the chain's `quoteroute`, else `quoteswap`) and the swap goes only if the very route the user saw (its assets in order, 2 to 5, none twice, from what is paid to what comes) is one the pools offer now and gives, less the slippage, at least what the user accepted ("The price moved… Quote again" otherwise, nothing asked). The request then takes the §13 path unchanged | chainadapter_tests `dex_swap_quoted_again_its_route_checked_and_shown`, dex.test.ts, run_dex |
| 39 | **Slippage bounds** | Enforced by the chain, not the page: the request carries `min_out` (the least the user accepted), the chain's RPC refuses to sign for less should its pools have moved since (`swapasset`, `swaproute`: "The pool moved… Quote it again"), and its rules clear an order that would give less as a refund (Layers' batch clearing: what goes in comes back). A one-pool swap keeps the chain's own bound on the price impact (`max_impact`, 10 %) | rpc_describeconfirmation_layers.py (Layers), chainadapter_tests |
| 40 | **Route verification in the trusted dialog** | The question shows, after the chain's name and before the chain's own rows, what **the app** checked, as rows of the app's own (`SidechainPages::Call` with `app_rows`, never a daemon's words, at most 16): *Checked by Chains*, the route in words ("GOLD → CHN → SILV (2 pools)"), each pool by its pair, kind, fee tier and id, what is paid, the quote made again just now, the least received with the slippage, the price impact. Then Layers' own describer says the route again from the request itself (its assets by label **and** hash, `swaproute` "Swap through pools") with `amount_in` and `min_out`: two sources that must agree, the token bound to that request | chainadapter_tests, sidechainpages_tests, rpc_describeconfirmation_layers.py, run_dex (both routes in the dialog) |
| 41 | **Host-built liquidity and new pools** (`chain.dex.addLiquidity`, `removeLiquidity`, `createPool`) | Assets by hash, both amounts exact with their decimals and not more than held; liquidity out only of a pool whose shares the wallet holds (its own `listmyassets`), a percentage above 0 and at most 100 with two decimals; `sets_price` said (into a pool someone provides to it is false: refused by the chain if the amounts would set its price; a new pool true); a pool's kind checked (a curve, a fee tier of four, A 1 to 10 000, a weight of twelve) and any kind but constant product at 0.30 % **only once the chain's node says its pool kinds apply** (`getdexinfo`), else refused before anything is asked | chainadapter_tests `dex_liquidity_in_and_out_and_new_pools`, dex.test.ts, run_dex |
| 42 | **New reads of the DEX** (`chain.dex.info`, `pools`, `quote`, `chart`, `positions`) | Reads of the chain's own pages' policy only (`getdexinfo`, `listpools`, `quoteroute`, `quoteswap`, `getpooltwap`, `getasset`, `listmyassets`; Layers' policy gained the three reads of its DEX, branch `unified-dex`), an answer that asks refused, not asked; bounded (200 pools, 5 routes, 24 chart points, 8 assets' pools per quote), every field checked (ids 64 hex, amounts plain decimals, a kind one of three, a fee one of four tiers); an older node whose policy lacks `quoteroute` gets pair quotes, never a guess; packs cannot call `chain.*` (§13.5 #26). Every amount of the page through `ModuleAmount` (masked); prices of the market are not amounts of the wallet and stay shown | chainadapter_tests `dex_reads_pools_info_quote_chart_positions`, dex.test.ts |
| 43 | **Found by review (MEDIUM): an unknown fee tier shown as 0.30 %** (a pool whose node gave its fee in percent, or a fee of no tier, was read as the default) | The fee as the node says it, exactly: `fee_millionths`, else `fee` in percent parsed exactly; a node of before the pool kinds (no `curve`, no `fee`) is constant product at 0.30 % by its rules. A fee that is not one of the four tiers, or a kind with no fee: the pool is not shown at all, so never claimed in a question | chainadapter_tests `dex_capabilities_and_pure_parts` (0.25 %, 2500, 7, 0 refused), `dex_reads_pools_info_quote_chart_positions` |
| 44 | **Found by review (MEDIUM): a question that names one pool while the chain acts on the pair's** (with pool kinds a pair has a pool of each kind; removeliquidity, addliquidity, swapasset and swaproute name the assets, so the pool of the default kind) | Every spend names its pools **by id** where the chain takes ids (its pool kinds apply: `getdexinfo`), and the request binds those ids: `swappools [asset, amount, [pools], slippage, min_out]`, `addpoolliquidity [pool, {asset: amount}, slippage]`, `removepoolliquidity [pool, percent, null, slippage]`, `createpool … {curve, fee, amp\|weight_a}`; Layers' own describers resolve each pool by its id in the same question. On a chain without ids, a spend goes by the assets only when what they name is the pool shown: a swap only through pools of the default kind; liquidity in or out only when the pair has one pool and the wallet shares of one; else refused before anything is asked | chainadapter_tests `dex_swap_quoted_again_its_route_checked_and_shown`, `dex_liquidity_in_and_out_and_new_pools` (a second pool of the pair: refused by assets, sent by id), rpc_describeconfirmation_layers.py (swappools), run_dex |
| 45 | **Found by review (MEDIUM): pools found by "last match"** (the first page of a pair's pools, the last row of the pair taken) | A pool is confirmed **by its id** (`getpool <id>`, else every page of `listpools` of an asset of it, the one row of that id: two of one id, none), its assets those of the hop or of the pair asked; the client sends the ids it showed (a swap: each hop's pool; liquidity: the pool) and the route must match the chain's fresh quote in its assets **and** its pools; the question shows each pool by pair, kind, fee tier and id; a pair quote (`quoteswap`) is shown only on the pair's one pool of the default kind. A new pool: its id as the chain will make it (`previewpool`), refused when one of that kind already has providers | chainadapter_tests, sidechainpages_tests, run_dex (the dialog's pools by id, the app's and Layers') |

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

## 17. What remains after `unified-modules` (2026-10-20)

- **The release key.** Generate it offline and put its public key in `ModuleInstaller::ReleaseKeys()`. Then sign
  every manifest, and make `require` the default and later the only mode (§15.4).
- **Publish manifests with the releases.** The release script must write `manifest-<prefix>.json` (and its `.sig`) for
  each sidechain. Until it does, every public release is refused as "made before this app could run it": the newest
  public Layers release, 20261020.1, has no host over RPC.
- **Windows:** the download's progress and Cancel (curl.exe currently runs to the end), the supervisor and the packs
  (§13.7).
- **The crash at exit (`SIGSEGV`, 2 of 6 runs of `run_modules.py`) is fixed.** Two WSL core dumps showed it: the
  download thread of an update, still in the end of the update (§15.3: waiting up to 3 minutes for the restarted
  node), emitted an event after `App::Run` had destroyed the window (`m_window->Post` on a null window). The app
  never waited for that thread before the window went (only `~ModuleInstaller`, later). Quitting during that wait
  also took the update back (the node "did not start", so the version before was made current again) and kept the
  process alive for 3 minutes after "Shutdown done". Now: `SidechainSupervisor::StopAll` marks the supervisor as
  stopping and `WaitFor` gives up at once; the end of an update or rollback that sees the app stopping (or its
  sidechain turned off meanwhile) changes nothing; `ModuleInstaller::Stop` cancels and waits for every job, after the
  supervisor and before the window goes; and the events of the hub are no longer delivered once the window goes.
  Tests: `stop_waits_for_the_jobs`, `wait_gives_up_when_stopping`; `run_modules.py` with a quit right after the
  update's restart (4 of 4 rolled back and hung before, none after).
- **The five other chains run through the app end to end, as modules.** Their packs have module mode (pinned), but
  only Layers (`run_modules.py`) and Thunder (`run_packs.py`) have been driven end to end.
- **Small gaps:**
  - `webui_e2e_modules` is not registered in ctest yet.
  - A user cannot pin a version (only update and rollback).
  - Tags are compared as text in the UI.

## 18. The app's own pages over every chain (2026-10-20, branch `unified-integrated`)

User feedback on §14: "its better, but i would integrate it even further. for every sidechain activated, more options
(through selector/dropdowns or showing multiple at a time) would appear on respective overview/send/receive/
transactions/wallets pages, same with sidechains / PoS / etc."

So the app's **own** pages became multi-chain. A sidechain turned on no longer lives only behind the module switcher:
it adds itself to the matching page of the app.

### 18.1 The rule

- **Read views show many chains by default**, with a filter of chips: *All*, then each chain (the app's own first). The
  pages: Home, Overview, Transactions, Manage wallets, Sidechains, Staking, Assets, Names, Markets.
- **Actions act on one chain**, chosen in a picker: Send, Receive, Trade. The picker starts on the chain a link asked
  for (a chain's card, an asset's row), else the one used last on that page, else the app's own.
- **What each page chose is remembered** (`prefs.chainViews`, one value per page, checked when read back), and so is the
  wallet the app's pages use on each sidechain (`prefs.chainWallets`, chosen on Manage wallets). A chain turned off is
  forgotten by the filters (they fall back to *All*) and the pickers (back to the app's own).
- The product's and its unit's names come from the build (`productName()`, `productUnit()`); no new string names
  Chains or CHN, so the rename to Backbone / eCash (ECX) is one place.

### 18.2 Page by page

| Page | What a sidechain adds |
|---|---|
| **Home, Overview** | "Every chain": a card per chain (the app's own, then each sidechain): available and pending in its own coin (never added up), its wallet and whether it has its own backup, its assets (a click sends it), its shielded balance (zSide), deposits made here still on their way, withdrawals not paid yet, its height and its link to the app's node; Send / Receive on that chain, and its own pages. Replaces §14's Portfolio. |
| **Send** | A chain picker. On a sidechain the form is that chain's (`SidechainSend`): its wallet, an asset picker (Layers, BitAssets: the coin or an asset the wallet holds, with its decimals), the shielded account (zSide, with a memo), subtract fee (coins). The address is checked by the host a moment after typing (`chain.validate`), the amount exactly with the decimals of what is sent; Send is enabled only for an address of that chain. The spend is the chain's own question in the trusted dialog. |
| **Receive** | A chain picker. On a sidechain: a new address of its wallet (made by its node), its QR code, a shielded address on zSide; its deposit address (`s<slot>_…`), and "Deposit from <product>…" (the Sidechains page, that chain's panel). |
| **Transactions** | Every chain in one list with a Chain column (the app's own included), filter chips; the app's own chip gives the page as it was (its filters, export, fee bumps). A row opens its details **in the app** (the kit's `wallet.transaction` of that chain, shown by the app's own `TxDetailsView`), with "Open in <chain>" as a second step. |
| **Manage wallets** | Each sidechain's wallets after the app's own (filter chips): open, close, create, back up (to a file chosen in the app's own chooser, given to that node only by a token, §13.5 #9), which one the app's pages use; "From your backup phrase" or "Its own backup" said plainly. |
| **Sidechains** | "The sidechains this app runs", above the drivechain's own tabs: for each, a deposit (paid by the app's wallet to the deposit address of the chain's wallet, from its node), a withdrawal back (to an address of the app's own chain, "Mine" fills one; the fee for the miners explicit), the deposits on their way, its own withdrawals not paid yet, its link to the app's node and its merged mining. Proposals, bundles and votes stay in the tabs below. |
| **Staking** (new) | Every chain that stakes (FastBlocks, Layers), filter chips: this node's validator keys (in the set or not, stake, share of the pool), the validator set of the epoch, the bonds of the wallet, the bonded total, the fee pot, the fee split. Bonding, unbonding and turning staking on are the chain's own Staking page, shown inside this one. |
| **Assets** (new) | *Holdings*: the assets of every chain that has them in one list (Chain, asset, balance, pending, control, supply), each sent on its chain from the app's Send. *Trade*: a chain picker over the chains with a DEX, the chain's own Trade page shown inside this one. |
| **Names**, **Markets** (new) | BitNames' names and reservations, Hivemind's markets and their prices, with filter chips; registering, records, trading and votes are the chain's own pages, shown inside. |

Staking, Assets, Names and Markets are in the sidebar (and the workspace's areas, and the palette) only while a chain
that has them is on; Assets is in the Wallet section, the others after Sidechains in the Chain section.

**A chain's own page inside the app's** (`ModuleEmbed`): where a host-native view is not reasonable (a DEX, the bond
forms, name records), the app's page keeps a place, and the module's frame (§14, unchanged in what it can reach) is laid
over it (`position: fixed` over the place, clipped to the page's area, followed on scroll and resize). The frame is
told the page to show; leaving the page lifts it. Without the module's pages (not built in, node not ready), "Open in
<chain>".

**The module switcher** stays, as the second way in: moved from the top of the sidebar to its foot, under "Their own
pages" (the workspace layout keeps it at the left of its top bar). Proposed: keep it — every chain's own pages are
still the complete set of what it does, and the app's pages do not (and should not) copy every one of them. Removing it
would leave those pages reachable only from the palette and the "Open in …" links.

### 18.3 The chain adapter

`src/webui/chainadapter.{h,cpp}` (`ChainAdapter`, host methods `chain.*` in `app.cpp`), and `lib/chains.svelte.ts` in
the page (the app's own chain from its own state, a sidechain through `chain.*`).

**Capabilities**, a table in the host (`ChainAdapter::Table()`, one row per chain of the catalog, `chain.capabilities`),
never guessed from what a node answers:

| Chain | assets | dex | staking | names | markets | shielded | its pages shown inside the app's |
|---|---|---|---|---|---|---|---|
| Layers | ✓ | ✓ | ✓ | | | | trade, auctions, staking, assets, my-assets, issue |
| FastBlocks | | | ✓ | | | | staking |
| Thunder | | | | | | | |
| BitNames | | | | ✓ | | | names, register, directory |
| BitAssets | ✓ | ✓ | | | | | trade, auctions, assets, my-assets, issue |
| zSide | | | | | | ✓ | private, zhistory |
| Hivemind | | | | | ✓ | | markets, votecoin, decisions, portfolio |

Every chain: send, receive, deposit, withdraw, 8 decimals for its coin.

**Mapping** (each method takes `{stem, wallet?}`; the wallet is the one asked for if loaded, else `main`, else the first):

| `chain.*` | Reads (supervisor's read path) | Writes (the chain's host over RPC → its question → trusted dialog) |
|---|---|---|
| `summary` | `listwallets`, `getblockcount`, `getmainchaininfo`, `getbalances`; `listmyassets` (assets), `getshieldedbalance` (shielded), `listwithdrawals` + `getaddressinfo` of each refund address (its own) | — |
| `transactions`, `transaction` | `listtransactions`; the kit's `wallet.transaction` | — |
| `wallets`, `wallet.create/load` | `listwallets`, `listwalletdir`, `getwalletinfo`, `getbalances`; `createwallet`, `loadwallet` | `wallet.unload`: the kit's `wallets.close`; `wallet.backup`: the app's chooser, then `wallet.backup` with the file's token |
| `receive`, `depositAddress` | `getshieldedaddress` (zSide), `getdepositaddress` | `wallet.newAddress` (asks only if the wallet needs its keys) |
| `validate` | `validateaddress` on that chain, the app's own node and every other sidechain running; `getaddressinfo` in their wallets | — |
| `send` | the checks of `validate`; `listmyassets` (an asset) | coins: `wallet.send.prepare` (asks nothing) then `wallet.send.commit` (the question; discarded when declined); an asset: `rpc.confirmed sendasset [address, "0x"<hash>, amount]`; shielded: `rpc.confirmed sendshielded [{address: amount}, memo]` |
| `withdraw` | `validateaddress` on the app's own node | `rpc.confirmed createwithdrawal [address, amount, fee]` |
| `deposit` | `getdepositaddress` of the chain's wallet | the app's own `createsidechaindeposit` (`PackDeposit`: only that chain's slot, Chains' own question) |
| `staking` | `getstakinginfo`, `getposinfo`, `getvalidators`, `listbonds` | the chain's own Staking page, inside |
| `peg` | `getmainchaininfo`, `getbmminfo`, `listwithdrawals` (+ its own) | — |
| `assets`, `names`, `markets` | `listmyassets`, `listmynames`, `listmarkets` | the chain's own pages, inside |

Generic reads use the methods the app's user may already call at each daemon (`AppRpcMethods`, §13.5 #11); a chain's
own reads go through its host over RPC as reads of **its own pages' policy** (`webuihost` kind `rpc`), so the adapter
can read nothing the chain's own pages could not. **The rpcwhitelist of the app's user did not change.** Answers are
bounded (rows, lengths, depth) and amounts stay the text the node wrote (no float on the way).

### 18.4 Security (rows #23–#30 of §13.5)

See §13.5. In short: the host builds every sidechain spend from typed, checked fields and sends it down the §13 path
unchanged (the chain's own describer, the trusted dialog with the chain named first and its mark, the one-time token);
an address of another chain is refused before anything is prepared or asked; a read never asks; the frames are the
frames of §14; packs cannot call `chain.*`.

### 18.5 Tests

- `src/webui/test/chainadapter_tests.cpp`: the table against the catalog; amounts exact (no sign, exponent, more
  decimals, overflow); what an address looks like; the wallet picked; the reads mapped per chain (no asset read on
  Thunder, nothing asked of a chain not running); a read that asks refused and not asked; a coin send prepared then
  asked, discarded when declined, bad amounts refused before anything; wrong-chain addresses (the app's own chain's, a
  deposit address, one of the user's Thunder wallet, one valid only on Thunder, a legacy one, one of the app's own
  wallet) refused with words that name the chain and **nothing prepared or asked**; an asset send by its hash, of an
  asset held, with its decimals, not more than held; withdrawals only to the app's chain; deposits from the chain's
  deposit address; peg rows of the wallet's own; wallet names. `sidechainpages_tests`: packs cannot call `chain.*`.
- `src/test/chains.test.ts` (vitest): the chains of the pages and their table; the pages of what sidechains add only
  while a chain has it; filters (default All, remembered per page, a chain turned off forgotten) and pickers (a link,
  the last used, the app's own); stored preferences checked; exact amounts; every chain's transactions merged; assets
  merged; deposits on their way until credited; the adapter's calls; Send (the picker, a wrong-chain address said before
  anything is sent, an asset with its decimals sent, a link with an asset, no asset picker on Thunder); Receive;
  Wallets; the peg panel; Staking; Assets; Transactions; a module's page inside an app's (checked, lifted).
  `modules.test.ts` updated (Home's cards replace the Portfolio; every chain's transactions through the adapter, their
  details in the app).
- `src/webui/test/e2e/run_integrated.py` + `integrated.js` (the real app on regtest, Layers with a validator key of its
  own and Thunder, Midnight; ctest `webui_e2e_integrated`): see §18.6.

### 18.6 End-to-end run and pictures

`run_integrated.py --app build_app/bin/chains-app --sidechain-bindir ~/skydoge/layers/build_rel/bin --sidechain-bindir
~/skydoge/thunder/build/bin --tmp … --shots …`: 30 checks pass. Layers and Thunder on; the table of capabilities; a
deposit into Layers from the Sidechains page (Chains' own question), credited, no longer "on its way"; a deposit into
Thunder by the adapter; GOLD registered on Layers; a withdrawal from Layers to an address of Chains ("On Layers"), one
to an address of a sidechain refused; Home with every chain and Layers' assets, its filter remembered; Overview the
same; on Send, Layers picked: a Chains address and a Thunder address refused **before any question** (none asked,
the host refuses them too), then 25 GOLD sent, asked in the trusted dialog "On Layers" with its mark and *Chain:
Layers* first, its destination and asset named, confirmed in a block; on Thunder, no asset picker, a coin send asked
"On Thunder", the chain remembered; Receive on Layers (an address of its wallet, its deposit address) and on Thunder
(refused on Layers); Transactions of every chain, the filter, a Layers transaction's details in the app; each chain's
wallets ("Its own backup"), a wallet made on Layers; the Sidechains page with the withdrawal on its way; Staking with
Layers' validator key, Thunder not listed; Assets with GOLD, and Layers' Trade inside the page on its own origin; the
frame lifted on leaving; the module switcher still there; both turned off, nothing left running. `run_startup.py`
(Chains alone) passes.

Pictures (Midnight) in `~/skydoge/media/unified-app/integrated/`: `integrated-overview`, `-send-layers-asset`,
`-send-wrong-chain`, `-dialog-layers-asset-send`, `-dialog-deposit-into-layers`, `-receive`, `-transactions`,
`-transaction-details`, `-wallets`, `-sidechains`, `-staking`, `-assets`, `-assets-trade`.

### 18.7 Open

- **The module switcher**: kept as the second way in (proposed above); remove it only if the app's pages should be the
  only way in, with the modules' pages reached by "Open in …".
- **Legacy (base58) addresses refused on sidechains** from the app's pages (they cannot say their chain). A user who
  needs one sends from the chain's own pages.
- **BitAssets, BitNames, Hivemind, zSide, FastBlocks** are mapped (table, reads, sends, embeds) and unit-tested, not
  driven end to end; the e2e drives Layers and Thunder.
- **Pending deposits** are tracked for deposits made from the app's pages in this run (until the chain's wallet shows a
  payment to the credited address); a deposit made elsewhere shows as the chain's pending balance only.
- Embedded pages are laid over their place (`position: fixed`); a page of the app that moves without scroll or resize
  events is followed by a 300 ms check.

## 19. One chain selector, chains in the sidebar (2026-10-20, branch `unified-ux`)

User feedback on §18: "Home provides a glance of every sidechain we have active, which it does. Hivemind should show
our open positions too if any. Wallet balance graph should also have a balance per sidechain displayed. On overview the
same thing: recent TX, latest blocks, latest TXs should also reflect all chains. The chain selector should be above,
next to navigation, and work on every page, rather than putting selector inside sections; and sections be dynamic
depending on the selection. On send, rather than a dropdown, … an extra selector inside the form, above Pay to … Same
for receive. Transactions page already good, just move the selector into the one unified selector at the top." Then:
"we should nest drivechain specific features under a section per activated drivechain in the left menu under the
'CHAIN' category."

### 19.1 One selector for every page

- **In the top bar**, next to back/forward and the page's title (dashboard: after the breadcrumbs; workspace: after
  the history buttons, where the module switcher was): *All chains*, then this app's own chain, then each sidechain on,
  each with its mark and how its node is doing (`GlobalChainSelector`). Only there once a sidechain is on.
- **Global, not per page** (`prefs.chain`, checked when read back; a chain turned off is forgotten: All). Proposed and
  kept: what the user looks at is a chain, not a page of it; moving from Home to Send to Transactions keeps it. A
  per-page memory was what made the chips of §18 hard to follow (each section a filter of its own).
- **Keys and palette**: Ctrl+] the next (All → this app's → each sidechain → All), Ctrl+[ the one before; the palette
  has "Show all chains", "Show only <chain>", "Next chain", "Previous chain", and every chain's items ("Layers: Trade").
- **Every page follows it**. All: every chain. This app's own chain: the page as it was. One sidechain: only it, and
  what does not apply to it goes: Home and Overview lose this app's cards (balance, network, mining, merged mining,
  peers, the drivechain's slots) and show that chain's card, chart, transactions and blocks; Staking, Assets, Names and
  Markets say "<chain> has no <feature>" with the chains that have it a click away (`NotOnThisChain`), and are not in
  the workspace's tabs or the palette meanwhile; Sidechains shows that chain's panel; Transactions and Manage wallets
  that chain's. Every chip and picker inside sections is gone (`ChainChips`, `ChainPicker`, the Transactions filter).
- A link that acts on a chain (a card's Send, an asset's Send, a group's item) selects it.

### 19.2 Send and Receive: the chain is the form's first field

Above "Pay to", a tile per chain (`ChainTiles`: its mark, its name, what the wallet holds there in its own coin, the
number of assets; a radio group, arrow keys), in step with the top bar both ways (the tile picked is the selection).
With All selected, no form until a chain is picked ("Choose a chain first: each chain's coins are its own"). The form
below is the chain's: on a sidechain, "What to send" as pills (the coin, then each asset with its logo and balance),
the shielded account on zSide, the address checked against every chain as typed, its help line saying which chain's
address it wants, a refusal as a callout ("Not an address to pay on Layers" and why), the amount with its unit, its
decimals and "All of it" (a coin's fee then taken from the amount). Receive the same, with a line on what an address is
for before one is made.

### 19.3 Home and Overview over every chain

- **The balance chart, one line per chain** (`ChainBalanceChart`), each in its chain's colour (§19.5) and in its own
  coin: never added up. One chain shown: its line with its amounts at the right. Several: each line on its own scale
  (one axis of amounts would mix coins), a legend with each chain's balance now and unit, the crosshair's tooltip each
  chain's balance then, the words "Each line on its own scale, in its own coin". A sidechain's line is made from its
  history (`chainBalanceHistory`): from what it holds now back through its latest 200 transactions (amounts and fees).
  Privacy mode: not drawn, the legend masked.
- **Hivemind's open positions** (`MarketPositions`, host `chain.positions`: the wallet's `getwalletpositions` joined to
  `listmarkets`): a card on Home only when there are some (market, outcome "Yes"/"No" or the answers, shares, the
  outcome's price now, what the shares are worth at it = shares × price exactly), its link to Markets → Positions (a
  new tab). Every number masked in privacy mode.
- **Recent transactions, latest blocks, latest transactions** of every chain shown: Overview's three panels and Home's
  Transactions and Blocks tabs (`ManyChainRecent`, `ManyChainBlocks`; host `chain.activity`: a chain's latest six
  blocks and its mempool's newest eight, the mempool read by the chain's own pages' policy). A row's details open in
  the app as on Transactions.

### 19.4 One group per sidechain in the sidebar

The Chain section: *Sidechains* (the drivechain's own page: deposits, bundles, votes; kept there as the one place that
is about every sidechain at once), then a folding group per sidechain on (`SidebarChainGroups`: its mark, its name, a
dot for its node; folded groups remembered in `prefs.chainGroupsFolded`), holding what that chain adds, from its table
(`lib/chaingroups.ts`):

| Chain | Items |
|---|---|
| Layers | Assets, Trade, Staking, Deposit and withdraw, All its pages |
| BitAssets | Assets, Trade, Deposit and withdraw, All its pages |
| Hivemind | Markets, Positions, Votes (its own Decisions page), Deposit and withdraw, All its pages |
| BitNames | Names, … |
| zSide | Shielded (its own Private page), … |
| FastBlocks | Staking, … |
| Thunder | Deposit and withdraw, All its pages |

An item selects its chain, then opens the app's page for it (Staking, Assets, Markets…) or the chain's own page. While
a chain's own pages are shown, its group lists them (and "Its node") under "All its pages": the chain stays one place
of the sidebar. The top-level Staking, Assets, Names, Markets entries and the "Their own pages" switcher are gone.
Simple mode: the same groups in its Features section (Move coins for the deposits; no "All its pages").

**Views over many chains**, without clutter: while All is selected, the Chain section keeps "All assets" when two
chains or more have assets, and "All staking" when two or more stake (`aggregateItems`); with one chain selected they
would repeat its group's item, and go. The palette always has them.

The Wallet section keeps the shared pages (Home, Overview, Send, Receive, Transactions, Manage wallets), which follow
the selector. The workspace layout (no sidebar) keeps its tabs, filtered by the selection, with the selector and, for a
sidechain selected, a button to its own pages.

### 19.5 Colours, logos

Each chain one colour wherever it is drawn (`lib/chaincolors.ts`: its mark, its line, its tile): the slot of the
eight-colour categorical palette of its place in the catalog (`--chain-<stem>`, stepped for the dark looks), not its
pack's own colour (Layers and Thunder are both violet there: two lines alike); this app's chain the theme's accent. A filter never repaints the others. Asset logos (`chain.assetLogo`: the logo the
chain's own host gives, `bitassets.logo`, checked again by the app as a PNG/JPEG `data:` URL, at most 44 KB; cached
per chain and asset, four asked at a time), letters in their place until then or without one: on Assets and in Send.

### 19.6 Found on the hub (fixed here)

- **Available balances of a sidechain were too high**: `getbalances.mine.trusted` counts coins spent by transactions
  not in the mempool (their sum is the negative `mine.nonmempool`); the hub's Layers showed 6161.54 where `getbalance`
  says 990.31. Now `ChainAdapter::WalletBalances`: available = trusted + nonmempool (never below 0), as the chain's own
  `getbalance`; used by Summary (Home's cards, Send's tiles and form), Manage wallets, and the supervisor's overview of
  a module. Residual: a coin both unconfirmed and spent by a transaction not in the mempool stays in "pending".
- **Stakes in base units** (250100000000 next to bonds in coins): `getposinfo`/`getvalidators` give integers of 10^-8;
  shown as the chain's coin with its decimals (`fromBaseUnits`), masked in privacy mode.
- **No asset logos on Assets**: §19.5.
- **"Merged mining on: 0 blocks" on Layers**: its blocks are made by stake; said "Blocks by stake; anchors by merged
  mining: N".

### 19.7 Security (rows #31–#34 of §13.5)

Unchanged: the trusted dialog with the chain named first and its mark, the wrong-chain refusal before anything is
prepared or asked, exact amounts as text. New: see §13.5 #31–#34.

### 19.8 Tests and pictures

- vitest `src/test/ux.test.ts`: the selector (All by default, a chain picked remembered and sent to the host, not there
  alone, Ctrl+]/Ctrl+[ round, the palette's entries and a chain's item run); sections per selection (pages available,
  Staking with Thunder said with the chains that stake, Trade asks which chain with All); Send and Receive tiles in step
  with the selector both ways, the form waiting with All, the tiles masked; the chart (a sidechain's history back from
  its balance, two lines with a legend each in its unit, never a total, masked); Hivemind's positions (none: no card;
  outcome, shares, price, worth exactly, the link, masked, gone with another chain); the sidebar's groups (each chain's
  items, Simple's, an item selecting its chain, folding remembered, a module page from its group), the aggregates.
  `chains.test.ts`, `modules.test.ts`, `unified.test.ts`, `layouts.test.ts` moved to the selector and the groups.
- `chainadapter_tests`: available as `getbalance` counts it (a fixture with a negative `nonmempool`, in the summary
  and the wallets list); positions joined to their market (a row of no market dropped, a read that asks refused, no
  positions on a chain without markets); activity (blocks, the mempool's newest); asset logos (PNG/JPEG data URLs only:
  SVG, HTML, markup, bad padding, a URL, too large refused; of another asset refused).
- e2e `run_integrated.py` / `integrated.js` with BitAssets and Hivemind too when their daemons are given (a Hivemind
  market made and traded by its own CLI, the position on Home and Markets → Positions): the selector on Home and
  Overview, Ctrl+], every chain's lists on Overview, Send and Receive by their tiles (in step with the selector), the
  Transactions page by the selector, Staking with Thunder, the sidebar's groups, Simple and Advanced: 44 checks pass
  (Layers, Thunder, BitAssets, Hivemind on; nothing left running). `run_startup.py` (Chains alone) passes.
- Pictures (Midnight) in `~/skydoge/media/unified-app/ux/`: `ux-topbar-all` (the selector open), `ux-topbar-layers`
  (Home with Layers only), `ux-home-every-chain`, `ux-home-chart-positions`, `ux-overview-all`, `ux-overview-all-lists`,
  `ux-send-layers-asset`, `ux-send-thunder-coin`, `ux-receive`, `ux-transactions`, `ux-sections-staking-thunder`,
  `ux-markets-positions`, `ux-sidebar-advanced`, `ux-sidebar-advanced-bitassets-hivemind`, `ux-sidebar-simple`; and the
  `integrated-*` pictures of §18 taken again.

### 19.9 Open

- **Per page or global**: global, as asked; a per-page memory could come back as an option if wanted.
- **Several chains on one chart** are on their own scales (no shared axis of amounts: the coins differ); small
  multiples (one small chart per chain) are the alternative if the shapes side by side read badly.
- A sidechain's line uses its latest 200 transactions: an older wallet's line starts at what it held then.
- The sidebar lists a chain's own pages under its group only while they are shown; listing them always would be long
  (Layers has a dozen).

## 20. Quality pass: one coherent app over every chain (2026-10-20, branch `unified-ux2`)

User feedback on §19: "think more on unified wallet, your results 2–3 days ago were much nicer." The rounds of §18–§19
added views fast and the pages stopped agreeing with each other: Overview said *All chains* and showed one chain's total,
lists were titled with raw addresses and filled with merged-mining dust, the sidebar repeated "Deposit and withdraw" in
every group, boxes sat in boxes, the copy read like this document. And on the hub the build of §19 kept WebKit busy on
ten cores. This section is the design the pages now follow; §20.6 is what was measured.

### 20.1 What lives where

```
Sidebar                       why
  Home                          a glance: what you hold on each chain, how the nodes are doing
  WALLET  Overview              your balances and activity, in detail
          Send · Receive        act on ONE chain (picked in the form, in step with the selector)
          Transactions          every chain's history, one list
          Manage wallets        each chain's wallets
          Advanced transactions (Advanced)
  CHAIN   Sidechains            moving coins in and out (ONE form), the state of the drivechain
          Staking               only while a chain that stakes is on (FastBlocks, Layers)
          Mine                  (Advanced)
          Block explorer        every chain's blocks and transactions
          ● Layers  ▸           one group per sidechain turned on: only what is ITS OWN
          ● Thunder ▸
  TOOLS   Tools                 (Advanced)
```

- **A sidechain's group** lists only that chain's own pages from its own app's *Chain* section (its local pages, not
  the template's Drivechains, Merged mining and Explorer, which the app has once for every chain), without Staking (the
  app's Staking entry has it). Its pages open in the app's main area (its module frame, §14), titled and iconed as its
  own app does; in Simple mode only those its own Simple mode shows. The head of a group folds it; its dot says how its
  node is doing. A chain with no page of its own (FastBlocks: Staking only) shows its head alone, which opens its Home.

  | Group | Advanced | Simple |
  |---|---|---|
  | Layers | Browse assets, My assets, Trade, Auctions | My assets, Swap |
  | BitAssets | Browse assets, My assets, Trade, Auctions | My assets, Swap |
  | BitNames | Get a name, My names, Name directory, Paymail, DNS | Get a name, My names |
  | Hivemind | Markets (and under it Create, its tab), Portfolio, Voting center (Votecoin its tab), Decisions, Payouts (§21.1) | Questions, My bets |
  | zSide | Private, Memos, Private activity, Melt & cast, Shielded pool | Private payments |
  | Thunder | Batch pay, Payments, Throughput | Payments, How busy |
  | FastBlocks | — (its Staking is the app's Staking entry) | — |

- **Gone from the sidebar:** "All assets", "All staking" (views of the selector, §19, that repeated the groups),
  "Deposit and withdraw" in every group (the Sidechains page is the one place that moves coins; a chain's card and the
  palette lead there with that chain selected), and "All its pages". **Settings → Sidechains → "Show every page of each
  sidechain in the sidebar"** (off by default; Advanced) brings the last one back, for debugging: the chain's whole own
  navigation under its group.
- The app's Assets, Names and Markets pages (§18) stay for the palette and links; the sidebar leads to each chain's own
  pages instead, which do more.

### 20.2 All, or one chain: what each part shows

The top bar's selector (§19) decides every page. A part that cannot follow it says what it shows in its title.

| Part | All chains | Chains (this app's own) | One sidechain |
|---|---|---|---|
| Home, first row | **Balances**: one line per chain (its mark, name, available in its coin; pending under it), Send/Receive; **Network** (Chains), **Sidechains** (their nodes in step, deposits and withdrawals on their way) | as the standalone app: Balance, Network, Sidechains, Mining | that chain: **Balance** (its wallet, its assets), **Node** (height, in step with Chains), **Peg** (on their way, slot, escrow), **Merged mining** or **Staking** |
| Home, chart | one line per chain, each on its own scale (§19.3) | the wallet's balance, timeframes | that chain's balance |
| Overview, balances | a table: chain, available, pending, what else it holds | the wallet's balances and chart | that chain's |
| Recent transactions | every chain, newest first, each named in words (§20.4); mining rewards of a chain folded into one row | the wallet's | that chain's |
| Latest blocks | one table: chain, height, transactions, age (genesis said "genesis") | Chains' | that chain's |
| Send, Receive | tiles: a chain with coins, then those without (muted, "empty"); no form until one is picked | the form | the form |
| Transactions | every chain, a Chain column | as before | that chain's |
| Sidechains | **Move coins** (one form) + **Your sidechains** (a table) + the drivechain's tabs | the same | **Move coins** on that chain + its row |
| Block explorer | search every chain at once, results named by chain; a strip of blocks of each chain, a block opening under them (§21.4) | Chains' explorer (§20.5), a block opening under its strip | that chain's own explorer, in the page |
| Staking | a tab beside the title for each chain that stakes, the one shown its whole own page (§21.2) | "Chains has no staking", the chains that do as tabs | that chain's own page, its tab alone |

### 20.3 Visual rules

- **Spacing**: 4 · 8 · 12 · 16 · 24 · 32 px (0.25 to 2 rem). Between the sections of a page 1.1 rem; inside a panel
  1.3 rem of padding; between a label and its value 0.25 rem; between rows of a list 0.5 rem.
- **One box per section.** A section is one `.panel`. Inside it: rows separated by a line, tables, definition lists —
  never another bordered box, except the tiles of a choice (Send's chains, Move coins' direction) and inputs.
- **A row of cards has equal heights**, its buttons at the bottom; a card never holds a lone line in a tall box.
- **Type**: page title 1.7 rem; panel title 1.2 rem (`h2`); a small-caps label for groups of a panel; numbers in the
  number font, tabular; secondary text muted, never smaller than 0.8 rem.
- **Numbers**: amounts exact, 8 decimals, the unit after the figure (`<Amount>`, `<ModuleAmount>`), masked in privacy
  mode; counts with separators; a time is relative under a day ("3 minutes ago"), else a date; a chain's first block
  is "genesis".
- **Chains**: named in words with their mark (`ChainBadge`) wherever a chain is meant; its colour only on its mark, its
  line and its tile edge, never a whole card.
- **Titles of rows are words**: what happened ("Received from Alice", "Mining rewards", "Deposit into Layers"), the
  address or id only as the secondary line, shortened in the middle.
- **Separators**: "·" between facts is a `.dot-sep` (`{' · '}`, kept as written by CSS). Svelte drops the space at the edge of an `{#if}`
  block, which made "main· own backup" everywhere.
- **Copy**: plain, short, friendly; second person; what to do, not how it works ("You confirm the details in the next
  step"). No internal words (wallet modes, "pack", "adapter", "host").
- **Simple mode**: the drivechain's internals (proposals, bundles, votes, raw data) go; coin control behind "Choose
  coins"; the explorer without raw JSON.

### 20.4 Page by page

```
HOME (All)                                    [Send] [Receive] [Move coins] [Explorer]  All actions ⌃K
┌ Balances ───────────────────────┐┌ Network ─────┐┌ Sidechains ──────┐
│ ● Chains      4 239.97692241 CHN ││ ✓ Up to date ││ 4 on, all in step│
│ ● Layers         18.99990000 CHN ││ Height  186  ││ On their way   1 │
│ ● Thunder ...                    ││ Peers     0  ││ In escrow  60.00 │
│ [Send] [Receive]                 ││ [Peers]      ││ [Move coins]     │
└──────────────────────────────────┘└──────────────┘└──────────────────┘
┌ Balance of each chain (chart) ───────────────────┐┌ Blind merged mining ┐
└──────────────────────────────────────────────────┘└─────────────────────┘
[ Open positions ] (Hivemind, when any)
[ Transactions | Sidechains | Blocks | Peers ]  one table, the selector's chains

OVERVIEW (All)
┌ Balances ─ table: chain · available · pending · holds ─────────────┐
┌ Recent transactions ──────────┐┌ Latest blocks ─────────────────────┐
│ ↓ Received from Alice  +12.5  ││ ● Chains   186  2 tx  12 s ago      │
│ ⛏ Layers: 9 mining rewards    ││ ● Layers    11  1 tx  20 s ago      │
└───────────────────────────────┘└────────────────────────────────────┘

SIDECHAINS
┌ Move coins ─────────────────────────┐┌ Your sidechains ─────────────────────┐
│ [Into a sidechain][Back to Chains]  ││ chain · node · balance · merged mining│
│ Sidechain  [● Layers ▾]             ││ On their way: deposit 1.00 → Layers   │
│ Amount [       ] CHN  Available …   │└──────────────────────────────────────┘
│ To  your Layers wallet              │
│ ▸ Choose coins (Advanced; Simple: a link)
│ [Deposit…]  You confirm it next.    │
└─────────────────────────────────────┘
[ Overview | Proposals | Withdrawals ]  (Advanced: slots, every active sidechain, its deposits)
```

- **Move coins** is the only deposit and withdrawal form: direction first (into a sidechain / back to Chains), then the
  sidechain (every active one; those this app runs first, with their balances; one it does not run takes a typed deposit
  address), the amount with what is available on the side it leaves, where it goes (your wallet on that chain, or an
  address of Chains for a withdrawal, "Mine" filling one), the fee, then **Choose coins**: the coins of the side the coins
  leave (Chains' wallet for a deposit, the sidechain's wallet for a withdrawal), ticked; only those are spent. The old
  "Deposit to the selected sidechain" form of the Overview tab is this one (merged).
- **Send**: tiles, then a single panel: what to send (coin, assets), pay to, amount; the help line one short sentence.
  No second title ("Layers Send"): the tile picked says the chain.

### 20.5 The block explorer

- **One page that follows the selector**; its own views (home, block, transaction, address, waiting transactions) are
  places of the page (`nav.tab`: `block:<hash>`, `tx:<txid>`, `address:<address>`, `mempool`), so Back and Forward, a
  link from Transactions or another chain all lead to one view.
- **All**: one search for a height, a hash, a transaction id or an address on every chain at once (`chain.search`;
  Chains by its own node), each result named by its chain and opening that chain's view; the newest blocks of every
  chain in one table (chain, height, transactions, age); the waiting transactions of each.
- **Chains**: the explorer of the standalone app, reorganised as an explorer: the home (the ribbon, the blocks with
  their pagination, the waiting transactions), the **block** (header facts with copy buttons; its coinbase; its
  drivechain messages as tables, as explorer.blockfab.org has them: blind merged mining M7 with the bid and its M8
  request, deposits M5, withdrawals M6, votes M4, bundles M3, proposals and acks M1/M2, each sidechain named, each
  sidechain block a link into that chain's explorer when this app runs it), the **transaction** (inputs and outputs, what
  each drivechain output says, the escrow before and after, a deposit's credit on its sidechain, a bundle's payouts),
  the **address** (what the wallet knows of it: no index is added).
- **A sidechain**: its own explorer (the chain's page in its frame, §14) inside the app's page: its blocks with the peg
  of each, the block of Chains that merged-mined it, deposits with their transaction on Chains, its own operations
  (assets, names, markets) as its own app shows them. Links between chains go through the app: a sidechain block of a
  BMM row opens that chain's explorer at it (`search:<hash>`); a deposit's credit on its sidechain is the deposit
  address credited, searched there.

### 20.6 Performance

Budget: idle (window shown, nothing happening) **under 5 % of one core** for the app and its web process together; a
page switch without a dropped frame; a sidechain catching up under one core on average. Measured by `run_showcase.py
--perf` (four sidechains on, a lived-in wallet, software GL as under WSLg): see §20.8 for the numbers before and after
and the causes found.

### 20.7 Security

Unchanged: the trusted dialog names the chain first with its mark, a wrong-chain address is refused before anything is
asked, amounts are exact text, every amount is masked in privacy mode. New, coin control: the coins chosen are named
by `{txid, vout}` only; the host checks their shape and that each is an unspent coin of the wallet that pays (Chains'
wallet for a deposit, the sidechain's for a withdrawal) before anything is asked; the node checks again (`inputs` of
`createsidechaindeposit` and of the sidechains' `createwithdrawal`: the wallet's own, unspent, unlocked, mature, holding
coins, named once; no other coin is added); the trusted dialog says "Paid with only the coins you chose", what they
hold, and each coin with its amount.

### 20.8 What was measured, what was changed (performance)

How: `src/webui/test/e2e/run_showcase.py --perf` starts the app on a lived-in regtest state (two wallets, four
sidechains on, 20 000 records of history, 300 payments on Layers, assets, a market, a withdrawal on its way), on the
user's WSLg desktop with software GL (`--real-display`, `LIBGL_ALWAYS_SOFTWARE=1`, as the hub runs it), and samples the
CPU time of every process it started (`/proc/<pid>/stat`, the web process's threads by name) while the page is left
alone: the first 45 s after the start, Home while a block comes every 10 s and a payment every 2 s ("busy"), Home while
blocks come as fast as the node takes them ("catch-up"), then each page idle. In % of one core, the app and its web
process (the sidechains' nodes apart, about 1 % for four).

**Numbers** (2026-10-20, % of one core, the app and its web process; the build of §19 against this one, the same
state, the same desktop; `--perf-seconds 45`):

| While | §19 (`unified-ux`) | §20 | |
|---|---|---|---|
| The first 45 s after the start | 15.1 | 3.4 | |
| Home, idle | 9.2 | **2.4** | budget 5: passes |
| Overview, idle | 7.5 | 2.6 | |
| Transactions, idle | 7.2 | 2.0 | |
| Sidechains, idle | 6.3 | 2.0 | |
| Block explorer (All), idle | 13.2 | 1.6 | |
| Home, a block every 10 s and a payment every 2 s | 94.6 | 69.6 | of it the node's own work (the payments of a 20 000-record wallet): 24 and 27 |
| Home, catching up 670 blocks in 35 s | **410.7** | **90.4** | budget one core: passes; the web process 402 → 85 |

The sidechains' four nodes add about 1 % in all, idle. The catch-up is what the hub saw: four cores and more, the web
process's llvmpipe threads drawing a frame for each block.

**Causes found.** Under software GL every frame WebKit composites is drawn by llvmpipe on every core, so the cost is the
number of frames times their area, not JavaScript:

1. *Hidden pages of every sidechain*: each module frame (a whole app) was made at the start, hidden, and kept polling
   and repainting its own status bar. Now a chain's frame is made the first time one of its pages is opened (or embedded:
   the explorer, a group page), `ModuleFrames.svelte` `wanted`.
2. *The status bar's new-block animation*: 900 ms of transform and a spreading box-shadow on every block: during a
   catch-up a block every few hundred ms, so the animation never stopped and the whole bar repainted each frame. Now a
   300 ms fade, and none in reduced effects.
3. *The balance chart*: one path point per record (20 000), its path rebuilt and redrawn on every transaction; now
   decimated to the pixels it has (`lib/chartdata.ts`) and recomputed only when its records change (`historyKey`).
4. *Clocks*: relative times ticked every 5 s on Home; now 30 s.
5. *Software rendering itself*: when the app runs with software GL (`app.info.softwareRendering`, from
   `LIBGL_ALWAYS_SOFTWARE`, `GALLIUM_DRIVER=llvmpipe|softpipe` or `WEBKIT_DISABLE_COMPOSITING_MODE`), the page drops
   its animations, transitions, large shadows (`.reduced-effects`): each is a repaint of its area per frame.

6. *A block, a change of everything*: the host already merges tips (250 ms), but any event that is not merged (a new
   transaction of the wallet: each block mined to it) delivered the newest tip at once, so a catch-up gave the page
   ~20 tips a second, each read again by every part that shows the chain. The page now takes the newest tip once a
   second (`lib/state.svelte.ts` `takeTip`, `TIP_MS`); the wallet's changes were already merged to one a second.
7. *Reads that change nothing*: the polls of the sidechains (activity, transactions, positions, summaries) put what they
   read in place even when it was the same, and the summary's own bookkeeping was followed by the `$effect`s that asked
   for it (each answer ran them again: their timers anew, the peg asked again). Now an unchanged answer is left alone and
   the bookkeeping is not followed (`refreshSummary`, `lib/manychains.svelte.ts` `same`).
8. *Home's chart* follows the history at most every 5 s (`CHART_MS`).

Polling was already paused while the window is hidden (`document.hidden`); the wallet's history on Transactions was
already windowed (`DataTable`); the list of every chain is the newest 500 rows.

### 20.9 Tests

- **Node** (`test/functional/feature_drivechain_wallet.py`): a deposit paid with the coins chosen spends exactly them
  and the escrow; refused: a coin not of the wallet, named twice, none, locked, spent, too few (not topped up),
  `inputs` with `replaces` (immature and no-value coins are refused by the same checks, not exercised there). The
  sidechains (`feature_sidechain.py`, template, merged into every chain's `ux2` branch): a withdrawal paid with the
  coins chosen, the same refusals.
- **Host** (`test_chains-app`): the trusted dialog of a deposit with chosen coins ("Paid with", "They hold", each coin),
  a coin not of the wallet refused before anything is asked (`walletapi_tests` `deposit_paid_with_chosen_coins`); the
  adapter's `chain.coins` and its check of the coins of a withdrawal (`chainadapter_tests`
  `coin_control_only_coins_of_the_wallet`); the records say what a transaction does on the drivechain (field 18).
- **Pages** (vitest): the sidebar's groups (own pages only, Simple, every page with the option), the palette, the
  activity in words (`activity.test.ts`: rewards and bids folded, rows of no value left out, deposits named, a block's
  age, the genesis).
- **End to end** (`run_integrated.py`, the app and four sidechains): one Move coins panel; a deposit with two coins
  chosen spends exactly those (and the escrow), its question names them; a withdrawal from Layers with one coin chosen
  spends exactly it (checked by the runner on Layers' node); Home, Overview, the explorer under All (one search, the
  result in Layers' own explorer), under Chains (a block's merged mining a link into Layers' explorer, a deposit's credit
  a link to the address it credits), the search of one chain not made again on another; the sidebar by default and with
  every page; the deposit said "Deposit into Layers" on Transactions.
- **Showcase and cost** (`run_showcase.py`): a lived-in state made once, every page pictured in each mode, look and
  size of a matrix; `--perf --budget 5` fails when the app costs more than 5 % of one core idle on Home (ctest
  `webui_e2e_idle_cost`, with the sidechains' programs).

### 20.10 Before → after

Pictures in `media/unified-app/ux2/`: `pages/` every page per mode, look and size; `before/` the build of §19 on the
same state; `e2e/` the end-to-end run with its trusted dialogs; `perf/` the numbers.

| Seen in §19 | Now |
|---|---|
| Overview with All: one chain's total and chart | A Balances table of every chain (available, pending and what is on its way, assets), then recent transactions and latest blocks of every chain |
| Recent transactions buried in mining dust, titled with addresses and txids | Each row in words (your label, else "Payment received", "Deposit into Layers", "Coins in from Chains"); rewards and merged mining bids folded into one row per chain; rows of no value left out; the address short, on the second line |
| Latest blocks: "15 years and 37…" for a genesis, cramped columns | One table (chain, height, transactions, hash, age), "genesis", relative ages, the hash column goes when the panel is narrow |
| Home: "Every chain" cards repeating Balance | One Balances card for every chain beside Network and Sidechains; one sidechain selected: its own Balance, Node, Peg, Merged mining or Staking |
| "main· own backup" everywhere | Separators keep their spaces (`.dot-sep`; spaces at the edge of an `{#if}` kept in a dozen places) |
| Sidebar groups: "Deposit and withdraw", "All its pages", generic icons, "All assets", "All staking" | Each chain's own pages only, with its own titles and icons (Simple's in Simple); every page only with Settings → Sidechains' option; one Staking entry |
| Sidechains: a deposit and a withdrawal box per chain, stat boxes, a second deposit form | One Move coins panel (direction, sidechain, amount, where, fee, Choose coins) beside one Your sidechains table |
| Send: empty chains as equals, "1 assets", "An address of Layers· An address of this wallet", "Layers Send", copy like a spec | Empty chains muted ("Nothing to send"), "1 asset", "A Layers address", no second title, one short line |
| The explorer of Chains only | It follows the selector: one search over every chain, each chain's own explorer, links between chains, a block's drivechain messages and a transaction's outputs said |
| WebKit at four cores and more on the hub while catching up | Under one core; idle 2 % instead of 6-13 % (§20.8) |

## 21. The hub's scale, Hivemind's menu, Staking whole (2026-10-21, branch `unified-ux3`)

User feedback on §20: on the hub, five minutes after the start (the sidechains caught up), WebKit's web process still
used 117–213 % of a core; the test data of §20.8 was smaller than the hub's. And two requests: "for Hivemind, in the
unified wallet, I'd like to get Votecoin into the Voting center section as one. Also, Create should be nested under
Markets"; "the bond / unbond / turn on section visible by default inside the sidechain staking window; we would show the
full page. Beside the Staking title, just a tab selector … to browse the different ones that have PoS only."

### 21.1 Hivemind: Votecoin in the Voting center, Create under Markets

```
Sidebar (Advanced)              Hivemind's pages (its pack, the same in its own app)
● Hivemind ▾
  Markets                       Markets   [Portfolio] [+ Create a market]
    Create          (nested)    [ All markets | Create › A market · A decision alone ]
  Portfolio
  Voting center                 Voting center                          [🔔 Remind me]
  Decisions                     [Your voting power] [To vote on] [To reveal now] [For the voters]
  Payouts                       [ Vote | Votecoin ]
Simple: Questions, My bets        Votecoin: what it is for · Accounts of this wallet · Move votecoin
                                            Over time · Largest holders
```

- **Votecoin is a tab of the Voting center**, one page: what the wallet's votecoin weighs (*Your voting power*: the
  votecoin it holds, its share of all, in how many accounts) heads both tabs; *Vote* is the center as it was (vote as,
  reveal now, to vote on, coming up, your votes); *Votecoin* says what it is for in two sentences with the figures the
  rules give (in all, voters, the least that seals a vote), then the accounts of the wallet (their share of all), moving
  it, how it went, the largest holders. The four figure cards Votecoin had are the head's. With no votecoin, the Vote tab
  says so and links to the Votecoin tab.
- **Create is a tab of Markets** (`All markets | Create`), its sub-tabs what it creates (`A market · A decision alone`,
  the kit's nested tabs, §TabRow), the wizard without a title of its own; *Create a market* stays the Markets page's
  primary action (it opens the tab) while the list shows.
- Both are `tabOf` pages of the pack (`chain/local.ts`): `go('create', …)`, `go('votecoin')`, the palette's *Create a
  market…*, *List a decision…*, *Move votecoin…* and Decisions' *Open a market on it* lead to the tabs; in Hivemind's
  own app the sidebar shows Markets and Voting center only (its kit has no third level). The pack is pinned at
  `hivemind` `ux3` (branch from `ux2`, `modules` merged).
- **The app's sidebar** lists Create nested under Markets (`lib/chaingroups.ts` `under`, drawn as the group's deep
  entries): the user asked for it *under* Markets, and the groups already have a deeper level; it opens Markets on its
  Create tab, is current while that tab shows (Markets then reads as the place it is in, `within`), and is Advanced's
  (Simple keeps Questions and My bets). Votecoin has no entry: it is the Voting center's.

### 21.2 Staking: the chain's whole page, a tab per chain that stakes

```
Staking  ( ● Layers  ● FastBlocks )            ← the chains that stake, of those the top selector shows
Slot 1 234 · epoch 12 …                        ← the chain's own Staking page, whole (its frame, headless)
[ Overview | Validator | Bonds | Network ]
…  bond, unbond, turn staking on or off, its keys, payout, the validator set …
```

- The page is the chain's own Staking page (FastBlocks', Layers'), whole, in the app's page: its frame laid over the
  page (§18 `ModuleEmbed`), **headless** (a new flag of the kit's module mode, `sidechain-template` `ux3`: the page's
  own title hidden, the app's is the title), filling the window and scrolling inside. Every spend is its own question in
  the app's trusted dialog with the chain's mark, as before.
- Beside the title a **tab per chain that stakes**, turned on and shown by the top bar's selector: All → each (Layers,
  FastBlocks), one chain → that one; one that does not stake → "Thunder has no staking", the chains that do as the tabs
  (a tab then selects its chain). The tab is remembered (`prefs.stakingChain`). Only the chain of the tab shown is asked
  anything (its frame); the summaries the page read of every chain every 8 s are gone, with its cards and tables that
  repeated the chain's own page (proposed: no cross-chain summary; the tab's dot says how each node is doing).
- Simple mode: the chain's own Simple card (stake and earn), under the same title and tabs.
- Layers' and FastBlocks' *Fee split* card said a sentence in the size of a figure ("Fee split from block 2 147 483 647:
  validators 50 % / BMM 50 %"); now the two shares (`50 % / 50 %`) and under them who gets which and since when (now, from
  block N, not scheduled yet).

### 21.3 At the hub's scale: what was measured, what was the cause, what was changed

**Measured where it happened.** `run_showcase.py --scale hub` makes a state the size of the hub's once (`hubscale.py`,
through each node's RPC, about 25 minutes): Chains' wallet of 25 778 transactions (17 000 sends, 8 000 mining rewards,
the bids of merged mining, a few hundred received; the hub's has 27 992: 19 649 sends, 7 938 rewards), Layers with
2 000 payments and 35 assets (26 logos), BitAssets with 700 and 63 assets (28 logos), Hivemind with 80 decisions,
90 markets and 5 positions; Layers, BitAssets and Hivemind on, as on the hub. It looks at it as the hub does: a window
of 3440 × 1380, Catppuccin Mocha, the text two steps larger, every block in the explorer (`explorerCount` 0), and draws
as WSLg draws (software GL, `LIBGL_ALWAYS_SOFTWARE=1`, WebKit's DMA-BUF renderer: on a virtual display with
`--env WEBKIT_DISABLE_DMABUF_RENDERER=`, checked against the user's own display with `--real-display`). It measures
each page left alone (30 s), and each page **live** (`--steady`, 90 s: a block of Chains a minute with each sidechain
merged-mined in it, the bids paid by Chains' wallet, and a payment on a sidechain every 3 s), with `--probe` (what
changes in the page, by part, and how late its timers run) and `--tick-test` (one word changed each second: what one
frame of this window costs).

**Numbers** (% of one core, the app and its web process, the sidechains' nodes apart; the app's own share includes about
2 % of RPC the runner itself asks; pictures `media/unified-app/ux3/perf/`):

| While | before (`unified-ux2`) | after |
|---|---|---|
| The first 45 s after the start | 12.5 | 3.5 |
| Home, idle | 3.5 | 1.2 |
| Overview / Transactions / Sidechains / Explorer, idle | 3.2 / 2.8 / 0.7 / 4.5 | 1.8 / 0.7 / 0.8 / 1.1 |
| Hivemind's Markets / Voting center, idle | 2.8 / 2.0 | 2.7 / 2.3 |
| BitAssets' Browse assets / Layers' My assets, idle | 12.3 / **40.8** | 3.1 / 4.2 |
| One word changed each second | **48.0** | 2.9 |
| Home, live | 54.4 | 7.9 |
| Overview, live | 57.8 | 10.4 |
| Transactions, live | 51.7 | 6.8 |
| Block explorer, live | **140.4** | 9.4 |
| Hivemind's Markets, live | **93.1** | 9.6 |
| BitAssets' Browse assets, live | **260.9** | 14.4 |

On the user's own display (WSLg, the hub's way), the same state and the build of §20, with GL compositing and painted
without it (change 1 below alone): one word a second 39.0 % → 3.5 %, Home live 34.5 % → 9.8 %, the first 45 s 15.1 % →
3.2 %. Budgets: idle under 5 % (every page passes), live under 25 % (every page
passes, the heaviest 14 %).

**The cause: a frame of this window, drawn by WebKit's GL compositing on an emulated GPU, cost 0.38 s of processor.**
Under WSLg (and any machine without a GPU driver) OpenGL is llvmpipe: Mesa's rasteriser on the processor, on 32 threads
here. WebKit composites every frame of the window with it, and the hub's window is 4.7 million pixels: one word changed
each second cost 39-48 % of a core, a page whose parts change a few times a second (a list read again, a sidechain's
pages polling, a chart, the ages of rows) one to three cores — the 117-213 % the hub showed. With the same page painted
by the processor without the GL compositing (`WEBKIT_DISABLE_COMPOSITING_MODE`), a frame costs 0.03 s, eleven times
less. Tried on the same live Home: Skia's CPU painting 22 %, llvmpipe on 4 threads 20 %, both 15 %, no GL compositing 8 %.

**What was changed:**
1. *Painted without GL when GL is emulated* (`-webuicompositing=auto|on|off`, `webui/appconfig.cpp` `ApplyRendering`, set
   before the web view starts): with `LIBGL_ALWAYS_SOFTWARE` or llvmpipe, `WEBKIT_DISABLE_COMPOSITING_MODE=1` and llvmpipe
   on 4 threads (`LP_NUM_THREADS`) for what still uses it; what the user set is kept; `-webuicompositing=on` keeps the GL
   compositing. A machine with a GPU is not touched. The look is the same (pictures).
2. *Fewer changes of the page*, each a frame: the chart of balances follows the chains at most every 15 s and the latest
   transactions every 5 s (`lib/throttle.svelte.ts`); rows keyed by transaction, not by their place (a new one made every
   row of the list written again: 13 000 changes of the page in 90 s on Transactions); a text of the outside has its
   direction set only when it changes (`SafeText`: Svelte assigned `dir` at each update of an element with `dir="auto"`,
   on every row of every list read again; in the app and the kit of the packs); the explorer's ribbon draws as many boxes
   as are scrolled to (every block listed made a thousand boxes, laid out again at each block).
3. *Less work at the hub's sizes*: the rows of every chain made from the newest of the wallet, not its 28 000 records at
   each block; ids sorted as they are, not by the locale (the ties of a long history); the Transactions page's rows are the
   wallet's records as kept (§21.5).
4. *The sidechains' frames*: a frame not shown rests as a window not seen (the shim's `document.hidden` and
   `visibilitychange`: its pages' timers stop), the state of its node (tip, mempool, peers) is handed the newest once a
   second while shown and held while hidden (`lib/packs.svelte.ts`), and a hidden one raises a notice at most every 30 s
   (a busy sidechain's payments made a notice slide in and out every few seconds; the bell has them all).

**The check** (ctest `webui_e2e_hub_cost`, with the sidechains' programs): the hub's state (made once, kept), Home,
Overview, Transactions, Sidechains, the explorer, Hivemind's Markets and BitAssets' assets idle under 5 %, Home, Overview,
Transactions and Hivemind's Markets live under 25 %, drawn as WSLg draws.

### 21.4 The block explorer: a strip of blocks for each chain

User: "I'd like to depict the block representation of all active sidechains on the block explorer main page rather than
the latest blocks table. The 3D representation is nice. Then when we click on any block its description would open
below."

```
Block explorer      [ height, block hash, transaction id or address, on every chain ]  [Search]
┌ Blocks of each chain ──────── Newest first. Click a block to see it below. ─────────────────────┐
│ ● Chains   8 279   ┊8 280┊ ▣8 279 ▣8 278 ▣8 277 …   (a box: height, transactions, the sidechains │
│ ● Layers   7 863   ┊7 864┊ ▣7 863 ▣7 862 …           it merged mined by their marks, how long ago) │
│ ● BitAssets …                                                                                    │
└──────────────────────────────────────────────────────────────────────────────────────────────────┘
┌ ● Chains  Block 8 279                       [‹] [›] [⧉ hash] [Open in its explorer] [✕] ┐
│ its facts · its transactions · its coinbase and drivechain messages (as its page in Chains' explorer) │
└──────────────────────────────────────────────────────────────────────────────────────────────────┘
```

- **All chains**: one strip per chain the selector shows (Chains first, then each sidechain on), its newest 24 blocks
  newest at the left, after an outline of the block being made with what waits for it (a click: that chain's waiting
  transactions). The boxes are the explorer's ribbon's (§19, `BlockBox`), smaller (`StripBox`): a lid in the chain's
  colour and a side, two skewed faces, still. A box of Chains shows the sidechains it merged mined by their marks.
- **A box clicked opens its block under the strips**, in a panel of its own: a block of Chains as its page in Chains'
  explorer says it (`ChainsBlockDetail`: its facts, transactions, coinbase and drivechain messages, `BlockActivity`), a
  sidechain's facts and transactions from its node (`SideBlockDetail`, the host's new `chain.block`: what a page shows,
  checked; its peg and the block of Chains that merged mined it in its own explorer, a click away — embedding that
  explorer showed its lists first and the block below them); the panel's arrows and ← → go to
  the block at the left (newer) or right (older), as in the strip, Escape closes it; the hash can be copied; *Open in its
  explorer* goes to that block's page. The tables of the newest blocks and of the waiting transactions are gone from the
  page.
- **One chain**: that chain's explorer as before — its strip (the larger ribbon) on top — and now the block a box opens
  is said *under* the strip (Chains' explorer kept the strip only over its lists); a sidechain's own explorer has its own
  strip. Its table of blocks stays there, the way through a chain's whole history.
- **Light**: a strip is read in parts (`lib/strips.svelte.ts`): Chains at its tip (taken at most once a second) for the
  new blocks only (`explorer.blocks` with summaries, from the strip's head), a sidechain every 8 s for its newest three
  (`chain.activity` now takes a `count`, 1 to 30), more only when it went further; a box keeps its place (keyed by its
  hash), so a new block is one box more at the head; the ages follow every 30 s. The faces are still (no animation); in
  reduced effects (software GL) the boxes keep their faces, which cost nothing once painted (§21.3: painted without GL).

### 21.5 Transactions: the app's own page over every chain

User: "I don't like the combined view. I'd use a standard view (of Chains), but include transactions from all coins
selected and add a column to know which chain it is from — but keep the same search/features as a regular transaction
page."

- The page is the app's own Transactions page (its windowed table, columns, search, filters of type, dates and amount,
  sorting, totals and count, CSV, details, the menu of a row) over every chain the selector shows, with one **Chain**
  column (mark and name) when more than one chain is listed. The combined view (`ChainActivity`) is gone.
- Search, filters and sorting work on the merged list (the search also matches an amount: "1.5" finds 1.50000000); the
  CSV has a Chain column first when the rows are not all Chains'. A deposit reads "Deposit into Layers", a merged mining
  bid "Merged mining bid".
- A sidechain's row: its details in the app (read from its node, `chain.transaction`), *Open in Layers*, *Show in block
  explorer*, the copies; fee bumping, abandoning, the raw transaction, the label and the third-party links stay on
  Chains' rows.
- At the hub's scale (`lib/alltxs.svelte.ts`): Chains' rows are its wallet's records as kept (§20.8), never made again at
  a block; a sidechain's history is read once (up to 5 000 rows, `chain.transactions`' most is now 5 000), then its
  latest 100 every 10 s while shown and seen, merged by transaction (unchanged rows keep their object: their row is not
  drawn again), whole again every 5 minutes, at a change of wallet or a burst; a block of Chains never reads a
  sidechain's history again.

### 21.6 Tests

- **Pages** (vitest): Hivemind's group (Markets with Create under it, current with its tab, no Votecoin, Simple),
  Staking (a tab per chain that stakes beside the title, remembered, one chain alone, a chain that does not stake said,
  only the shown chain's frame laid in the page), the frames' state held while hidden and handed the newest once a
  second, a hidden sidechain's notices limited, a value followed at most once a period (`ThrottleHarness`), a text's
  direction set only when it changes, the strips (`strips.test.ts`: a new block one box more, the others kept, only the
  new ones read; a block opened under the strips, ← → and Escape; a sidechain's in its own explorer; no table of the
  newest blocks), the Transactions page over every chain (`transactions-all.test.ts`). Hivemind's own pages (its
  `hivemind.test.ts`: Markets' Create tab and its sub-tabs, the Voting center's Votecoin tab, one title each), the kit's
  headless module mode and SafeText (the template's `module-mode.test.ts`, `perf.test.ts`), Layers' and FastBlocks' fee
  split card.
- **Host** (`test_chains-app`): `-webuicompositing` (`appconfig_tests` `software_gl_paints_without_compositing`),
  `chain.activity`'s count and `chain.transactions`' 5 000 (`chainadapter_tests`).
- **End to end**: `run_integrated.py` with FastBlocks too (the Staking tabs, FastBlocks' and Layers' own pages, Simple
  and Light; Hivemind's group, Markets' Create, the Voting center and its Votecoin tab, Light and Simple; the explorer's
  strips, a block of Chains and one of Layers opened under them, ← →, Simple and Light, Chains alone with its block under
  its strip; the Transactions page with its Chain column, a search across chains, a sidechain's row's details);
  `run_startup.py`; `run_showcase.py --scale hub` (the numbers of §21.3 and the pictures at the hub's scale).

### 21.7 Before → after

Pictures in `media/unified-app/ux3/`: `e2e/` the end-to-end run (Hivemind's group and pages, Staking, the explorer's
strips, Transactions), `pages/` the hub-sized state pictured (Advanced and Simple, Midnight and Light), `perf/` the
numbers and their chart.

| Seen in §20 | Now |
|---|---|
| On the hub, WebKit at 117–213 % of a core after the start; a page of BitAssets 261 %, the explorer 140 % with the network alive | Every page under 5 % idle and under 15 % with the network alive, at the hub's sizes and window (§21.3) |
| Hivemind's group: Markets, Portfolio, Voting center, Decisions, Create, Votecoin, Payouts | Markets with Create under it (its tab), Portfolio, Voting center (Votecoin its tab, the wallet's voting power above both), Decisions, Payouts |
| Staking: summaries of every chain, the bond, unbond and staking switch behind "Bond, unbond, turn on…" | The chain's whole staking page under the title, a tab beside it for each chain that stakes |
| The explorer's main page: a table of the newest blocks of every chain and one of the waiting transactions | A strip of boxes for each chain, a block opening under the strips |
| Transactions with every chain: a separate list of 500 rows, no search, no filters | The app's own Transactions page over every chain, a Chain column, its search, filters, sorting, CSV and details |

## 22. Trade: the DEX as one page of the app (2026-10-21, branch `unified-dex`)

User: "a first-class DEX page in the unified wallet, in addition to the per-chain pages", working across every chain
with a DEX (Layers; Flush, the renamed Starbase on eCash, later; BitAssets' pools) and following the global selector.

### 22.1 Where it is (proposed and done: the Wallet section)

```
Sidebar (Advanced)               Trade                   [ Swap | Pools | Positions | Orders | Markets ]
  Home                           Trade on  [● Layers 39.80 CHN · 2 assets] [● BitAssets …]   ← tiles: chains with a DEX
  WALLET  Overview               ┌ You pay  [100      ] (GOLD ▾) ┐ ┌ Route ───────────────────────────────┐
          Send · Receive         │          ⇅                     │ │ GOLD → CHN → SILV                     │
          Trade        ← here    │ You receive ≈ 490.3 (SILV ▾)   │ │ ● GOLD / CHN  Constant product 0.30 % │
          Transactions           │ Rate · least received · impact │ │ ● CHN / SILV  Constant product 0.30 % │
          Manage wallets         │ Slippage [0.1][0.5][1][other]  │ │ Pools 2 · impact 0.9 % · fees 0.60 %  │
  CHAIN   …                      │ [Swap…] You confirm next.      │ │ Other routes …                        │
Simple: Features → Swap          └────────────────────────────────┘ └───────────────────────────────────────┘
```

- **The Wallet section, after Receive**, not a new MARKETS category: Trade acts on the user's coins on one chain as Send
  and Receive do; a section of one entry would be a box for a lone line (§20.3), and "Markets" is already Hivemind's
  word (its prediction markets). In the sidebar and the workspace's Wallet area only while a chain with a DEX is on (as
  Staking); the palette has it. **Simple mode**: "Swap" in its Features (with Staking: at most three, §20.1), the page
  is a simplified swap only.
- Each chain's own Trade page stays (its group in the sidebar, Assets → Trade): the complete set of what its DEX does
  (auctions, oracles); the app's page is the common part over every chain.

### 22.2 What each tab shows

| Tab | One chain selected | All | A chain without a DEX |
|---|---|---|---|
| **Swap** | its form (tiles in step with the selector) | the tiles ask which chain (each chain has its own pools) | "Thunder has no DEX", the chains that have one a click away |
| **Pools** | its pools: pair (logos), curve, fee tier, liquidity ≈ in its coin, volume since made, APR estimate, your share; a row opens the pool's page; *Create a pool* | every chain's, a Chain column | the same |
| **Pool page** | price (time-weighted averages, inverted at a click), what it holds, trades, fee, APR est., made in block, its id; your share; Add liquidity / Remove | (its chain) | — |
| **Create a pool** | a wizard: pair · kind (curve, fee tier, A, weights) · amounts (they set the first price) · review | asks the chain | — |
| **Positions** | the wallet's liquidity: pool, range ("Full range" for constant-product shares), share, worth, fees ("in what the shares are worth"), Manage, Collect (for concentrated positions, disabled) | every chain's | — |
| **Orders** | phase B, laid out: the order book, *Place a limit order* (Buy/Sell, price, amount, good until), your open orders; nothing can be sent | — | — |
| **Markets** | every pair: price, 24 h change and volume (phase C: a dash and a line saying so), liquidity, trades, fee; search by ticker or hash; sort by column | every chain's, a Chain column | — |

- **Swap**: what is paid and what comes, each with its logo and ticker (a list of what the chain's pools trade, with the
  wallet's balance of each, searchable); the chain's own quote a moment after typing, again every 15 s while shown; the
  route hop by hop (each pool's kind and fee tier, what comes out of it), the price impact (the route's price against
  the pools' prices before it, fees included; a warning from 3 %, a word from 10 %), the rate, the least received at the
  slippage (0.1, 0.5, 1 % or typed, remembered: `prefs.dexSlippage`, checked when read back), other routes to choose.
  More than held: still quoted (to see the price), not sent. Simple mode: the same swap, the route in one line
  ("Through CHN"), the default slippage, no route panel.
- **Estimates said as such**: liquidity (what a pool holds valued at the pools' prices in the chain's coin, "≈"), and the
  APR (its volumes since made times its fee, over what it holds, scaled to a year by the chain's own pace of blocks:
  "APR, est."; a pool younger than a day, or with no price in the coin: "–"). Amounts themselves are exact text.

### 22.3 The adapter: `chain.dex.*`

Capabilities (`ChainAdapter::Table()`, `dexFeatures` of `chain.capabilities`), never guessed from what a node answers;
what applies *now* is said by the chain's node (`chain.dex.info` from its `getdexinfo`):

| Chain | pair, liquidity | route | twap | curves (pool kinds) | orders (phase B) | stats (phase C) |
|---|---|---|---|---|---|---|
| Layers | ✓ | ✓ (from its DEX's height) | ✓ | when its node says they apply (dex-curves) | | |
| BitAssets | ✓ | | | | | |
| Flush (eCash, later) | — its row comes with its catalog entry on the `ecash` branch | | | | | |

| `chain.dex.*` | Reads (the chain's own pages' policy) | Writes (its question in the trusted dialog) |
|---|---|---|
| `info` | `getdexinfo` | — |
| `pools {asset?}` | `listpools` (200), `getblockcount` + two headers (its pace of blocks) | — |
| `quote {in, out, amount, maxHops}` | `getasset` (decimals), `quoteroute` (≤ 5 routes), else `quoteswap`; each hop's pool from `listpools` of its asset (kind, fee, reserves: the impact) | — |
| `chart {pool}` | `getpooltwap` over 24 windows ending now: the average of each of 24 equal parts of what the pool keeps | — |
| `positions` | `listmyassets` `liquidity` | — |
| `swap {in, out, amount, path, pools, slippage, minOut}` | the checks of §13.5 #38, `quote` again (its assets and pools), each pool by id | by id: `swappools [in, amount, [pools], slippage, min_out]`; by assets (only pools of the default kind): `swapasset` / `swaproute` |
| `addLiquidity {pool, a, b, amountA, amountB?, slippage?, setsPrice?}` | held amounts, the pool by its id | by id: `addpoolliquidity [pool, {a: amount, b: amount}, slippage]`; by assets (the pair's one pool): `addliquidity` |
| `removeLiquidity {pool, percent}` | the wallet's shares of that pool, the pool by its id | by id: `removepoolliquidity [pool, percent, null, slippage]`; by assets (one pool, one position of the pair): `removeliquidity` |
| `createPool {a, b, amountA, amountB, curve, fee, amp?, weightA?}` | the kind checked; `previewpool` (the new pool's id) | with pool kinds: `createpool [a, amount_a, b, amount_b, {curve, fee, amp\|weight_a}, slippage]`; without: constant product 0.30 % by `addliquidity … sets_price=true` |

- **Layers** (branch `unified-dex` of `layers`: `ux3`, then `dex-curves` at f951a9370f merged): its pages' policy reads
  `getdexinfo`, `quoteroute`, `getpooltwap`, `getpool` (by id), `previewpool`; its questions resolve every asset (by id;
  the coin as `CHN`) and every pool (by id) they show (`rpc_describeconfirmation_layers.py`: a route by its assets and by
  its pools' ids). Its pack is unchanged (pinned at `ux3`).
- **Pools by id** (the pool kinds of `dex-curves`): every spend names its pools by id once the chain's pool kinds apply
  (`features.byId`): `swappools`, `addpoolliquidity`, `removepoolliquidity`, `createpool {curve, fee (percent), amp |
  weight_a}`; before (or on BitAssets), by the assets only where they name the very pool shown (§13.5 #44). Pools are
  read with `curve`, `fee_millionths` (else `fee` in percent), `amp`, `weight_a`, `default_kind`; one of no tier is not
  shown (§13.5 #43). Requests name the coin `CHN`, assets `0x<hash>`.

### 22.4 Light (§20.6)

Only the chain the tab shows, only what the tab needs, only while the window is seen (`lib/dex.svelte.ts` `poll`:
`document.hidden` stops it, `visibilitychange` resumes): Swap its chain's pools every 20 s and a quote every 15 s while
an amount is typed; Pools and Markets the pools of the chains shown every 20 s; Positions every 30 s; a pool's page its
chart every minute. An answer that did not change is left in place (nothing drawn again); no per-tick recomputation (the
estimates are derived from the pools' answer); the chart is one still SVG path. The hub-scale check measures Trade
(Swap and Pools) among its pages: at the hub's state, window and way of drawing (virtual display, software GL), % of one
core, the app and its web process: Trade idle 0.7, Trade → Pools idle 0.7, Trade live 5.5 (budgets 5 and 25; the other
pages as §21.3: Home 1.1 idle, 7.7 live). Every page of the check passes.

### 22.5 Security

Rows #38–#42 of §13.5: host-built swaps from typed fields, quoted again and their route checked before anything is asked;
slippage bounds enforced by the chain (min_out at signing, refunds at clearing); the route the app checked shown in the
trusted dialog before the chain's own rows (which say it again from the request); liquidity and new pools checked the
same way; the new reads bounded, of the chain's own policy, never asking. The §13 path is unchanged: the chain's own
describer, the trusted dialog naming the chain first with its mark, the one-time token bound to the request.

### 22.6 Tests

- **Host** (`test_chains-app`, `chainadapter_tests`): `dex_capabilities_and_pure_parts` (the table, asset ids, slippage
  text, the least received, pool kinds, pool rows), `dex_reads_pools_info_quote_chart_positions` (info with and without
  pool kinds, pools checked, a quote's route with each pool's kind and its impact, decimals refused before the node is
  asked, an older node: pair quotes, the chart of 24 parts, the wallet's shares, a read that asks refused),
  `dex_swap_quoted_again_its_route_checked_and_shown` (swaproute and swapasset requests exactly, the app's rows, the price
  moved, a route not offered, an asset twice, more than held, a slippage out of bounds, a chain of one-pool swaps),
  `dex_liquidity_in_and_out_and_new_pools`; `sidechainpages_tests` `the_apps_own_rows_after_the_chain_before_the_daemons`
  (after the chain's name, before the daemon's rows, at most 16; a pack's request has none).
- **Layers** (`rpc_describeconfirmation_layers.py`, DEX from block 0): a route quoted through the pages' policy, its
  question "Swap through pools" with the route in words and each asset by label and hash, run with its token.
- **Pages** (vitest `dex.test.ts`, 18): exact amounts (least received, the second amount of liquidity, a share out),
  the estimates, the index searched and sorted, where Trade is (Wallet, Simple's Swap, only with a DEX), Swap (tiles of
  DEX chains, the quote, the route, other routes, slippage, the swap sent with exactly what was shown), more than held,
  privacy mode, All and a chain without a DEX, only the chain shown read, Simple mode, the pools, a pool's page with its
  chart, liquidity in and out, the wizard with and without pool kinds, positions over every chain, orders, markets.
  `layouts.test.ts`: Trade among the pages reached while a chain has it.
- **End to end** (`run_dex.py` + `dex.js`, Layers with its DEX from block 1): a pool made with the wizard (its question
  "On Layers" with the app's rows), a second one, a swap GOLD → CHN → SILV through both (the route shown, asked with the
  route the app checked and Layers' own, cleared in a block, at least what was shown received), liquidity in at the
  pool's price and half of it out from the pool's page, positions, the market index, orders, Simple's swap. `run_startup.py`.
- **Cost**: `run_showcase.py --scale hub` with Trade (Swap, Pools) among the pages measured.

### 22.7 Open

- **Pool kinds**: aligned to `dex-curves` at f951a9370f (§22.3); later changes of its RPCs to follow; concentrated liquidity (a range, in or out, fees to collect):
  the Positions columns are there, the data comes with the chains.
- **Orders** (phase B) and **24-hour figures** (phase C): laid out, wired when a chain's node offers them (`orders`,
  `stats` features).
- **Flush** (Starbase renamed, on eCash): its row of the table with its catalog entry on the `ecash` branch.
- **BitAssets** is mapped (pair quotes, one-pool swaps, liquidity) and unit-tested, not driven end to end here.
- A pool's figures follow its chain at the pace of the page's reads (20 s): just after a spend they are the block's before.
