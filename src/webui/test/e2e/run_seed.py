#!/usr/bin/env python3
# Copyright (c) 2026 The Chains developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end test of the single seed (doc/unified-app.md §23): one backup phrase for the app's chain and its sidechains.

Three runs of chains-app on regtest, each in a virtual display, with seed.js in its page and Layers and Thunder from
--sidechain-bindir:

  1. The first start (no wallet): the guide opens by itself; the phrase is made, shown, checked and given a passphrase in
     the app's own dialog window (the page script reads the words from that window, as a user reads them from the screen);
     Layers and Thunder picked and turned on; each chain's wallet made from the phrase. The runner then checks, apart from
     the app's code (seedref.py, and each node's own descriptors), that every chain's addresses come from the phrase: the
     first address of each chain's account is the wallet's, with its path, and each wallet holds its own account only;
     that a sidechain's wallet is encrypted with its own passphrase made from the seed, never the user's. Coins are moved
     into Layers and Thunder (the user's passphrase asked by the app, the chains' own given to them), a payment made on
     Layers, the passphrase changed, the phrase shown again. The balances of the three chains are noted.
  2. The phrase restored into a fresh data folder (the chains' blocks copied, no wallet, no seed.dat, no settings): the guide
     restores it, Layers and Thunder are turned on, every chain is read again, and each balance is the one noted.
     Then merged mining with the main wallet locked (§23.12): the daemon's own bids refused, said plainly in the page; a
     mining wallet made from the phrase (account 1), topped up from the main wallet; the runner mines and checks that the
     daemon's bids are paid by the mining wallet while the main wallet stays locked and pays nothing.
  2. The phrase restored into a fresh data folder (the chains' blocks copied, no wallet, no seed.dat, no settings): the guide
     restores it, Layers and Thunder are turned on, every chain is read again, each balance is the one noted, and the
     runner checks the accounts again with this version's labels.
  3. Wallets of before (a wallet "main" with coins, made before the phrase): the offer opens by itself, once; a phrase is
     made, its wallet is "seed", the coins are moved by a payment to yourself, and the old wallet's keys are as they were.
  4. Without a password (§23.11): said plainly before it is made; nothing asked after the words; the runner checks seed.dat
     says so and every wallet of the phrase is kept open (the app's own wallet's passphrase made from the seed too);
     deposits and payments asked without a password; zSide (when zsided is in a --sidechain-bindir) with its own wallet, its
     notice, and its own backup written by its node through the app's chooser (written over the second time); a password
     added (every wallet locked, then asked) and removed (open again).

    run_seed.py --app build_app/bin/chains-app --sidechain-bindir <layers, thunder (and zside) bin> --tmp <dir> --shots <dir>

Nothing outside --tmp is touched; the nodes use ports of their own (--port); only processes started here are stopped.
The words of the test's phrase are written to the test's own log (a throwaway regtest wallet): the runner needs them.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run_smoke import App, check_shutdown, environment, ports_taken, script  # noqa: E402
from run_unified import cli, daemons  # noqa: E402
from run_packs import SIDECHAINS, bmm  # noqa: E402
import seedref  # noqa: E402

STEMS = ("layers", "thunder")
#: zSide's place in the catalog (supervisor.cpp): its ports are the base + 2 * 5.
ZSIDE_INDEX = 5
#: FastBlocks and BitNames, their keys from the phrase too when their programs are given (doc §25): their places.
EXTRA_INDEX = {"fastblocks": 1, "bitnames": 3}


def run(args, base, name, datadir, prelude, programs, ports, handle, failures):
    """One run of the app: its checkpoints handled, its result."""
    env = environment(base / name)
    page = base / name / "seed.js"
    page.write_text(script("seed.js", prelude))
    rpc_port, sc_base = args.port + 1, args.port + 10
    app = App(args.app, ["-regtest", f"-port={args.port}", f"-rpcport={rpc_port}", "-listen=0", "-dnsseed=0", "-fixedseeds=0",
                         "-fallbackfee=0.0002", f"-datadir={datadir}", f"-webuiconfig={base / name / 'chains-app.json'}",
                         *[f"-sidechainbindir={d.resolve()}" for d in args.sidechain_bindir], f"-sidechainportbase={sc_base}",
                         "-sidechainarg=-listen=0", "-sidechainarg=-fallbackfee=0.0002",
                         f"-webuitestshots={args.shots.resolve()}", f"-webuitestscript={page}"], env, base / f"{name}.log")
    try:
        while True:
            message = app.wait_event("CHECKPOINT", 1800)
            if message is None:
                break
            app.events.pop("CHECKPOINT", None)
            parts = message.split(" ", 3)
            what, stem = parts[1], parts[2]
            rest = parts[3] if len(parts) > 3 else ""
            try:
                if what == "bmm":
                    bmm(args.app.parent / "chains-cli", programs[stem], datadir, rpc_port, stem, ports[stem], int(rest or "1"))
                else:
                    handle(what, stem, rest, datadir, rpc_port)
            except (RuntimeError, subprocess.TimeoutExpired, KeyError, ValueError, TypeError) as e:
                failures.append(f"{name}: {what} {stem}: {e}")
            # Done: the page waits for the node's network to be on again (it turned it off before asking). The words come
            # before any sidechain is on: no RPC server yet, and the page does not wait for them.
            if what != "words":
                try:
                    cli(args.app.parent / "chains-cli", [f"-datadir={datadir}", "-regtest", f"-rpcport={rpc_port}", "setnetworkactive", "true"])
                except (RuntimeError, subprocess.TimeoutExpired) as e:
                    failures.append(f"{name}: the page could not be told {what} was done: {e}")
            if "RESULT" in app.events:
                break
        code = app.wait(900)
    finally:
        app.stop()
    result = app.result()
    if code != 0 or not result or result["failures"]:
        failures.append(f"{name}: exit {code}, {result['failures'] if result else 'no result'}; the page last said: {app.last}")
    check_shutdown(datadir, failures)
    time.sleep(1)
    for dpid in daemons(datadir):
        failures.append(f"{name}: a sidechain daemon was left running ({dpid}): stopped")
        os.kill(dpid, 15)
    if result:
        missing = [s for s in result["shots"] if not (args.shots / f"{s}.png").is_file()]
        if missing:
            failures.append(f"{name}: missing pictures: {missing}")
        print(f"  {name}: {result['passed']} checks passed, {len(result['shots'])} pictures", flush=True)
    return result


def copy_chains(src, dst):
    """The chains' blocks and state of a data folder, without a wallet, seed.dat, the nodes' settings or what is in flight."""
    skip_names = {"wallets", "seed.dat", "settings.json", "mempool.dat", ".lock", "mainchain.rpcpass", ".cookie", "debug.log", "supervisor.log", "peers.dat", "anchors.dat"}

    def ignore(folder, names):
        return [n for n in names if n in skip_names or n.startswith("seed.dat")]

    shutil.copytree(src, dst, ignore=ignore)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--sidechain-bindir", required=True, type=Path, action="append", help="folders with layersd and thunderd")
    parser.add_argument("--tmp", required=True, type=Path, help="folder for the data directories (made anew)")
    parser.add_argument("--shots", required=True, type=Path, help="folder for the pictures")
    parser.add_argument("--port", type=int, default=49520, help="ports of Chains: port, port+1 (RPC); the sidechains from port+10")
    parser.add_argument("--only", default="", help="runs to make (first,restore,migrate,nopass; default all)")
    args = parser.parse_args()

    if shutil.which("xvfb-run") is None:
        print("xvfb-run is needed", file=sys.stderr)
        return 2
    programs = {}
    for stem in STEMS:
        found = next((d for d in args.sidechain_bindir if (d / f"{stem}d").is_file() and (d / f"{stem}-cli").is_file()), None)
        if not found:
            print(f"no {stem}d in {args.sidechain_bindir}", file=sys.stderr)
            return 2
        programs[stem] = found / f"{stem}-cli"
    rpc_port, sc_base = args.port + 1, args.port + 10
    ports = {stem: sc_base + 2 * SIDECHAINS[stem][1] for stem in STEMS}
    extra = []
    for stem, index in EXTRA_INDEX.items():
        found = next((d for d in args.sidechain_bindir if (d / f"{stem}d").is_file() and (d / f"{stem}-cli").is_file()), None)
        if found:
            programs[stem] = found / f"{stem}-cli"
            ports[stem] = sc_base + 2 * index
            extra.append(stem)
    zside_dir = next((d for d in args.sidechain_bindir if (d / "zsided").is_file() and (d / "zside-cli").is_file()), None)
    if zside_dir:
        programs["zside"] = zside_dir / "zside-cli"
        ports["zside"] = sc_base + 2 * ZSIDE_INDEX
    taken = ports_taken([args.port, rpc_port] + [p for s in ports.values() for p in (s, s + 1)])
    if taken:
        print(f"ports {taken} are taken: give another --port", file=sys.stderr)
        return 2
    base = args.tmp.resolve()
    if base.exists():
        shutil.rmtree(base)
    base.mkdir(parents=True)
    args.shots.mkdir(parents=True, exist_ok=True)
    chains_cli = args.app.parent / "chains-cli"
    only = set(args.only.split(",")) if args.only else {"first", "restore", "migrate", "nopass"}
    failures = []
    noted = {}

    def chains(datadir, *a):
        return cli(chains_cli, [f"-datadir={datadir}", "-regtest", f"-rpcport={rpc_port}"] + list(a))

    def side(stem, datadir, *a):
        return cli(programs[stem], [f"-datadir={datadir}/sidechains/{stem}/data", "-regtest", f"-rpcport={ports[stem]}"] + list(a))

    def descriptor(run_cli, text):
        return text + "#" + run_cli("getdescriptorinfo", text)["checksum"]

    def handle(what, stem, rest, datadir, port):
        if what == "words":
            noted["words"] = rest.split()
            print(f"  the phrase of the test: {len(noted['words'])} words", flush=True)
        elif what == "addresses":
            # Apart from the app's code: the seed of the words (seedref.py), each chain's account, each node's descriptors.
            stems = ("layers",) if rest == "nopass" else STEMS + tuple(extra)
            seed = seedref.bip39_seed(" ".join(noted["words"]))
            m = seedref.master(seed)
            fp = seedref.fingerprint(m)
            accounts = {"chains": 0}
            for s in stems:
                genesis = side(s, datadir, "getblockhash", "0")
                accounts[s] = seedref.account(s, genesis)
                noted[f"genesis:{s}"] = genesis
                if accounts[s] < seedref.APP_ACCOUNTS:
                    failures.append(f"{s}: its account {accounts[s]} is one of the app's own chain")
            for chain, account in accounts.items():
                run_cli = (lambda *a: chains(datadir, *a)) if chain == "chains" else (lambda *a, s=chain: side(s, datadir, *a))
                k = seedref.path(m, 86 + 2**31, 1 + 2**31, account + 2**31)
                desc = descriptor(run_cli, f"tr([{fp}/86h/1h/{account}h]{seedref.xprv(k, 'test')}/0/*)")
                first = run_cli("deriveaddresses", desc, "[0,1]")
                info = run_cli("-rpcwallet=main", "getaddressinfo", first[0])
                path = str(info.get("hdkeypath", "")).replace("'", "h")
                if not info.get("ismine") or path != f"m/86h/1h/{account}h/0/0" or info.get("hdmasterfingerprint") != fp:
                    failures.append(f"{chain}: the first address of its account {first[0]} is not its wallet's: {info}")
                else:
                    print(f"  {chain}: account {account}, {first[0]} is its wallet's ({path}, {rest or 'first'})", flush=True)
                # Its wallet holds its own account only (four descriptors: tr and wpkh, receive and change).
                listed = run_cli("-rpcwallet=main", "listdescriptors")["descriptors"]
                origins = sorted({d["desc"].replace("'", "h").split("]")[0] + "]" for d in listed})
                want = sorted({f"tr([{fp}/86h/1h/{account}h]", f"wpkh([{fp}/84h/1h/{account}h]"})
                if len(listed) != 4 or origins != want:
                    failures.append(f"{chain}: its wallet's descriptors are not those of its account alone: {origins}")
            # A sidechain's wallet is encrypted with its own passphrase made from the seed: the user's does not open it. Its
            # passphrase checked by changing it to itself (its lock as it was: without a password the app keeps it open).
            for s in stems:
                own = seedref.wallet_passphrase(seed, s, noted[f"genesis:{s}"])
                try:
                    side(s, datadir, "-rpcwallet=main", "walletpassphrase", "correct horse", "1")
                    failures.append(f"{s}: the user's own passphrase opened its wallet")
                except RuntimeError:
                    pass
                side(s, datadir, "-rpcwallet=main", "walletpassphrasechange", own, own)
                print(f"  {s}: its wallet's passphrase is its own made from the seed, not the user's", flush=True)
            if rest == "nopass":
                # Without a password the app's own wallet has one made from the seed too (an empty stem, its chain's genesis).
                own = seedref.wallet_passphrase(seed, "", chains(datadir, "getblockhash", "0"))
                chains(datadir, "-rpcwallet=main", "walletpassphrasechange", own, own)
                print("  chains: without a password, its wallet's passphrase is made from the seed", flush=True)
        elif what == "protection":
            # Whether seed.dat has a password, and whether every wallet of the phrase is kept open (none) or locked (one).
            want_open = rest == "open"
            until = time.time() + 90
            while True:
                header = json.loads((datadir / "regtest" / "seed.dat").read_text())
                infos = [chains(datadir, "-rpcwallet=main", "getwalletinfo"), side("layers", datadir, "-rpcwallet=main", "getwalletinfo")]
                opened = [i.get("unlocked_until", -1) for i in infos]
                ok = header.get("password") is (not want_open) and all((u > time.time() + 365 * 86400) if want_open else u == 0 for u in opened)
                if ok or time.time() > until:
                    break
                time.sleep(1)
            if not ok:
                failures.append(f"protection {rest}: seed.dat password {header.get('password')}, wallets open until {opened}")
            else:
                print(f"  protection {rest}: seed.dat password {header.get('password')}, the wallets of the phrase {'open' if want_open else 'locked'}", flush=True)
        elif what == "setbmm":
            if rest == "on":
                address = side(stem, datadir, "-rpcwallet=main", "getnewaddress")
                side(stem, datadir, "setbmm", "true", address, "true", "0.0001")
            else:
                side(stem, datadir, "setbmm", "false")
            print(f"  {stem}: merged mining {rest}", flush=True)
        elif what == "minebmm":
            # The daemon's own bids, paid by the mining wallet, while the main wallet stays locked and pays nothing.
            count = int(rest or "1")
            start = side(stem, datadir, "getblockcount")
            main_txs = len(chains(datadir, "-rpcwallet=main", "listtransactions", "*", "10000"))
            paid_before = chains(datadir, "-rpcwallet=mining", "getbalances")["mine"]["trusted"]
            to = chains(datadir, "-rpcwallet=demo", "getnewaddress")
            until = time.time() + 300
            while side(stem, datadir, "getblockcount") < start + count and time.time() < until:
                chains(datadir, "generatetoaddress", "1", to)
                time.sleep(3)
            height = side(stem, datadir, "getblockcount")
            paid_after = chains(datadir, "-rpcwallet=mining", "getbalances")["mine"]["trusted"]
            main_info = chains(datadir, "-rpcwallet=main", "getwalletinfo")
            bids = [t for t in chains(datadir, "-rpcwallet=mining", "listtransactions", "*", "1000") if t.get("category") == "send"]
            if height < start + count:
                failures.append(f"merged mining: {stem} at {height}, not {start + count}: its bids were not paid")
            if not paid_after < paid_before or not bids:
                failures.append(f"merged mining: the mining wallet paid nothing ({paid_before} -> {paid_after}, {len(bids)} sends)")
            if main_info.get("unlocked_until") != 0 or len(chains(datadir, "-rpcwallet=main", "listtransactions", "*", "10000")) != main_txs:
                failures.append(f"merged mining: the main wallet was not left locked and untouched: {main_info.get('unlocked_until')}")
            print(f"  merged mining: {stem} {start} -> {height}, paid by the mining wallet ({paid_before} -> {paid_after}, {len(bids)} bids), the main wallet locked and untouched", flush=True)
        elif what == "backupfile":
            # zSide's own backup, written by its node into the file the app's chooser gave (the second time: written over).
            round_, path = rest.split(" ", 1)
            target = Path(path)
            until = time.time() + 90
            previous = noted.get("backup-mtime", 0)
            while time.time() < until and not (target.is_file() and target.stat().st_size > 0 and target.stat().st_mtime_ns > previous):
                time.sleep(0.5)
            if not (target.is_file() and target.stat().st_size > 0 and target.stat().st_mtime_ns > previous):
                failures.append(f"zside: its backup ({round_}) was not written to {path}")
            else:
                noted["backup-mtime"] = target.stat().st_mtime_ns
                print(f"  zside: its own backup written ({'a new file' if round_ == '1' else 'written over, the chooser asked'}): {target.stat().st_size} bytes", flush=True)
        elif what == "balances":
            noted["balances"] = json.loads(rest)
            print(f"  balances noted: {noted['balances']}", flush=True)

    first = base / "first" / "datadir"
    if "first" in only:
        first.mkdir(parents=True)
        run(args, base, "first", first, f"window.__SEED = {json.dumps({'phase': 'first', 'extra': extra})};\n", programs, ports, handle, failures)
    if "restore" in only and "words" in noted and "balances" in noted:
        second = base / "restore" / "datadir"
        second.parent.mkdir(parents=True)
        copy_chains(first, second)
        prelude = f"window.__SEED = {json.dumps({'phase': 'restore', 'words': noted['words'], 'balances': noted['balances'], 'extra': extra})};\n"
        run(args, base, "restore", second, prelude, programs, ports, handle, failures)
    elif "restore" in only:
        failures.append("restore: not run (the first run noted no words or balances)")
    if "migrate" in only:
        third = base / "migrate" / "datadir"
        third.mkdir(parents=True)
        run(args, base, "migrate", third, "window.__SEED = {phase: 'migrate'};\n", programs, ports, handle, failures)
    if "nopass" in only:
        fourth = base / "nopass" / "datadir"
        fourth.mkdir(parents=True)
        noted.pop("words", None)
        prelude = f"window.__SEED = {json.dumps({'phase': 'nopass', 'zside': zside_dir is not None, 'zsideBackup': str(base / 'nopass' / 'zside-backup.dat')})};\n"
        run(args, base, "nopass", fourth, prelude, programs, ports, handle, failures)
    if failures:
        print("FAILED:\n  " + "\n  ".join(failures), flush=True)
        return 1
    print("OK", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
