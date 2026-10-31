#!/usr/bin/env python3
# Copyright (c) 2026 The Backbone developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end test of Backbone's sidechains (doc/unified-app.md §26, P1): the app supervises Flush and Thunder.

Backbone (backbone-app with the test hooks) on regtest with eCash's rules (-testecash), slots 46 and 9 activated, and a
local HTTPS mirror of the latest releases of block-fabric/backbone-flush and backbone-thunder made of real releases:
Flush's program package (8.3.7, as published: no pages in it) with the pages' asset its next release attaches
(flush-<version>-pack.tar.gz, built by the Flush repository's deploy-pack), and Thunder's (mainnet-v1, its pages in its
package). backbone.js turns them on as a user would (each downloaded from its latest release and checked by its
SHA256SUMS first); this runner checks the processes at its checkpoints, merged-mines Flush's blocks as an eCash miner
would (createbmmblock, createbmmrequest: the test's, not the app's: merged mining from the app is P4), and checks that
nothing is left running.

    run_backbone.py --app build_t/bin/backbone-app --flush-release <dir: flush-<v>-x86_64-linux-gnu.tar.gz, SHA256SUMS>
                    --flush-pack <flush-<v>-pack.tar.gz> --thunder-release <dir: thunder-<tag>-x86_64-linux-gnu.tar.gz, SHA256SUMS>
                    --tmp <dir> --shots <dir>

Nothing outside --tmp is touched; the nodes use ports of their own; only processes started here are stopped.
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from run_smoke import App, check_shutdown, environment, ports_taken, script  # noqa: E402
from run_unified import find_app, processes, threads  # noqa: E402
import release_mirror  # noqa: E402

OWNER = "block-fabric"


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def mirror_release(out, repo, tag, files, base_url):
    """One latest release of a repository, as GitHub serves it: releases/latest, its files, their SHA256SUMS."""
    download = out / OWNER / repo / "releases" / "download" / tag
    download.mkdir(parents=True)
    sums = []
    assets = []
    for f in files:
        shutil.copyfile(f, download / f.name)
        sums.append(f"{sha256(f)}  {f.name}")
        assets.append({"name": f.name, "size": f.stat().st_size, "browser_download_url": f"{base_url}/ignored/{f.name}"})
    (download / "SHA256SUMS").write_text("\n".join(sums) + "\n")
    assets.append({"name": "SHA256SUMS", "size": (download / "SHA256SUMS").stat().st_size})
    api = out / "repos" / OWNER / repo / "releases"
    api.mkdir(parents=True)
    (api / "latest").write_text(json.dumps({"tag_name": tag, "name": f"{repo} {tag}", "draft": False, "prerelease": False,
                                            "published_at": "2026-10-30T20:40:22Z", "assets": assets}))


def daemons(datadir):
    """The sidechains' programs whose data is under this data directory: {pid: (ppid, argv)}."""
    out = {}
    for pid, (ppid, _comm, argv) in processes().items():
        joined = " ".join(argv)
        if argv and f"{datadir}/sidechains/" in joined and not argv[0].endswith(("-cli", "curl")):
            out[pid] = (ppid, argv)
    return out


def cli(program, args, timeout=60):
    out = subprocess.run([str(program)] + args, capture_output=True, text=True, timeout=timeout)
    if out.returncode != 0:
        raise RuntimeError(f"{Path(program).name} {args[-3:]}: {out.stderr.strip()}")
    text = out.stdout.strip()
    try:
        return json.loads(text)
    except ValueError:
        return text


def bmm(main_cli, flush_cli, datadir, rpc_port, flush_port, count):
    """Blocks of Flush by blind merged mining, as an eCash miner would make them: Flush builds one, a wallet of the
    mainchain asks for it (createbmmrequest, slot 46), a block of the mainchain takes it."""
    main = [f"-datadir={datadir}", "-regtest", f"-rpcport={rpc_port}"]
    side = [f"-datadir={datadir}/sidechains/flush/data", "-regtest", f"-rpcport={flush_port}"]
    mining = cli(main_cli, main + ["-rpcwallet=demo", "getnewaddress"])
    until = time.time() + 90
    while True:
        try:
            cli(flush_cli, side + ["-rpcwallet=main", "getwalletinfo"])
            break
        except RuntimeError:
            if time.time() > until:
                raise
            time.sleep(1)
    for _ in range(count):
        address = cli(flush_cli, side + ["-rpcwallet=main", "getnewaddress"])
        block = cli(flush_cli, side + ["createbmmblock", address])
        cli(main_cli, main + ["-rpcwallet=demo", "createbmmrequest", "46", block["hash"], "0.001"])
        cli(main_cli, main + ["generatetoaddress", "1", mining])
        until = time.time() + 60
        while time.time() < until and cli(flush_cli, side + ["getbestblockhash"]) != block["hash"]:
            time.sleep(0.5)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--flush-release", required=True, type=Path)
    parser.add_argument("--flush-pack", required=True, type=Path)
    parser.add_argument("--thunder-release", required=True, type=Path)
    parser.add_argument("--tmp", required=True, type=Path)
    parser.add_argument("--shots", required=True, type=Path)
    parser.add_argument("--port", type=int, default=49610, help="Backbone: port, port+1 (RPC), port+2 (gRPC); the mirror port+5; the sidechains from port+10")
    args = parser.parse_args()
    if shutil.which("xvfb-run") is None:
        print("xvfb-run is needed", file=sys.stderr)
        return 2
    flush_pkg = next(iter(sorted(args.flush_release.glob("flush-*-x86_64-linux-gnu.tar.gz"))), None)
    thunder_pkg = next(iter(sorted(args.thunder_release.glob("thunder-*-x86_64-linux-gnu.tar.gz"))), None)
    if not flush_pkg or not thunder_pkg or not args.flush_pack.is_file():
        print("the releases of Flush and Thunder (their Linux packages) and Flush's pages are needed", file=sys.stderr)
        return 2
    flush_version = re.match(r"flush-(.+)-x86_64-linux-gnu\.tar\.gz", flush_pkg.name).group(1)
    thunder_tag = re.match(r"thunder-(.+)-x86_64-linux-gnu\.tar\.gz", thunder_pkg.name).group(1)
    flush_tag = f"flush-v{flush_version}"
    rpc_port, grpc_port, mirror_port, sc_base = args.port + 1, args.port + 2, args.port + 5, args.port + 10
    flush_port = sc_base  # Flush first in the catalog: RPC sc_base, peers +1; Thunder's app RPC +2, peers +3, its own RPC +102
    taken = ports_taken([args.port, rpc_port, grpc_port, mirror_port, sc_base, sc_base + 1, sc_base + 2, sc_base + 3, sc_base + 102])
    if taken:
        print(f"ports {taken} are taken: give another --port", file=sys.stderr)
        return 2
    base = args.tmp.resolve()
    if base.exists():
        shutil.rmtree(base)
    base.mkdir(parents=True)
    args.shots.mkdir(parents=True, exist_ok=True)

    mirror = base / "mirror"
    url = f"https://127.0.0.1:{mirror_port}"
    pack = args.flush_pack.resolve()
    pack_named = base / f"flush-{flush_version}-pack.tar.gz"
    shutil.copyfile(pack, pack_named)
    mirror_release(mirror, "backbone-flush", flush_tag, [flush_pkg.resolve(), pack_named], url)
    mirror_release(mirror, "backbone-thunder", thunder_tag, [thunder_pkg.resolve()], url)
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
    page = base / "backbone.js"
    page.write_text(script("backbone.js", prelude=f"window.__E2E_TAGS = {{ flush: '{flush_tag}', thunder: '{thunder_tag}' }};\n"))
    main_cli = next(c for c in (args.app.parent / "backbone-cli", args.app.parent / "chains-cli") if c.is_file())
    failures = []
    # No -grpc: the app serves it late, when Thunder is turned on (AppStartGrpcLate). No release key: checked by the sums.
    app = App(args.app, ["-regtest", "-testecash", f"-port={args.port}", f"-rpcport={rpc_port}", f"-grpcport={grpc_port}", "-listen=0", "-dnsseed=0",
                         "-fixedseeds=0", "-fallbackfee=0.0002", f"-datadir={datadir}", f"-webuiconfig={base / 'backbone-app.json'}",
                         f"-sidechainportbase={sc_base}", "-sidechainarg=-listen=0", "-sidechainarg=-fallbackfee=0.0002",
                         "-sidechaindropinarg=--bt-no-seeds", "-sidechaindropinarg=--bt-no-p2p",
                         f"-sidechainreleaseapi={url}", f"-sidechaindownloadcacert={cert}",
                         f"-webuitestshots={args.shots.resolve()}", f"-webuitestscript={page}"], env, base / "backbone.log")
    try:
        while True:
            message = app.wait_event("CHECKPOINT", 1800)
            if message is None:
                break
            app.events.pop("CHECKPOINT", None)
            parts = message.split()
            what = parts[1]
            try:
                pid = find_app(app.proc.pid)
                running = daemons(datadir)
                if what == "standalone":
                    print(f"  standalone: backbone-app {pid}", flush=True)
                    if ports_taken([rpc_port]):
                        failures.append("standalone: the RPC port of Backbone is open")
                    if ports_taken([grpc_port]):
                        failures.append("standalone: the gRPC port is open before a drop-in needs it")
                    if pid and any("webui-sidecha" in n for n in threads(pid)):
                        failures.append("standalone: the thread of the supervisor runs")
                    if running:
                        failures.append(f"standalone: programs run: {running}")
                elif what == "bmm":
                    installed = sorted((datadir / "sidechains" / "bin" / "flush").glob("*/flush-cli"))
                    bmm(main_cli, installed[-1], datadir, rpc_port, flush_port, int(parts[3]))
                    print(f"  bmm: {parts[3]} blocks of Flush", flush=True)
                elif what == "flush":
                    flush = {p: v for p, v in running.items() if v[1][0].endswith("/flushd")}
                    print(f"  flush: {list(flush)}", flush=True)
                    if len(flush) != 1:
                        failures.append(f"flush: {len(flush)} flushd, not 1")
                    for _p, (ppid, argv) in flush.items():
                        if ppid != pid:
                            failures.append(f"flush: not a child of the app ({ppid} != {pid})")
                        if f"/sidechains/bin/flush/{flush_tag}/flushd" not in argv[0]:
                            failures.append(f"flush: not the downloaded program: {argv[0]}")
                        for wanted in [f"-datadir={datadir}/sidechains/flush/data", "-regtest", "-sidechainslot=46", "-testmainenforcer",
                                       f"-mainchainrpcport={rpc_port}", f"-rpcport={flush_port}", "-webuichooser=chains-app"]:
                            if wanted not in argv:
                                failures.append(f"flush: {wanted} not given: {argv}")
                        if any("password=" in a for a in argv):
                            failures.append("flush: a password on its command line")
                elif what == "thunder":
                    thunder = {p: v for p, v in running.items() if v[1][0].endswith("/thunder")}
                    print(f"  thunder: {list(thunder)}", flush=True)
                    if len(thunder) != 1:
                        failures.append(f"thunder: {len(thunder)} programs, not 1")
                    for tpid, (ppid, argv) in thunder.items():
                        if ppid != pid:
                            failures.append(f"thunder: not a child of the app ({ppid} != {pid})")
                        joined = " ".join(argv)
                        for wanted in ["--network regtest", f"--mainchain-grpc-url http://127.0.0.1:{grpc_port}", f"--bt-app-rpc-port {sc_base + 2}",
                                       "--bt-app-exit-with-stdin", "--bt-app-rpcwhitelistdefault=0", "--bt-app-chooser chains-app",
                                       f"--datadir {datadir}/sidechains/thunder/data/regtest", "--bt-no-seeds"]:
                            if wanted not in joined:
                                failures.append(f"thunder: {wanted} not given: {joined}")
                        if "setwalletphrase" in joined or "password" in joined:
                            failures.append("thunder: the phrase's method or a password on its command line")
                        # Its standard input: a pipe the app holds (it stops when the app goes).
                        try:
                            stdin = os.readlink(f"/proc/{tpid}/fd/0")
                            if not stdin.startswith("pipe:"):
                                failures.append(f"thunder: its standard input is {stdin}, not the app's pipe")
                        except OSError as e:
                            failures.append(f"thunder: {e}")
                    if not ports_taken([grpc_port]):
                        failures.append("thunder: Backbone's gRPC was not served late")
                elif what == "disabled":
                    if running:
                        failures.append(f"disabled: programs still run: {running}")
            except (RuntimeError, subprocess.TimeoutExpired, KeyError, ValueError, IndexError) as e:
                failures.append(f"{message}: {e}")
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
        failures.append(f"backbone run: exit {code}, {result['failures'] if result else 'no result'}; the page last said: {app.last}")
    check_shutdown(datadir, failures)
    time.sleep(1)
    for dpid in daemons(datadir):
        failures.append(f"a sidechain's program was left running ({dpid}): stopped")
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
