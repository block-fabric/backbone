#!/usr/bin/env python3
# Copyright (c) 2026 The Chains developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end test of the Trade page: the DEX of the chains the app runs, as one page of the app (doc/unified-app.md §22).

Starts chains-app on regtest (a temporary data directory, a virtual display) with dex.js in its page, and Layers from
--sidechain-bindir with its DEX's rules from its first blocks (testactivationheight=dex@1 in its own layers.conf: its
routes are orders of the DEX's batches). The page turns Layers on, moves coins into it, registers two assets (as Layers'
own pages do), then drives the Trade page as a user: a pool created with the wizard, a second one, a swap whose route
goes through both pools (quoted, its route shown, asked in the trusted dialog with the route the app checked, cleared in
a block), liquidity added to a pool and half of it taken out from the pool's page, the positions, the market index, and
Simple mode's swap. The runner merged-mines Layers' blocks when the page asks ("CHECKPOINT bmm layers <count>").

    run_dex.py --app build_app/bin/chains-app --sidechain-bindir <layers bin> --tmp <dir> --shots <dir>

Nothing outside --tmp is touched; the nodes use ports of their own (--port); only processes started here are stopped.
"""

import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run_smoke import App, check_shutdown, environment, ports_taken, script  # noqa: E402
from run_unified import daemons  # noqa: E402
from run_packs import SIDECHAINS, bmm  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--sidechain-bindir", required=True, type=Path, help="the folder with layersd and layers-cli")
    parser.add_argument("--tmp", required=True, type=Path, help="folder for the data directory (made anew)")
    parser.add_argument("--shots", required=True, type=Path, help="folder for the pictures")
    parser.add_argument("--port", type=int, default=49410, help="ports of Chains: port, port+1 (RPC); Layers from port+10")
    parser.add_argument("--screen", default="1400x900", help="the virtual display's size")
    args = parser.parse_args()

    if shutil.which("xvfb-run") is None:
        print("xvfb-run is needed", file=sys.stderr)
        return 2
    side_cli = args.sidechain_bindir / "layers-cli"
    if not (args.sidechain_bindir / "layersd").is_file() or not side_cli.is_file():
        print(f"no layersd in {args.sidechain_bindir}", file=sys.stderr)
        return 2
    rpc_port, sc_base = args.port + 1, args.port + 10
    layers_port = sc_base + 2 * SIDECHAINS["layers"][1]
    taken = ports_taken([args.port, rpc_port, layers_port, layers_port + 1])
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
    layers_data = datadir / "sidechains" / "layers" / "data"
    layers_data.mkdir(parents=True)
    # The DEX's rules from Layers' first blocks (every network but regtest has them from its genesis).
    (layers_data / "layers.conf").write_text("[regtest]\ntestactivationheight=dex@1\n")
    page = base / "dex.js"
    page.write_text(script("dex.js"))
    chains_cli = args.app.parent / "chains-cli"
    failures = []
    app = App(args.app, ["-regtest", f"-port={args.port}", f"-rpcport={rpc_port}", "-listen=0", "-dnsseed=0", "-fixedseeds=0",
                         "-fallbackfee=0.0002", f"-datadir={datadir}", f"-webuiconfig={base / 'chains-app.json'}",
                         f"-sidechainbindir={args.sidechain_bindir.resolve()}", f"-sidechainportbase={sc_base}", "-sidechainarg=-listen=0",
                         "-sidechainarg=-fallbackfee=0.0002",
                         f"-webuitestshots={args.shots.resolve()}", f"-webuitestscript={page}"], env, base / "dex.log", screen=args.screen)
    try:
        while True:
            message = app.wait_event("CHECKPOINT", 1800)
            if message is None:
                break
            app.events.pop("CHECKPOINT", None)
            parts = message.split()
            what, stem = parts[1], parts[2]
            count = parts[3] if len(parts) > 3 else "0"
            try:
                if what == "bmm":
                    bmm(chains_cli, side_cli, datadir, rpc_port, stem, layers_port, int(count))
            except (RuntimeError, subprocess.TimeoutExpired, KeyError, ValueError, TypeError) as e:
                failures.append(f"{what} {stem}: {e}")
            if "RESULT" in app.events:
                break
        code = app.wait(900)
    finally:
        app.stop()
    result = app.result()
    if code != 0 or not result or result["failures"]:
        failures.append(f"dex run: exit {code}, {result['failures'] if result else 'no result'}; the page last said: {app.last}")
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
