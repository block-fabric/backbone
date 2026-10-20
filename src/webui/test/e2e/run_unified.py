#!/usr/bin/env python3
# Copyright (c) 2026 The Chains developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end test of the unified app (doc/unified-app.md): the Chains app runs a sidechain.

Starts chains-app on regtest with a temporary data directory, in a virtual display, with unified.js
in its page: Chains alone (checked here: no RPC port, no thread of the supervisor, no daemon); Layers
turned on in Settings → Sidechains through its wizard (checked here: its daemon runs, a child of the
app, wired to the node of the app, no password on its command line); the chain switcher shows it in
step with Chains; the first-start guide; Layers turned off (checked here: its daemon stopped cleanly).
Pictures in Midnight.

    run_unified.py --app build/bin/chains-app --sidechain-bindir ~/skydoge/layers/build_rel/bin --tmp <dir> --shots <dir>

Nothing outside --tmp is touched; the nodes use ports of their own; only processes started here are stopped.
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


def processes():
    """{pid: (ppid, comm, argv)} of this user's processes."""
    out = {}
    for entry in Path("/proc").iterdir():
        if not entry.name.isdigit():
            continue
        try:
            stat = (entry / "stat").read_text()
            argv = (entry / "cmdline").read_bytes().split(b"\0")
        except OSError:
            continue
        comm = stat[stat.index("(") + 1:stat.rindex(")")]
        ppid = int(stat[stat.rindex(")") + 2:].split()[1])
        out[int(entry.name)] = (ppid, comm, [a.decode(errors="replace") for a in argv if a])
    return out


def find_app(root_pid):
    """The pid of chains-app under xvfb-run."""
    procs = processes()
    todo = [root_pid]
    while todo:
        pid = todo.pop()
        if pid in procs and procs[pid][1].startswith("chains-app"):
            return pid
        todo += [p for p, (pp, _, _) in procs.items() if pp == pid]
    return None


def daemons(datadir):
    """The sidechain daemons whose data is under this data directory: {pid: (ppid, argv)}."""
    return {pid: (ppid, argv) for pid, (ppid, comm, argv) in processes().items()
            if argv and argv[0].endswith("d") and any(a.startswith(f"-datadir={datadir}/sidechains/") for a in argv)}


def threads(pid):
    names = []
    for task in Path(f"/proc/{pid}/task").iterdir():
        try:
            names.append((task / "comm").read_text().strip())
        except OSError:
            pass
    return names


def cli(program, args, timeout=60):
    out = subprocess.run([str(program)] + args, capture_output=True, text=True, timeout=timeout)
    if out.returncode != 0:
        raise RuntimeError(f"{program.name} {args[-3:]}: {out.stderr.strip()}")
    text = out.stdout.strip()
    try:
        return json.loads(text)
    except ValueError:
        return text


def bmm_blocks(chains_cli, layers_cli, datadir, rpc_port, sc_base, count):
    """Blocks of Layers by blind merged mining: Layers builds one, the wallet of Chains asks for it, a block of Chains takes it."""
    chains = [f"-datadir={datadir}", "-regtest", f"-rpcport={rpc_port}"]
    layers = [f"-datadir={datadir}/sidechains/layers/data", "-regtest", f"-rpcport={sc_base}"]
    mining = cli(chains_cli, chains + ["-rpcwallet=demo", "getnewaddress"])
    for _ in range(count):
        address = cli(layers_cli, layers + ["-rpcwallet=main", "getnewaddress"])
        block = cli(layers_cli, layers + ["createbmmblock", address])
        cli(chains_cli, chains + ["-rpcwallet=demo", "createbmmrequest", "7", block["hash"], "0.001"])
        cli(chains_cli, chains + ["generatetoaddress", "1", mining])
        until = time.time() + 60
        while time.time() < until and cli(layers_cli, layers + ["getbestblockhash"]) != block["hash"]:
            time.sleep(0.5)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--sidechain-bindir", required=True, type=Path, action="append", help="folder with layersd (more folders: other sidechains)")
    parser.add_argument("--tmp", required=True, type=Path, help="folder for the data directory (made anew)")
    parser.add_argument("--shots", required=True, type=Path, help="folder for the pictures")
    parser.add_argument("--port", type=int, default=49410, help="ports of Chains: port, port+1 (RPC); Layers on port+10, port+11")
    args = parser.parse_args()

    if shutil.which("xvfb-run") is None:
        print("xvfb-run is needed", file=sys.stderr)
        return 2
    if not any((d / "layersd").is_file() for d in args.sidechain_bindir):
        print(f"no layersd in {args.sidechain_bindir}", file=sys.stderr)
        return 2
    rpc_port, sc_base = args.port + 1, args.port + 10
    taken = ports_taken([args.port, rpc_port, sc_base, sc_base + 1])
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
    page = base / "unified.js"
    page.write_text(script("unified.js"))
    failures = []
    app = App(args.app, ["-regtest", f"-port={args.port}", f"-rpcport={rpc_port}", "-listen=0", "-dnsseed=0", "-fixedseeds=0",
                         "-fallbackfee=0.0002", f"-datadir={datadir}", f"-webuiconfig={base / 'chains-app.json'}",
                         *[f"-sidechainbindir={d.resolve()}" for d in args.sidechain_bindir], f"-sidechainportbase={sc_base}", "-sidechainarg=-listen=0",
                         f"-webuitestshots={args.shots.resolve()}", f"-webuitestscript={page}"], env, base / "unified.log")
    seen_daemon = None
    try:
        # Chains alone.
        if app.wait_event("CHECKPOINT", 300) is None:
            failures.append("no checkpoint standalone")
        else:
            pid = find_app(app.proc.pid)
            names = threads(pid) if pid else []
            print(f"  standalone: chains-app {pid}, {len(names)} threads", flush=True)
            if ports_taken([rpc_port]):
                failures.append("standalone: the RPC port of Chains is open")
            if any("webui-sidecha" in n for n in names):
                failures.append("standalone: the thread of the supervisor runs")
            if daemons(datadir):
                failures.append(f"standalone: daemons run: {daemons(datadir)}")
            app.events.pop("CHECKPOINT", None)
        # Layers on and in step: a few blocks of it, merged mined (as a miner of Chains would).
        if app.wait_event("CHECKPOINT", 600) is None:
            failures.append("no checkpoint bmm")
        else:
            layers_cli = next(d / "layers-cli" for d in args.sidechain_bindir if (d / "layersd").is_file())
            try:
                bmm_blocks(args.app.parent / "chains-cli", layers_cli, datadir, rpc_port, sc_base, 3)
                print("  bmm: 3 blocks of Layers", flush=True)
            except (RuntimeError, subprocess.TimeoutExpired, StopIteration) as e:
                failures.append(f"bmm: {e}")
            app.events.pop("CHECKPOINT", None)
        # Layers on.
        if app.wait_event("CHECKPOINT", 600) is None:
            failures.append("no checkpoint enabled")
        else:
            pid = find_app(app.proc.pid)
            running = daemons(datadir)
            print(f"  enabled: daemons {list(running)}", flush=True)
            if len(running) != 1:
                failures.append(f"enabled: {len(running)} daemons, not 1")
            for dpid, (ppid, argv) in running.items():
                seen_daemon = dpid
                if ppid != pid:
                    failures.append(f"enabled: the daemon is not a child of chains-app ({ppid} != {pid})")
                for wanted in [f"-datadir={datadir}/sidechains/layers/data", "-regtest", "-sidechainslot=7", f"-mainchainrpcport={rpc_port}",
                               "-mainchainrpcconnect=127.0.0.1", "-mainchainrpcuser=chains-app-sidechains", f"-rpcport={sc_base}", "-listen=0"]:
                    if wanted not in argv:
                        failures.append(f"enabled: {wanted} not given to the daemon: {argv}")
                if any(a.startswith("-mainchainrpcpassword=") or a.startswith("-rpcpassword") for a in argv):
                    failures.append("enabled: a password on the command line of the daemon")
            if not ports_taken([rpc_port]):
                failures.append("enabled: the RPC server of Chains does not listen")
            if not ports_taken([sc_base]):
                failures.append("enabled: Layers does not listen on its RPC port")
            if pid and not any("webui-sidecha" in n for n in threads(pid)):
                failures.append("enabled: no thread of the supervisor")
            app.events.pop("CHECKPOINT", None)
        # Layers off.
        if app.wait_event("CHECKPOINT", 600) is None:
            failures.append("no checkpoint disabled")
        else:
            if daemons(datadir):
                failures.append(f"disabled: daemons still run: {daemons(datadir)}")
            log = datadir / "sidechains" / "layers" / "data" / "regtest" / "debug.log"
            if "Shutdown done" not in (log.read_text(errors="replace") if log.exists() else ""):
                failures.append(f"disabled: no clean shutdown of Layers in {log}")
        code = app.wait(600)
    finally:
        app.stop()
    result = app.result()
    if code != 0 or not result or result["failures"]:
        failures.append(f"unified run: exit {code}, {result['failures'] if result else 'no result'}; the page last said: {app.last}")
    check_shutdown(datadir, failures)
    # Nothing left running: a daemon left over is a failure, and is stopped (only ours, by pid).
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
