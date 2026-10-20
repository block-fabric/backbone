# Backbone on the eCash networks

Backbone (this repository, formerly "Chains") is a full node, wallet and app for **eCash** (ECX), the drivechain fork of
Bitcoin made by ecash-com/bitcoin, with BIP300/301 enforced in the node as the
[bip300301_enforcer](https://github.com/LayerTwo-Labs/bip300301_enforcer) enforces them. The Chains (CHN)
networks are still here, for the sidechains, the test hub and the releases that use them; they will go in a
later step (see the end).

The product name is one constant, `CLIENT_NAME` in `CMakeLists.txt` ("Backbone"). The executables
(`backboned`, `backbone-cli`, `backbone-app`, `backbone-tx`, `backbone-util`, `backbone-wallet`), the data
directory, the configuration file and the user agent (`/Backbone:32.0.0/`) are derived from it.

## The networks

| `-chain=` | What | Message start | P2P / RPC | Fork height, bits | Replay locktime | Data (default datadir) |
|---|---|---|---|---|---|---|
| `ecash` (default) | eCash's main network (ecash-com/bitcoin tag `ecash-v1.0.0-core31.1`, branch `master`) | `eca5dcb7` | 8533 / 8532 | 973,728 (2016 × 483), `0x1901a6e1` | 499,999,998 | the root |
| `ecashbeta` | betanet, eCash's rehearsal (branch `betanet`), whose own client calls it main | `eca5b104` | 8533 / 8532 | 967,680 (2016 × 480), `0x19044b7e` | 499,999,999 | `betanet/` |
| `ecashtest` | eCash's test network: Bitcoin's testnet4 under eCash's rules (`CTestNet4Params` of ecash-com/bitcoin) | `eca5b124` | 48533 / 48532 | none (its chain is testnet4's) | 499,999,998 | `testnet4/` |
| `main`, `test`, `signet`, `regtest` | the Chains (CHN) networks, unchanged | as before | as before | — | — | Chains' datadir |

All eCash networks have Bitcoin's genesis (testnet4's for `ecashtest`), buried deployments, addresses
(`bc1…`/`tb1…`, base58 0/5/128), 10-minute blocks with the 2016-block retarget, 4 MWU blocks, coinbase
maturity 100, and Bitcoin Core v32's minimum chain work, assumevalid (block 966,143, before both forks) and
assumeutxo snapshots. Seeds: eCash's DNS seeds (`seed.ecash.*` for main, `seed.beta.*` for betanet; no
fixed seeds, as eCash).

**eCash's main network.** Its parameters are published (tag `ecash-v1.0.0-core31.1`, 2026-09-08; ecash.com
gives the fork for about 2026-10-31). Its DNS seeds did not resolve yet on 2026-10-20. The
bip300301_enforcer has **no preset for eCash's main network** (its latest, commit `17eb2ce`, has alphanet
and betanet): the drivechain thresholds of `ecash` are the enforcer's `Thresholds::MAINNET` (the BIP300
specification's: an unused slot activates with more than 1,815 acks in 2,016 blocks), with the fork height as
activation height and OP_NOP8. **They are provisional** and must be checked against the enforcer's mainnet
preset once LayerTwo Labs publishes one (follow-up 1).

**eCash's test network.** eCash runs no test network of its own: ecash-com/bitcoin's testnet3/testnet4 keep
Bitcoin's DNS seeds, which list Bitcoin nodes (another message start), and eCash's FAQ names betanet as the
network to test on. The eCash seed hosts do not listen on 18533/48533. So `ecashtest` is offered on
testnet4 (the current Bitcoin test network; testnet3 is not offered). Its chain is Bitcoin testnet4's, which
eCash's rules accept unchanged. Its replay locktime is that of eCash's release (499,999,998 on every network
of that client). With no enforcer preset for it, BIP300/301 starts at 161,280 (2016 × 80, above the
testnet4 history, which nobody scanned) with the enforcer's testnet thresholds (`Thresholds::SHORT`). To
run it with peers, see "Running the test network" below.

## Data directory and configuration

- The eCash networks: `~/.backbone` (Windows `%LOCALAPPDATA%\Backbone`, macOS
  `~/Library/Application Support/Backbone`), configuration `backbone.conf`; main at the root, betanet in
  `betanet/`, the test network in `testnet4/`, as eCash's own client lays them out.
- The Chains networks keep `~/.chains` and `chains.conf`.
- Which family is chosen from the command line (`-chain`, `-testnet`, `-signet`, `-regtest`; none: `ecash`).
  `chain=` in a configuration file then picks the network within that data directory.
- In any data directory the file read is the one it has: `backbone.conf`, else `chains.conf`, else
  `ecash.conf`. So an existing eCash datadir works as it is (`-datadir=~/.ecash`, its `ecash.conf` is read;
  main at its root, testnet4 in `testnet4/`). A betanet datadir of eCash's betanet client has betanet at its
  root: link it as `<datadir>/betanet` or use it with `-chain=ecashbeta -datadir=` on a parent that has it.
- The block files of eCash's client are readable as they are (same message start). One explicit `-datadir`
  shared by Chains' main and eCash's main is not supported (both use the root).
- The app (`backbone-app`) asks for the network at the first start (eCash Mainnet, Betanet, Testnet; the
  Chains networks with `-devnetworks`) and keeps the choice, unless the command line names one.

## What the RPC interface reports

`getblockchaininfo` and `getmininginfo` report as `chain` what eCash's node reports — `main` on `ecash` and
`ecashbeta`, `testnet4` on `ecashtest` — because the bip300301_enforcer, DATUM and the explorers key on it;
the new field `network` has our name (`ecash`, `ecashbeta`, `ecashtest`, or the Chains name). Amounts and fee
rates are in ECX on the eCash networks (`CURRENCY_UNIT`), in CHN on the Chains networks.

## The eCash rules (as ecash-com/bitcoin)

| Rule | Where |
|---|---|
| Difficulty reset: the retarget into the fork height gives the fork bits (before the limit); a fork block with other bits is `bad-diffbits-ecash-da`. The fork height must be a retarget boundary (asserted): elsewhere the retarget does not run and no block could be valid. Headers presync admits the reset. | `src/pow.cpp` (`CalculateNextWorkRequired`, `PermittedDifficultyTransition`), `src/validation.cpp` (`ContextualCheckBlockHeader`), `src/kernel/chainparams.cpp` |
| Replay protection: a transaction with the network's magic nLockTime is final at any height; it fails every `OP_CHECKLOCKTIMEVERIFY` (`SCRIPT_VERIFY_REPLAY_LOCKTIME`, `_BETANET`). Stock Bitcoin never confirms it (with an input that is not final). | `src/consensus/tx_verify.cpp`, `src/script/interpreter.cpp` |
| `OP_DRIVECHAIN` = `OP_NOP8` (0xb7): a script of four bytes starting with it succeeds with 0xDC on the stack, whatever its other bytes, as a scriptPubKey, redeem script, witness script or tapleaf, at every height (`SCRIPT_VERIFY_DRIVECHAIN_NOP8`, set for every block, the exception blocks too); `OP_NOP5` is a NOP. Such scripts are standard to create and spend. The treasury scripts are `OP_NOP8 0x01 <slot> OP_TRUE`. | `src/script/interpreter.cpp`, `src/validation.cpp` (`GetEcashScriptFlags`), `src/policy/policy.cpp`, `src/drivechain/sidechain.cpp` (`EscrowOpcode`) |
| Repurpose transactions: the 232 txids of each network (copied byte for byte from `src/repo_txns.h` of the `master` and `betanet` branches) are accepted, in blocks and in the mempool, without their input scripts being checked; everything else is checked. Each network has only its own list. | `src/kernel/ecash_repurpose.cpp`, `src/validation.cpp` |
| getblocktemplate: a request must acknowledge the `bip300301` rule (or use the `drivechain`/`coinbasetxn` capability, whose clients put this node's `drivechain_coinbase_outputs` in their coinbase), unless `-deprecatedrpc=getblocktemplate`; the reply has `!bip300301`. | `src/rpc/mining.cpp` |

## BIP300/301 on the eCash networks

`Consensus::DrivechainParams::rules` = `RULES_ENFORCER`: the node checks blocks as the bip300301_enforcer does
(commit `17eb2ce`, the one betanet runs; `lib/validator/task/mod.rs connect_block`, `lib/messages.rs`,
`lib/validator/dbs/diff.rs`), in `SidechainDB::ConnectBlockEnforcer` (`src/drivechain/scdb.cpp`):

- No message, deposit or withdrawal is read below the activation height (the fork height).
- The coinbase messages are handled in output order, each seeing what the ones before it did. Two M1 of the
  same proposal, two M2 for a slot, two M4, two M7 for a slot make the block invalid.
- M1: any description (version 0 or not); a proposal already collecting acks is not made again.
- M2: an ack of the proposing block does not count; the ack that takes a proposal over the threshold within
  the maximum age activates it (an unused slot: more than 1,008 acks within 2,016 blocks on betanet; a used
  one: more than 13,150 within 26,300). The sidechain replaced keeps its treasury and bundles. A proposal
  fails older than the maximum age, or when it can no longer make the threshold.
- M3: for an active slot only (else the block is invalid), not a bundle already pending; several per slot.
- M4: the one and two byte forms (two bytes only with a value above 253: an abstain or an alarm counts),
  `LeadingBy50`, `RepeatPrevious` (what the previous block did). Upvoting a bundle downvotes the others of
  its slot with votes; the alarm downvotes them all.
- Bundles fail older than 26,300 blocks; a withdrawal (M6) pays one with more than 13,150 votes, counting the
  block's own votes; only that bundle leaves.
- M5: deposits to several sidechains in one transaction, any destination; a treasury output of an inactive
  slot, or one in a coinbase, is an ordinary output.
- M7 for any slot; an M8 needs its slot's M7 and the parent block, one per slot.

Chains' own safety rules are **not consensus on eCash** (they would fork the node off the enforcer's chain).
They are kept as **policy of the block template** (`drivechain::MinerState::CreateBlockAdditions`): the vote
message is first in the coinbase; no bundle is upvoted or paid out that was paid out before, or that was
pending when another bundle of its slot was paid out (single payout, paid-bundle memory); no bundle is
proposed to a full queue (the pending cap); the vote message is in the canonical form. Idle and upvote expiry
have no policy form beyond never upvoting what the sidechain node does not vouch for.

## The wallet

- **Seed and keys.** Bitcoin Core's: coin type 0' on `ecash` and `ecashbeta`, 1' on `ecashtest`
  (`IsTestChain`). A Bitcoin wallet's keys are eCash keys: pre-fork coins are spendable on eCash.
- **Replay protection, on by default** (`-walletreplayprotection`, `src/wallet/spend.cpp`
  `ApplyReplayProtection`): new transactions (sendtoaddress, send, sendall, fundrawtransaction and the PSBT
  calls with locktime 0) have the magic locktime in place of anti-fee-sniping, with inputs that are not final
  (the wallet's RBF sequence). Not when a locktime is asked for, when every input is final, or when an input
  spends a timelocked script (miniscript `after()`, `OP_CHECKLOCKTIMEVERIFY`), which the magic locktime would
  fail: then anti-fee-sniping, with a warning in the log. A fee bump keeps the locktime.
- **Replay is one way.** Nothing makes a Bitcoin transaction invalid on eCash: moving a coin that was never
  split on Bitcoin moves it on eCash too (eCash's `betanet-bridge` seed nodes replay them on purpose).

### Splitting coins (design)

A coin is *split* when an eCash transaction with the magic locktime spent it, or it descends from one, or it
was made after the fork; a split coin no longer exists on Bitcoin, so nothing replays. Today the split is
`sendall` to an address of the same wallet (it gets the magic locktime): `backbone-cli -rpcwallet=w sendall
'["'$(backbone-cli -rpcwallet=w getnewaddress)'"]'` (tested in `wallet_ecash_replay.py`). The helper to
build (follow-up 6):

1. `splitcoins [outpoints]`: one transaction per wallet (or per batch of inputs) to a fresh internal address,
   with the magic locktime; refuse (rather than fall back) if it cannot have it (timelocked coins are listed
   apart, to be split once their lock has passed, with an explicit locktime ≥ the lock and a protected child).
2. A split flag per coin in `listunspent`/the app (spent by a protected transaction, a descendant of one, or
   created above the fork height), and "unsplit" warnings on the overview.
3. Drivechain transactions (deposits M5, BMM requests M8) funded only from split coins: a replayed deposit
   puts real BTC into an anyone-can-spend output on Bitcoin.
4. The app: a one-time "Split my coins" step after the fork, and the claim of pre-fork coins by importing a
   Bitcoin wallet (descriptors, 0'), with a loud warning that those keys hold real BTC.

## Running the test network

eCash's test network has no eCash peers; its chain is Bitcoin testnet4's. A node fetches it from Bitcoin's
testnet4 nodes through a relay that rewrites the message start of every message
(`magicproxy.py LOCAL_PORT:HOST:48333 …`, eca5b124 ↔ 1c163f28, nothing else changed):

```
python3 magicproxy.py 49210:<testnet4 node>:48333 49211:<another>:48333 &
backboned -datadir=~/.chains-ecash -chain=ecashtest -connect=127.0.0.1:49210 -connect=127.0.0.1:49211 -listen=0 -rpcport=49202
```

(`seed.testnet4.wiz.biz` and `seed.testnet4.bitcoin.sprovoost.nl` list testnet4 nodes.) Synced on
2026-10-20 to height 157,123 in about 20 minutes (14 GB), with the block hashes of mempool.space/testnet4.

## Tests

- `src/test/ecash_tests.cpp`: parameters, the reset at the retarget boundary and the first retarget after it,
  the magic locktime with CLTV for both values, OP_NOP8 against OP_NOP5 per network (bare, P2SH, P2WSH,
  tapleaf), the repurpose lists per network, the enforcer's rules and the template policy.
- `test/functional/feature_ecash_fork.py`: a node with Bitcoin's rules and one with eCash's (`-testecash
  -testecashfork=288@207fffff`, both `-testpowretarget`) split at the fork height; replay, magic locktime,
  repurpose (`-testrepurposetx`), OP_NOP8, a reorg across the fork, the bip300301 rule.
- `test/functional/wallet_ecash_replay.py`: the wallet's defaults, a timelocked coin, the split.

## Follow-ups

1. eCash main network: the enforcer's mainnet preset (thresholds, activation height, opcode) when published;
   the main network's seeds once they resolve; a fork-day runbook (rewrite of a Bitcoin datadir's magic or
   eCash's fork-point snapshot; the drivechain database with assumeutxo is untested).
2. eCash's open pull requests: BIP94 on mainnet from the fork height (PR 4), ASERT (PR 2), a snapshot at
   967,000 (PR 3): follow them if merged.
3. Live validation of the enforcer rules: a shadow run beside the bip300301_enforcer on a server (not this
   PC) on betanet from 967,680, comparing sidechains, CTIPs and bundle votes at every block; the
   differential test against the enforcer on an eCash regtest.
4. Sidechains on eCash: slots that do not collide with LayerTwo Labs' (2 BitNames, 4 BitAssets, 9 zSide);
   their mainchain wiring for `-chain=ecash…` (they look for `chainsd`/`chains-cli` and the Chains ports);
   deposits and BMM against backboned on eCash; the template without the single-payout assumption and with
   constants retuned for 10-minute blocks (audit 2026-10-19 §3); the app's sidechain supervisor (no network
   arguments for the eCash networks yet).
5. Policy constants still Chains' on eCash: `MAX_STANDARD_TX_WEIGHT` (392,000 against Bitcoin's 400,000)
   and the cluster size; per network if relay parity matters.
6. The split-coins helper above; the app's remaining "CHN" texts (first steps, tools, multisig) and the
   `chains:` URI scheme (eCash's Electrum uses `ecx:`).
7. Renames outside this repository: the GitHub repositories (block-fabric/backbone, block-fabric/backbone-<sidechain>),
   the unified-app supervisor, `~/skydoge/tools/build.sh` users, the release scripts (`chainsd.exe`…), the
   testnet tooling (`chainsd -testnet`), the explorer, the sidechains' mainchain client.
8. Removal of the Chains networks once nothing uses them: the sidechains' CHN mainchain, the test hub
   (`~/skydoge/testnet`), the CHN releases, the explorer of the CHN test network, Chains' chainparams,
   aserti3, the 800k/400k weights, maturity 360, the Chains drivechain rules (`RULES_CHAINS`) and their tests.
