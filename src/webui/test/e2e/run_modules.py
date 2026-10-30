#!/usr/bin/env python3
# Copyright (c) 2026 The Chains developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end test of the sidechains as modules, their programs downloaded (doc/unified-app.md §14, §15).

Makes a local mirror of the releases of Layers (release_mirror.py): two releases made of the layersd and layers-cli of
--layers-bindir (with their manifests, sums and signatures of the test key), and an older one laid out as a real
release made before this app (no manifest: refused). Serves it over HTTPS on 127.0.0.1, and starts chains-app on
regtest pointed at it (-sidechainreleaseapi, -sidechaindownloadcacert, -sidechainreleasekey: regtest only), with NO
folder of sidechain programs: Layers must be downloaded. The page (modules.js) drives the wizard, the module, a send
through the trusted dialog, the update. This runner merged-mines Layers' blocks at "CHECKPOINT bmm layers <count>".

    run_modules.py --app build_app/bin/chains-app --layers-bindir <dir with layersd, layers-cli> --tmp <dir> --shots <dir>
        [--old-release <dir with a real release archive and its SHA256SUMS, e.g. layers-20261020.1>]

Nothing outside --tmp is touched; ports from --port (49000–49999 here); only processes started here are stopped.
"""

import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from run_smoke import App, check_shutdown, environment, ports_taken, script  # noqa: E402
from run_unified import cli, daemons  # noqa: E402
import release_mirror  # noqa: E402

TAGS = {"old": "20261020.1", "first": "20261021.1", "second": "20261022.1"}


def bmm(chains_cli, side_cli, datadir, rpc_port, port, count):
    chains = [f"-datadir={datadir}", "-regtest", f"-rpcport={rpc_port}"]
    side = [f"-datadir={datadir}/sidechains/layers/data", "-regtest", f"-rpcport={port}"]
    mining = cli(chains_cli, chains + ["-rpcwallet=demo", "getnewaddress"])
    # Its wallet is loaded by the supervisor once its node answers: a moment after "running".
    until = time.time() + 90
    while True:
        try:
            cli(side_cli, side + ["-rpcwallet=main", "getwalletinfo"])
            break
        except RuntimeError:
            if time.time() > until:
                raise
            time.sleep(1)
    for _ in range(count):
        address = cli(side_cli, side + ["-rpcwallet=main", "getnewaddress"])
        block = cli(side_cli, side + ["createbmmblock", address])
        cli(chains_cli, chains + ["-rpcwallet=demo", "createbmmrequest", "7", block["hash"], "0.001"])
        cli(chains_cli, chains + ["generatetoaddress", "1", mining])
        until = time.time() + 60
        while time.time() < until and cli(side_cli, side + ["getbestblockhash"]) != block["hash"]:
            time.sleep(0.5)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--layers-bindir", required=True, type=Path)
    parser.add_argument("--old-release", type=Path, help="a real older release of Layers (its archive and SHA256SUMS-layers.txt)")
    parser.add_argument("--tmp", required=True, type=Path)
    parser.add_argument("--shots", required=True, type=Path)
    parser.add_argument("--port", type=int, default=49510, help="Chains: port, port+1; the mirror port+5; the sidechains from port+10")
    args = parser.parse_args()
    if shutil.which("xvfb-run") is None:
        print("xvfb-run is needed", file=sys.stderr)
        return 2
    rpc_port, mirror_port, sc_base = args.port + 1, args.port + 5, args.port + 10
    layers_port = sc_base  # Layers is first in the catalog
    taken = ports_taken([args.port, rpc_port, mirror_port, layers_port, layers_port + 1])
    if taken:
        print(f"ports {taken} are taken: give another --port", file=sys.stderr)
        return 2
    base = args.tmp.resolve()
    if base.exists():
        shutil.rmtree(base)
    base.mkdir(parents=True)
    args.shots.mkdir(parents=True, exist_ok=True)

    # The mirror: made here, served by a process of this runner.
    mirror = base / "mirror"
    release_mirror.make(stem="layers", bindir=args.layers_bindir.resolve(), tags=[TAGS["first"], TAGS["second"]], old_release=args.old_release.resolve() if args.old_release else None, old_tag=TAGS["old"], key_hex=release_mirror.TEST_PRIVKEY_HEX, out=mirror, base_url=f"https://127.0.0.1:{mirror_port}")
    cert, key = base / "mirror-cert.pem", base / "mirror-key.pem"
    release_mirror.make_cert(cert, key)
    server = subprocess.Popen([sys.executable, "-I", str(HERE / "release_mirror.py"), "serve", "--dir", str(mirror), "--port", str(mirror_port), "--cert", str(cert), "--key", str(key)],
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    ready = server.stdout.readline()
    if not ready.startswith("ready"):
        server.kill()
        print(f"the mirror did not start: {ready}", file=sys.stderr)
        return 2

    env = environment(base)
    datadir = base / "datadir"
    datadir.mkdir()
    page = base / "modules.js"
    page.write_text(script("modules.js", prelude=f"window.__E2E_TAGS = {{ old: '{TAGS['old']}', first: '{TAGS['first']}', second: '{TAGS['second']}' }};\n"))
    chains_cli = args.app.parent / "chains-cli"
    side_cli = None
    failures = []
    app = App(args.app, ["-regtest", f"-port={args.port}", f"-rpcport={rpc_port}", "-listen=0", "-dnsseed=0", "-fixedseeds=0",
                         "-fallbackfee=0.0002", f"-datadir={datadir}", f"-webuiconfig={base / 'chains-app.json'}",
                         f"-sidechainportbase={sc_base}", "-sidechainarg=-listen=0", "-sidechainarg=-fallbackfee=0.0002",
                         f"-sidechainreleaseapi=https://127.0.0.1:{mirror_port}", f"-sidechaindownloadcacert={cert}",
                         f"-sidechainreleasekey={release_mirror.TEST_PUBKEY_HEX}",
                         f"-webuitestshots={args.shots.resolve()}", f"-webuitestscript={page}"], env, base / "modules.log")
    try:
        while True:
            message = app.wait_event("CHECKPOINT", 1800)
            if message is None:
                break
            app.events.pop("CHECKPOINT", None)
            _, what, stem, count = message.split()
            try:
                # The downloaded program's own cli, as a user would have it.
                installed = sorted((datadir / "sidechains" / "bin" / "layers").glob("*/layers-cli"))
                side_cli = installed[-1] if installed else args.layers_bindir / "layers-cli"
                if what == "bmm":
                    bmm(chains_cli, side_cli, datadir, rpc_port, layers_port, int(count))
            except (RuntimeError, subprocess.TimeoutExpired, KeyError, ValueError) as e:
                failures.append(f"{what} {stem}: {e}")
            if "RESULT" in app.events:
                break
        code = app.wait(900)
    finally:
        app.stop()
        server.terminate()
        try:
            server.wait(10)
        except subprocess.TimeoutExpired:
            server.kill()
    result = app.result()
    if code != 0 or not result or result["failures"]:
        failures.append(f"modules run: exit {code}, {result['failures'] if result else 'no result'}; the page last said: {app.last}")
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
