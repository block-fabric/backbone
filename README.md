# Backbone

Backbone is a full node, wallet and app for **eCash** (ECX), the drivechain fork of Bitcoin
([ecash-com/bitcoin](https://github.com/ecash-com/bitcoin)). It enforces BIP300 (sidechain escrows and
withdrawals) and BIP301 (blind merged mining) inside the node, the way LayerTwo Labs'
[bip300301_enforcer](https://github.com/LayerTwo-Labs/bip300301_enforcer) does next to an eCash node, so no
separate enforcer is needed. It also serves the enforcer's gRPC interface, so sidechain software made for
"eCash node + enforcer" connects to Backbone unchanged.

- eCash's main network forks from Bitcoin at block **973,728**. Backbone follows eCash's current release
  (`ecash-v1.1.0-core31.1`), including its dampened 288-block retarget from the fork.
- Bitcoin's history and addresses (`bc1…`), 10-minute blocks, ECX as the coin.
- Based on Bitcoin Core v32.

The details of each network, the rules and the enforcer interface are in [doc/ecash.md](doc/ecash.md).

## The sidechains

Every sidechain holds ECX that came in by a deposit on eCash, and sends it back by a withdrawal that eCash
miners vote through. Each sidechain has its own node and app:

| Sidechain | Slot | What it is |
|---|---|---|
| [Flush](https://github.com/block-fabric/backbone-flush) | 46 (betanet: 99, temporary) | Tokens, assets and a DEX on a fast chain ([flush.markets](https://flush.markets)) |
| [Thunder](https://github.com/block-fabric/backbone-thunder) | 9 | Payments at volume, on large blocks |
| [BitNames](https://github.com/block-fabric/backbone-bitnames) | 2 | Names you own like coins |
| [BitAssets](https://github.com/block-fabric/backbone-bitassets) | 4 | User-created assets, pools and auctions |
| [zSide](https://github.com/block-fabric/backbone-zside) | 98 | Private payments with Zcash's Orchard |
| [Hivemind](https://github.com/block-fabric/backbone-hivemind) | 13 | Prediction markets |
| [Coinshift](https://github.com/block-fabric/backbone-coinshift) | 255 | Swaps with other chains |
| [FastBlocks](https://github.com/block-fabric/backbone-fastblocks) | — | A template for fast chains of your own |

Thunder, BitNames, BitAssets, zSide, Hivemind and Coinshift are drop-in C++ nodes
([backbone-core](https://github.com/block-fabric/backbone-core)), consensus-compatible with LayerTwo Labs' clients.
On the main network, a sidechain exists once its slot is proposed and activated there.

## Installing

Builds for Linux (x86-64) and Windows (10 or 11, 64-bit) are on the
[releases page](https://github.com/block-fabric/backbone/releases): `backbone-<version>-x86_64-linux-gnu.tar.gz`,
`backbone-<version>-win64-setup.exe` and `backbone-<version>-win64.zip`, with `SHA256SUMS`.

- **Linux:** unpack, check `sha256sum -c SHA256SUMS`, and run what is in `bin/`. The node needs `libzmq5`. The app,
  `backbone-app`, also needs the system's web view (on Ubuntu 24.04:
  `sudo apt install libzmq5 libgtk-3-0 libwebkit2gtk-4.1-0 libsqlite3-0`).
- **Windows:** run the installer, or unpack the zip. The app uses the WebView2 runtime that comes with Windows.

## Building from source

On Debian or Ubuntu (24.04):

```sh
sudo apt install build-essential cmake pkgconf python3 libevent-dev libboost-dev \
    libsqlite3-dev libzmq3-dev libgtk-3-dev libwebkit2gtk-4.1-dev nodejs npm
git clone https://github.com/block-fabric/backbone.git
cd backbone
cmake -B build -DWITH_ZMQ=ON
cmake --build build -j$(nproc)
```

- The first build runs `npm ci` for the interface (Svelte, TypeScript, Vite), so it needs the npm registry once.
- Add `-DBUILD_WEBUI=OFF` (and leave out GTK, WebKitGTK and npm) for a node without a window.
- Windows builds are cross-compiled with the depends system: `cmake --build build_win --target deploy-app` makes
  the zip and the installer. See [doc/webui.md](doc/webui.md) and [doc/build-windows.md](doc/build-windows.md);
  other systems and options are in the other `doc/build-*.md`.

The programs, in `build/bin`:

| Program | What it is |
|---|---|
| `backbone-app` | the node and the wallet, with their window (an HTML interface in a native web view) |
| `backboned` | the node, without a window |
| `backbone-cli` | commands for a running node or wallet |
| `backbone-wallet`, `backbone-tx`, `backbone-util` | tools that work without a node |

Tests: `ctest --test-dir build` and `build/test/functional/test_runner.py`.

## Running it

```sh
backbone-app                     # eCash's main network (the default)
backbone-app -chain=ecashbeta    # betanet, eCash's rehearsal network
backboned -chain=ecashtest       # eCash's rules on Bitcoin's testnet4, without a window
```

Data and the configuration file `backbone.conf` live in `~/.backbone` (Windows: `%LOCALAPPDATA%\Backbone`;
macOS: `~/Library/Application Support/Backbone`). Betanet's data is in `betanet/` and the test network's in
`testnet4/`.

| Network | `-chain=` | P2P port | RPC port |
|---|---|---|---|
| eCash main network | `ecash` | 8533 | 8532 |
| betanet | `ecashbeta` | 8533 | 8532 |
| test network | `ecashtest` | 48533 | 48532 |

Peers come from eCash's DNS seeds. A node that finds none can be given one with `-addnode=<host>:8533`.

**Sidechain nodes** reach Backbone in one of two ways:
- the enforcer's gRPC interface, on 127.0.0.1:50051 (`-grpc`, `-grpcport`);
- JSON-RPC, with the cookie file in the data folder: run Backbone with `server=1`.

For ZMQ (`-zmqpub…`), use a build with ZMQ (the releases have it). For `-txindex`, keep the full chain:
`-txindex` doesn't work with pruning.

## Sidechains in the wallet

- **Sidechains** page:
  - the active sidechains and the proposals;
  - deposits to a sidechain's deposit address (`s<slot>_<address>_<checksum>`, checked before anything is sent);
  - the withdrawal bundles and how this node votes on them.
- **Sidechain Nodes** page: install, start and stop sidechain nodes. Running a program always asks first, in a
  dialog of the desktop.

## Commands

Drivechains, on the node: `listactivesidechains`, `getsidechain`, `listsidechainproposals`,
`createsidechainproposal`, `acksidechain`, `removesidechainproposal`, `listsidechaindeposits`,
`getsidechainevents`, `listwithdrawalbundles`, `getwithdrawalbundle`, `receivewithdrawalbundle`,
`sendwithdrawalbundle`, `setwithdrawalvote`, `setdefaultwithdrawalvote`, `setsidechainackpolicy`,
`getvotepolicy`, `verifybmm`, `getdrivechaininfo`, `getaveragefee`.

In the wallet:
- `createsidechaindeposit`: deposit to a sidechain;
- `createbmmrequest`: pay miners to commit to a sidechain block (sidechain nodes do this themselves).

Mining: `getblocktemplate` carries the drivechain messages in the coinbase.

## Drivechains on eCash

- **Activation.** A sidechain is proposed in a slot (`createsidechainproposal`, M1) and activates once enough
  blocks ack it (M2).
  - **Main network:** more than 1,815 acks within 2,016 blocks for a free slot; 13,150 within 26,300 for a slot
    already in use.
  - **Betanet:** more than 1,008 for a free slot.
- **Deposits** go into the slot's escrow output; the sidechain creates the coins.
- **Withdrawals.** A sidechain node hands Backbone the bundle it wants paid (`receivewithdrawalbundle`). Miners
  vote on it (M4), and it pays out (M6) once it has more than 13,150 votes within 26,300 blocks.
- **Default votes on the main network:**
  - Backbone upvotes only the bundles its own sidechain node handed it (verified) and abstains elsewhere.
  - It acks only proposals for a free slot, never one that would replace a sidechain in use.
  - Betanet is more permissive. `getvotepolicy` shows each vote and why; the options and RPCs above change them.
- **Blind merged mining** (BIP301). A sidechain block exists only if an eCash block commits to it; the sidechain
  pays the miner a fee for that, in ECX.
- The messages (M1 to M8) and the escrow script use the byte layouts of the `bip300301_enforcer`, so pools and
  tools made for it read Backbone's blocks.

## Credits

Backbone is based on [Bitcoin Core](https://github.com/bitcoin/bitcoin) v32; its own README is in
[doc/README-bitcoin-core.md](doc/README-bitcoin-core.md). eCash's rules follow ecash-com/bitcoin. Drivechains are
Paul Sztorc's design (BIP300 and BIP301); the message layouts and the gRPC interface follow LayerTwo Labs.

## License

MIT: see [COPYING](COPYING).
