#!/usr/bin/env python3
# Copyright (c) 2026 The Chains developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end test of the pages of the sidechains in the unified app (doc/unified-app.md §13).

Starts chains-app on regtest (a temporary data directory, a virtual display) with packs.js in its page, and the
daemons of Layers and Thunder from --sidechain-bindir. The page turns both on, shows their own pages, moves coins into
them, and spends on them through the app's trusted dialog (packs.js). This runner merged-mines their blocks when the
page asks ("CHECKPOINT bmm <stem> <count>"), and, at "CHECKPOINT tamper <stem>", plays a client that changes a call
after its question: the daemon must refuse it, and refuse the token again after.

    run_packs.py --app build_app/bin/chains-app --sidechain-bindir ~/skydoge/layers/build/bin \\
                 --sidechain-bindir ~/skydoge/thunder/build/bin --tmp <dir> --shots <dir>

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
from run_unified import cli, daemons  # noqa: E402

# The catalog of the supervisor (supervisor.cpp): slot and index (its ports are base + 2 index).
SIDECHAINS = {"layers": (7, 0), "fastblocks": (5, 1), "thunder": (2, 2)}


def bmm(chains_cli, side_cli, datadir, rpc_port, stem, port, count):
    """Blocks of a sidechain by blind merged mining: it builds one, the wallet of Chains asks for it, a block of Chains takes it."""
    slot = SIDECHAINS[stem][0]
    chains = [f"-datadir={datadir}", "-regtest", f"-rpcport={rpc_port}"]
    side = [f"-datadir={datadir}/sidechains/{stem}/data", "-regtest", f"-rpcport={port}"]
    mining = cli(chains_cli, chains + ["-rpcwallet=demo", "getnewaddress"])
    for _ in range(count):
        address = cli(side_cli, side + ["-rpcwallet=main", "getnewaddress"])
        block = cli(side_cli, side + ["createbmmblock", address])
        cli(chains_cli, chains + ["-rpcwallet=demo", "createbmmrequest", str(slot), block["hash"], "0.001"])
        cli(chains_cli, chains + ["generatetoaddress", "1", mining])
        until = time.time() + 60
        while time.time() < until and cli(side_cli, side + ["getbestblockhash"]) != block["hash"]:
            time.sleep(0.5)


def tamper(side_cli, datadir, stem, port, failures):
    """A client that changes a call after its question: refused by the daemon, and its token used up."""
    side = [f"-datadir={datadir}/sidechains/{stem}/data", "-regtest", f"-rpcport={port}", "-rpcwallet=main"]
    address = cli(side_cli, side + ["getnewaddress"])
    described = cli(side_cli, side + ["describeconfirmation", "signmessage", json.dumps([address, "hello"])])
    if not described.get("needed") or len(described.get("token", "")) != 64:
        failures.append(f"tamper: no question for signmessage: {described}")
        return
    request = described["request"]
    changed = json.loads(json.dumps(request))
    changed["params"]["params"] = [address, "pay me"]
    for label, req in (("changed parameters", changed), ("the same call with the used token", request)):
        out = subprocess.run([str(side_cli)] + side + ["webuihost", json.dumps(req), described["token"]], capture_output=True, text=True, timeout=60)
        if out.returncode == 0 or "-32802" not in out.stderr:
            failures.append(f"tamper: {label} was not refused: {out.stdout.strip()} {out.stderr.strip()}")
        else:
            print(f"  tamper: {label} refused ({out.stderr.strip().splitlines()[-1] if out.stderr.strip() else ''})", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--sidechain-bindir", required=True, type=Path, action="append", help="folders with layersd and thunderd")
    parser.add_argument("--tmp", required=True, type=Path, help="folder for the data directory (made anew)")
    parser.add_argument("--shots", required=True, type=Path, help="folder for the pictures")
    parser.add_argument("--port", type=int, default=49410, help="ports of Chains: port, port+1 (RPC); the sidechains from port+10")
    args = parser.parse_args()

    if shutil.which("xvfb-run") is None:
        print("xvfb-run is needed", file=sys.stderr)
        return 2
    programs = {}
    for stem in ("layers", "thunder"):
        found = next((d for d in args.sidechain_bindir if (d / f"{stem}d").is_file()), None)
        if not found:
            print(f"no {stem}d in {args.sidechain_bindir}", file=sys.stderr)
            return 2
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
    page = base / "packs.js"
    page.write_text(script("packs.js"))
    chains_cli = args.app.parent / "chains-cli"
    failures = []
    app = App(args.app, ["-regtest", f"-port={args.port}", f"-rpcport={rpc_port}", "-listen=0", "-dnsseed=0", "-fixedseeds=0",
                         "-fallbackfee=0.0002", f"-datadir={datadir}", f"-webuiconfig={base / 'chains-app.json'}",
                         *[f"-sidechainbindir={d.resolve()}" for d in args.sidechain_bindir], f"-sidechainportbase={sc_base}", "-sidechainarg=-listen=0",
                         "-sidechainarg=-fallbackfee=0.0002",
                         f"-webuitestshots={args.shots.resolve()}", f"-webuitestscript={page}"], env, base / "packs.log")
    try:
        # The page asks; this runner mines or plays the tampering client; until the page is done.
        while True:
            message = app.wait_event("CHECKPOINT", 1800)
            if message is None:
                break
            app.events.pop("CHECKPOINT", None)
            _, what, stem, count = message.split()
            try:
                if what == "bmm":
                    bmm(chains_cli, programs[stem], datadir, rpc_port, stem, ports[stem], int(count))
                elif what == "tamper":
                    tamper(programs[stem], datadir, stem, ports[stem], failures)
            except (RuntimeError, subprocess.TimeoutExpired, KeyError, ValueError) as e:
                failures.append(f"{what} {stem}: {e}")
            if "RESULT" in app.events:
                break
        code = app.wait(900)
    finally:
        app.stop()
    result = app.result()
    if code != 0 or not result or result["failures"]:
        failures.append(f"packs run: exit {code}, {result['failures'] if result else 'no result'}; the page last said: {app.last}")
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
