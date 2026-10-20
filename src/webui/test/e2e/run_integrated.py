#!/usr/bin/env python3
# Copyright (c) 2026 The Chains developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end test of the app's own pages over every chain it runs (doc/unified-app.md §18, §19).

Starts chains-app on regtest (a temporary data directory, a virtual display) with integrated.js in its page, and the
daemons of Layers and Thunder from --sidechain-bindir (Layers with a validator key of its own, in its layers.conf, as a
user's would be). The page turns both on and drives the app's own pages: Overview with every chain, a deposit into
Layers from the Sidechains page, an asset sent on Layers from Send (the chain picked in its picker; a wrong-chain
address refused first), Receive on each chain, Transactions of every chain, the wallets of each, Staking, Assets; the one
chain selector of the top bar on every page (§19). With BitAssets and Hivemind in a --sidechain-bindir too, both are
turned on as well: their groups in the sidebar, and a trade on a Hivemind market (made here with its own CLI, as its
pages would) whose open position shows on Home. This runner merged-mines the sidechains' blocks when the page asks
("CHECKPOINT bmm <stem> <count>") and makes the market ("CHECKPOINT hivemind trade 0").

    run_integrated.py --app build_app/bin/chains-app --sidechain-bindir ~/skydoge/layers/build_rel/bin \\
                      --sidechain-bindir ~/skydoge/thunder/build/bin --tmp <dir> --shots <dir>

Nothing outside --tmp is touched; the nodes use ports of their own (--port); only processes started here are stopped.
"""

import argparse
import json
import os
import secrets
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run_smoke import App, check_shutdown, environment, ports_taken, script  # noqa: E402
from run_unified import daemons  # noqa: E402
from run_packs import SIDECHAINS, bmm  # noqa: E402
from run_unified import cli  # noqa: E402

# The chains this run may turn on: (slot, place in the catalog: its ports), as run_packs has them for the first three
# (its bmm reads its own table: the two more added there).
SIDECHAINS.update({"bitassets": (4, 4), "hivemind": (9, 6)})
REQUIRED = ("layers", "thunder")
OPTIONAL = ("bitassets", "hivemind")


def hivemind_trade(side_cli, datadir, port, bmm_one):
    """A market on Hivemind and a purchase of Yes in it, by its own CLI (as its pages would ask): a position to show."""
    side = [f"-datadir={datadir}/sidechains/hivemind/data", "-regtest", f"-rpcport={port}", "-rpcwallet=main"]
    info = cli(side_cli, side + ["gethivemindinfo"])
    decision = cli(side_cli, side + ["createdecision", "Will it rain tomorrow?", str(info["period"] + 2)])
    bmm_one()
    market = cli(side_cli, side + ["createmarket", "Rain tomorrow?", json.dumps([decision["id"]]), "10"])
    bmm_one()
    cli(side_cli, side + ["buyshares", market["id"], "1", "3"])
    bmm_one()
    return market["id"]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--sidechain-bindir", required=True, type=Path, action="append", help="folders with layersd and thunderd")
    parser.add_argument("--tmp", required=True, type=Path, help="folder for the data directory (made anew)")
    parser.add_argument("--shots", required=True, type=Path, help="folder for the pictures")
    parser.add_argument("--port", type=int, default=49510, help="ports of Chains: port, port+1 (RPC); the sidechains from port+10")
    args = parser.parse_args()

    if shutil.which("xvfb-run") is None:
        print("xvfb-run is needed", file=sys.stderr)
        return 2
    programs = {}
    for stem in REQUIRED + OPTIONAL:
        found = next((d for d in args.sidechain_bindir if (d / f"{stem}d").is_file() and (d / f"{stem}-cli").is_file()), None)
        if not found:
            if stem in REQUIRED:
                print(f"no {stem}d in {args.sidechain_bindir}", file=sys.stderr)
                return 2
            print(f"  no {stem}d in {args.sidechain_bindir}: run without it", flush=True)
            continue
        programs[stem] = found / f"{stem}-cli"
    rpc_port, sc_base = args.port + 1, args.port + 10
    ports = {stem: sc_base + 2 * SIDECHAINS[stem][1] for stem in programs}
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
    # Layers as a user who stakes has it: its own validator key, named in its own conf (the app passes none, touches none).
    layers_data = datadir / "sidechains" / "layers" / "data"
    layers_data.mkdir(parents=True)
    key = layers_data / "validator.key"
    key.write_text(secrets.token_hex(32) + "\n")
    key.chmod(0o600)
    (layers_data / "layers.conf").write_text(f"[regtest]\nvalidatorkeyfile={key}\n")
    page = base / "integrated.js"
    extra = [s for s in OPTIONAL if s in programs]
    page.write_text(script("integrated.js", f"window.__INTEGRATED = {json.dumps({'extra': extra})};\n"))
    chains_cli = args.app.parent / "chains-cli"
    failures = []
    app = App(args.app, ["-regtest", f"-port={args.port}", f"-rpcport={rpc_port}", "-listen=0", "-dnsseed=0", "-fixedseeds=0",
                         "-fallbackfee=0.0002", f"-datadir={datadir}", f"-webuiconfig={base / 'chains-app.json'}",
                         *[f"-sidechainbindir={d.resolve()}" for d in args.sidechain_bindir], f"-sidechainportbase={sc_base}", "-sidechainarg=-listen=0",
                         "-sidechainarg=-fallbackfee=0.0002",
                         f"-webuitestshots={args.shots.resolve()}", f"-webuitestscript={page}"], env, base / "integrated.log")
    try:
        while True:
            message = app.wait_event("CHECKPOINT", 1800)
            if message is None:
                break
            app.events.pop("CHECKPOINT", None)
            _, what, stem, count = message.split()
            try:
                if what == "bmm":
                    bmm(chains_cli, programs[stem], datadir, rpc_port, stem, ports[stem], int(count))
                elif what == "hivemind" and stem == "trade":
                    one = lambda: bmm(chains_cli, programs["hivemind"], datadir, rpc_port, "hivemind", ports["hivemind"], 1)  # noqa: E731
                    print(f"  a Hivemind market traded: {hivemind_trade(programs['hivemind'], datadir, ports['hivemind'], one)}", flush=True)
            except (RuntimeError, subprocess.TimeoutExpired, KeyError, ValueError, TypeError) as e:
                failures.append(f"{what} {stem}: {e}")
            if "RESULT" in app.events:
                break
        code = app.wait(900)
    finally:
        app.stop()
    result = app.result()
    if code != 0 or not result or result["failures"]:
        failures.append(f"integrated run: exit {code}, {result['failures'] if result else 'no result'}; the page last said: {app.last}")
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
