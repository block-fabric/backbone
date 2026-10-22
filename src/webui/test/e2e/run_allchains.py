#!/usr/bin/env python3
# Copyright (c) 2026 The Chains developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end test of the unified app with every sidechain of its catalog on (doc/unified-app.md §25).

Starts chains-app on regtest (a temporary data directory, a virtual display) with allchains.js in its page and the daemons
of the seven sidechains from --sidechain-bindir (the first folder that has a program wins: give the staged builds first).
The page turns all of them on (FastBlocks through its wizard, as a validator of this computer), moves coins into each,
and drives what each one is for through the app's own pages: a payment and an address on every chain, a withdrawal
started from every chain; FastBlocks' and Layers' validator keys made by the app and a bond; a name registered on
BitNames, found again by the palette (Ctrl+K) and the explorer's search; zSide's coins shielded, paid privately with a memo
and unshielded; a market traded on Hivemind; then Home, Overview, the explorer's strips, Transactions, the sidebar's groups
and the selector with all seven, in Midnight and in a light theme. This runner merged-mines the sidechains' blocks when the
page asks ("CHECKPOINT bmm <stem|all> <count>"), makes the Hivemind market ("CHECKPOINT hivemind trade 0"), and checks
what a page cannot see by itself ("CHECKPOINT validator <stem>": the node's own arguments and its key file).

    run_allchains.py --app build_app/bin/chains-app --sidechain-bindir ~/skydoge/tools/unified-bin.next \\
                     --sidechain-bindir ~/skydoge/tools/unified-bin --tmp <dir> --shots <dir>

Nothing outside --tmp is touched; the nodes use ports of their own (--port); only processes started here are stopped.
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
from run_unified import cli, daemons, processes  # noqa: E402
from run_integrated import hivemind_trade  # noqa: E402

# The catalog of the supervisor (supervisor.cpp): slot and place (its ports are base + 2 place).
CATALOG = {"layers": (7, 0), "fastblocks": (5, 1), "thunder": (2, 2), "bitnames": (3, 3), "bitassets": (4, 4), "zside": (6, 5), "hivemind": (9, 6)}


def bmm_all(chains_cli, programs, datadir, rpc_port, ports, stems, count, failures):
    """Blocks of each sidechain asked by blind merged mining, all in one block of Chains a round: each builds one, the wallet of
    Chains asks for each, a block of Chains takes them all."""
    chains = [f"-datadir={datadir}", "-regtest", f"-rpcport={rpc_port}"]
    mining = cli(chains_cli, chains + ["-rpcwallet=demo", "getnewaddress"])
    for _ in range(count):
        made = {}
        for stem in stems:
            side = [f"-datadir={datadir}/sidechains/{stem}/data", "-regtest", f"-rpcport={ports[stem]}"]
            # Twice at most: a node started again a moment ago (its validator key, its payout) answers a little later.
            for attempt in range(2):
                try:
                    wallets = cli(programs[stem], side + ["listwallets"])
                    wallet = "main" if "main" in wallets else (wallets[0] if wallets else "")
                    address = cli(programs[stem], side + [f"-rpcwallet={wallet}", "getnewaddress"]) if wallet else None
                    block = cli(programs[stem], side + ["createbmmblock"] + ([address] if address else []))
                    cli(chains_cli, chains + ["-rpcwallet=demo", "createbmmrequest", str(CATALOG[stem][0]), block["hash"], "0.001"])
                    made[stem] = block["hash"]
                    break
                except (RuntimeError, subprocess.TimeoutExpired, KeyError, TypeError) as e:
                    if attempt:
                        print(f"  bmm {stem}: no block this round ({str(e).splitlines()[0]})", flush=True)
                    else:
                        time.sleep(5)
        cli(chains_cli, chains + ["generatetoaddress", "1", mining])
        until = time.time() + 60
        waiting = dict(made)
        while waiting and time.time() < until:
            for stem, h in list(waiting.items()):
                side = [f"-datadir={datadir}/sidechains/{stem}/data", "-regtest", f"-rpcport={ports[stem]}"]
                try:
                    best = cli(programs[stem], side + ["getbestblockhash"])
                    if best == h or cli(programs[stem], side + ["getblockheader", h]).get("confirmations", -1) >= 1:
                        del waiting[stem]
                except (RuntimeError, subprocess.TimeoutExpired):
                    pass
            time.sleep(0.4)
        if waiting:
            print(f"  bmm: not taken in time by {sorted(waiting)}", flush=True)
        print(f"  bmm round: {sorted(made)} asked, {sorted(set(made) - set(waiting))} taken", flush=True)


def check_validator(stem, datadir, failures):
    """The node of a chain that stakes runs with the validator key the app made on this computer: its argument, its file (0600,
    a secp256k1 secret in hex, not a key of the phrase: the run has none), its payout to an address of its wallet."""
    for pid, (ppid, comm, argv) in processes().items():
        if not argv or not argv[0].endswith(f"{stem}d") or not any(a.startswith(f"-datadir={datadir}/sidechains/") for a in argv):
            continue
        key_args = [a for a in argv if a.startswith("-validatorkeyfile=")]
        payout = [a for a in argv if a.startswith("-validatorpayout=")]
        if not key_args:
            failures.append(f"{stem}: its node runs without the app's validator key: {argv}")
            return
        path = Path(key_args[0].split("=", 1)[1])
        mode = path.stat().st_mode & 0o777
        text = path.read_text().strip()
        if mode != 0o600 or len(text) != 64 or not all(c in "0123456789abcdef" for c in text):
            failures.append(f"{stem}: its key file {path} is not one secret of 32 bytes, owner-only (mode {oct(mode)})")
            return
        if path.parent != datadir / "sidechains" / stem / "data" / "regtest":
            failures.append(f"{stem}: its key file is not in its network's data folder: {path}")
            return
        print(f"  {stem}: its node runs with the app's validator key ({path.name}, {oct(mode)}){', its payout ' + payout[0].split('=', 1)[1] if payout else ''}", flush=True)
        return
    failures.append(f"{stem}: no node of it runs")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--sidechain-bindir", required=True, type=Path, action="append", help="folders with the sidechains' programs (the first that has one wins)")
    parser.add_argument("--tmp", required=True, type=Path, help="folder for the data directory (made anew)")
    parser.add_argument("--shots", required=True, type=Path, help="folder for the pictures")
    parser.add_argument("--port", type=int, default=49460, help="ports of Chains: port, port+1 (RPC); the sidechains from port+10")
    parser.add_argument("--only", default="", help="the sidechains to turn on (default: every one whose program is there)")
    args = parser.parse_args()

    if shutil.which("xvfb-run") is None:
        print("xvfb-run is needed", file=sys.stderr)
        return 2
    wanted = [s for s in CATALOG if not args.only or s in args.only.split(",")]
    programs = {}
    for stem in wanted:
        found = next((d for d in args.sidechain_bindir if (d / f"{stem}d").is_file() and (d / f"{stem}-cli").is_file()), None)
        if not found:
            print(f"  no {stem}d in {args.sidechain_bindir}: run without it", flush=True)
            continue
        programs[stem] = found / f"{stem}-cli"
        print(f"  {stem}: {found / f'{stem}d'}", flush=True)
    if not programs:
        print("no sidechain program found", file=sys.stderr)
        return 2
    rpc_port, sc_base = args.port + 1, args.port + 10
    ports = {stem: sc_base + 2 * CATALOG[stem][1] for stem in programs}
    taken = ports_taken([args.port, rpc_port] + [p for s in ports.values() for p in (s, s + 1)])
    if taken:
        print(f"ports {taken} are taken: give another --port", file=sys.stderr)
        return 2
    base = args.tmp.resolve()
    if base.exists():
        shutil.rmtree(base)
    base.mkdir(parents=True)
    args.shots.mkdir(parents=True, exist_ok=True)
    env = environment(base)
    datadir = base / "datadir"
    datadir.mkdir()
    page = base / "allchains.js"
    page.write_text(script("allchains.js", f"window.__ALLCHAINS = {json.dumps({'chains': list(programs)})};\n"))
    chains_cli = args.app.parent / "chains-cli"
    if not chains_cli.is_file():
        chains_cli = args.app.parent / "bitcoin-cli"
    failures = []
    app = App(args.app, ["-regtest", f"-port={args.port}", f"-rpcport={rpc_port}", "-listen=0", "-dnsseed=0", "-fixedseeds=0",
                         "-fallbackfee=0.0002", f"-datadir={datadir}", f"-webuiconfig={base / 'chains-app.json'}",
                         *[f"-sidechainbindir={d.resolve()}" for d in args.sidechain_bindir], f"-sidechainportbase={sc_base}", "-sidechainarg=-listen=0",
                         "-sidechainarg=-fallbackfee=0.0002",
                         f"-webuitestshots={args.shots.resolve()}", f"-webuitestscript={page}"], env, base / "allchains.log", screen="1440x960")
    try:
        while True:
            message = app.wait_event("CHECKPOINT", 2400)
            if message is None:
                break
            app.events.pop("CHECKPOINT", None)
            parts = message.split()
            what, stem = parts[1], parts[2]
            count = parts[3] if len(parts) > 3 else "0"
            try:
                if what == "bmm":
                    stems = [s for s in programs if s != "fastblocks" or True] if stem == "all" else [stem]
                    bmm_all(chains_cli, programs, datadir, rpc_port, ports, stems, int(count), failures)
                elif what == "hivemind" and stem == "trade":
                    one = lambda: bmm_all(chains_cli, programs, datadir, rpc_port, ports, ["hivemind"], 1, failures)  # noqa: E731
                    print(f"  a Hivemind market traded: {hivemind_trade(programs['hivemind'], datadir, ports['hivemind'], one)}", flush=True)
                elif what == "validator":
                    check_validator(stem, datadir, failures)
            except (RuntimeError, subprocess.TimeoutExpired, KeyError, ValueError, TypeError, OSError) as e:
                failures.append(f"{what} {stem}: {e}")
            if "RESULT" in app.events:
                break
        code = app.wait(900)
    finally:
        app.stop()
    result = app.result()
    if code != 0 or not result or result["failures"]:
        failures.append(f"allchains run: exit {code}, {result['failures'] if result else 'no result'}; the page last said: {app.last}")
    check_shutdown(datadir, failures)
    time.sleep(1)
    for dpid in daemons(datadir):
        failures.append(f"a sidechain daemon was left running ({dpid}): stopped")
        os.kill(dpid, 15)
    if result:
        missing = [s for s in result["shots"] if not (args.shots / f"{s}.png").is_file()]
        if missing:
            failures.append(f"missing pictures: {missing}")
        print(f"  {result['passed']} checks passed, {len(result['shots'])} pictures in {args.shots}", flush=True)
    if failures:
        print("FAILED:\n  " + "\n  ".join(failures), flush=True)
        return 1
    print("OK", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
