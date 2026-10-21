#!/usr/bin/env python3
# Copyright (c) 2026 The Chains developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""The app over every chain with a lived-in state, for its pictures and its idle cost (doc/unified-app.md §20).

Starts chains-app on regtest in a virtual display with software GL (as under WSLg), with the daemons of Layers, Thunder,
BitAssets and Hivemind from --sidechain-bindir, and showcase.js in its page. Two phases:

  setup   a chain with the four sidechains active and turned on, coins deposited into each, an asset on Layers, a
          market traded on Hivemind, payments with labels on Chains, a withdrawal from Layers on its way back, many
          merged-mined blocks (their small rewards in the sidechains' wallets). The data directory is kept in --state.
  run     from a copy of --state: --perf measures what the app costs while nothing happens (the processor time of the
          app, its web process and its network process, sampled from /proc) on Home with every chain shown, then on
          each page; --shots takes the pictures of every page (each mode, look and size asked in --matrix).

    run_showcase.py --app build_app/bin/chains-app --sidechain-bindir ~/skydoge/layers/build/bin \\
        --sidechain-bindir ~/skydoge/thunder/build/bin --sidechain-bindir ~/skydoge/bitassets/build/bin \\
        --sidechain-bindir ~/skydoge/hivemind/build/bin --state <dir> --tmp <dir> --shots <dir> [--perf] [--budget 5]

With --perf and --budget, fails when the idle cost of the app on Home (its processes together, in % of one core) is
over the budget. Nothing outside --tmp and --state is touched; only processes started here are stopped.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run_smoke import App, check_shutdown, environment, ports_taken, script  # noqa: E402
from run_unified import cli, daemons  # noqa: E402
from run_packs import SIDECHAINS, bmm  # noqa: E402

SIDECHAINS.update({"bitassets": (4, 4), "hivemind": (9, 6)})
STEMS = ("layers", "thunder", "bitassets", "hivemind")


def tree(root):
    """The processes under root (itself included): {pid: comm}."""
    children = {}
    comms = {}
    for entry in Path("/proc").iterdir():
        if not entry.name.isdigit():
            continue
        try:
            raw = (entry / "stat").read_text()
        except OSError:
            continue
        comm = raw[raw.index("(") + 1:raw.rindex(")")]
        fields = raw.rsplit(")", 1)[1].split()
        pid = int(entry.name)
        children.setdefault(int(fields[1]), []).append(pid)
        comms[pid] = comm
    out = {}
    todo = [root]
    while todo:
        pid = todo.pop()
        if pid in comms:
            out[pid] = comms[pid]
        todo.extend(children.get(pid, []))
    return out


def cpu_ticks(pid):
    try:
        fields = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
        return int(fields[11]) + int(fields[12])
    except (OSError, IndexError, ValueError):
        return None


def group(comm):
    """The part of the app a process is: the app (and its node), its web process, its network process, X, a sidechain."""
    if comm.startswith("WebKitWebProces"):
        return "web"
    if comm.startswith("WebKitNetworkPr"):
        return "network"
    if comm.startswith("WebKitGPUProces"):
        return "gpu"
    if comm == "chains-app":
        return "app"
    if comm.startswith("Xvfb"):
        return "xvfb"
    if comm.endswith("d") and any(comm.startswith(s) for s in STEMS):
        return "sidechains"
    return "other"


def threads_of(pid):
    """The threads of a process: {name: ticks}, summed by name."""
    out = {}
    try:
        tasks = list(Path(f"/proc/{pid}/task").iterdir())
    except OSError:
        return out
    for task in tasks:
        try:
            raw = (task / "stat").read_text()
        except OSError:
            continue
        name = raw[raw.index("(") + 1:raw.rindex(")")]
        fields = raw.rsplit(")", 1)[1].split()
        out[name] = out.get(name, 0) + int(fields[11]) + int(fields[12])
    return out


def measure(root, seconds):
    """The processor time of each part of the app over `seconds`, in % of one core (and the busiest threads of the web process)."""
    tick = os.sysconf("SC_CLK_TCK")
    before = {pid: (comm, cpu_ticks(pid)) for pid, comm in tree(root).items()}
    web = [pid for pid, (comm, _) in before.items() if comm.startswith("WebKitWebProces")]
    threads_before = {pid: threads_of(pid) for pid in web}
    start = time.time()
    time.sleep(seconds)
    elapsed = time.time() - start
    after = {pid: (comm, cpu_ticks(pid)) for pid, comm in tree(root).items()}
    threads = {}
    for pid in web:
        now = threads_of(pid)
        for name, ticks in now.items():
            threads[name] = threads.get(name, 0) + ticks - threads_before[pid].get(name, 0)
    used = {}
    detail = {}
    for pid, (comm, ticks) in after.items():
        if ticks is None:
            continue
        was = before.get(pid, (comm, 0))[1] or 0
        used[group(comm)] = used.get(group(comm), 0) + (ticks - was)
        if ticks - was:
            detail[comm] = detail.get(comm, 0) + (ticks - was)
    out = {k: round(100.0 * v / tick / elapsed, 1) for k, v in sorted(used.items())}
    out["by_process"] = {k: round(100.0 * v / tick / elapsed, 1) for k, v in sorted(detail.items(), key=lambda kv: -kv[1])[:8]}
    out["web_threads"] = {k: round(100.0 * v / tick / elapsed, 1) for k, v in sorted(threads.items(), key=lambda kv: -kv[1])[:6] if v > 0}
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--sidechain-bindir", required=True, type=Path, action="append")
    parser.add_argument("--state", required=True, type=Path, help="the prepared data (made by the setup phase when missing)")
    parser.add_argument("--tmp", required=True, type=Path, help="folder for the run (made anew)")
    parser.add_argument("--shots", required=True, type=Path)
    parser.add_argument("--port", type=int, default=49610, help="Chains: port, port+1; the sidechains from port+10")
    parser.add_argument("--screen", default="1400x900", help="the virtual display (WxH)")
    parser.add_argument("--perf", action="store_true", help="measure the idle cost of the app")
    parser.add_argument("--perf-seconds", type=int, default=60)
    parser.add_argument("--budget", type=float, default=None, help="fail when the app costs more than this (%% of one core) idle on Home")
    parser.add_argument("--shots-matrix", default="", help="what to picture: comma-separated mode:look:size (advanced:midnight:1180x760,...)")
    parser.add_argument("--pages", default="", help="the pages measured or pictured (default: every page)")
    parser.add_argument("--busy", action="store_true", help="also measure Home while blocks and payments come (a busy network)")
    parser.add_argument("--maximize", action="store_true", help="the window maximized to the virtual display")
    parser.add_argument("--real-display", action="store_true", help="on the user's desktop (WSLg: Wayland, software GL) as the hub runs it, not a virtual display")
    parser.add_argument("--setup-only", action="store_true")
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
    ports = {stem: sc_base + 2 * SIDECHAINS[stem][1] for stem in programs}
    taken = ports_taken([args.port, rpc_port] + [p for s in ports.values() for p in (s, s + 1)])
    if taken:
        print(f"ports {taken} are taken: give another --port", file=sys.stderr)
        return 2
    chains_cli = args.app.parent / "chains-cli"
    failures = []
    state = args.state.resolve()

    def start(base, phase, extra_prelude):
        env = environment(base)
        if args.real_display:
            # As launch.sh runs the hub: the desktop's own display, software GL, WebKit's own renderer.
            runtime = os.environ.get("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}")
            if os.environ.get("WAYLAND_DISPLAY"):
                env["WAYLAND_DISPLAY"] = os.path.join(runtime, os.environ["WAYLAND_DISPLAY"])
            env["DISPLAY"] = os.environ.get("DISPLAY", ":0")
            env.pop("WEBKIT_DISABLE_DMABUF_RENDERER", None)
        page = base / "showcase.js"
        page.write_text(script("showcase.js", f"window.__SHOWCASE = {json.dumps({'phase': phase, **extra_prelude})};\n"))
        datadir = base / "datadir"
        datadir.mkdir(exist_ok=True)
        app_args = ["-regtest", f"-port={args.port}", f"-rpcport={rpc_port}", "-listen=0", "-dnsseed=0", "-fixedseeds=0",
                    "-fallbackfee=0.0002", f"-datadir={datadir}", f"-webuiconfig={base / 'chains-app.json'}",
                    *[f"-sidechainbindir={d.resolve()}" for d in args.sidechain_bindir], f"-sidechainportbase={sc_base}", "-sidechainarg=-listen=0",
                    "-sidechainarg=-fallbackfee=0.0002", f"-webuitestshots={args.shots.resolve()}", f"-webuitestscript={page}"]
        app = App(args.app, app_args, env, base / f"{phase}.log", screen=args.screen, virtual=not args.real_display)
        return app, datadir

    def follow(app, datadir, measures):
        while True:
            message = app.wait_event("CHECKPOINT", 3600)
            if message is None:
                return
            app.events.pop("CHECKPOINT", None)
            parts = message.split()
            what = parts[1]
            try:
                if what == "bmm":
                    bmm(chains_cli, programs[parts[2]], datadir, rpc_port, parts[2], ports[parts[2]], int(parts[3]))
                elif what == "hivemind":
                    side = [f"-datadir={datadir}/sidechains/hivemind/data", "-regtest", f"-rpcport={ports['hivemind']}", "-rpcwallet=main"]
                    one = lambda: bmm(chains_cli, programs["hivemind"], datadir, rpc_port, "hivemind", ports["hivemind"], 1)  # noqa: E731
                    hm = programs["hivemind"]
                    info = cli(hm, side + ["gethivemindinfo"])
                    decision = cli(hm, side + ["createdecision", "Will it rain tomorrow?", str(info["period"] + 2)])
                    one()
                    market = cli(hm, side + ["createmarket", "Rain tomorrow?", json.dumps([decision["id"]]), "10"])
                    one()
                    cli(hm, side + ["buyshares", market["id"], "1", "3"])
                    one()
                elif what == "bigside":
                    # A long history on a sidechain's wallet: 300 payments to itself, merged-mined in a few blocks.
                    stem = parts[2]
                    side = [f"-datadir={datadir}/sidechains/{stem}/data", "-regtest", f"-rpcport={ports[stem]}", "-rpcwallet=main"]
                    for i in range(300):
                        cli(programs[stem], side + ["sendtoaddress", cli(programs[stem], side + ["getnewaddress"]), "0.001"])
                        # A chain of unconfirmed payments is 25 long at most in a mempool: a block every 20.
                        if i % 20 == 19:
                            bmm(chains_cli, programs[stem], datadir, rpc_port, stem, ports[stem], 1)
                    bmm(chains_cli, programs[stem], datadir, rpc_port, stem, ports[stem], 1)
                elif what in ("idle", "busy"):
                    name = parts[2]
                    seconds = int(parts[3])
                    stop = threading.Event()
                    worker = None
                    if what == "busy":
                        # As on a busy network: a payment every 2 s on Chains and a block every 10 s, with a block of each
                        # sidechain merged-mined in turn.
                        def busy():
                            chains = [f"-datadir={datadir}", "-regtest", f"-rpcport={rpc_port}"]
                            to = cli(chains_cli, chains + ["-rpcwallet=friend", "getnewaddress"])
                            mining = cli(chains_cli, chains + ["-rpcwallet=demo", "getnewaddress"])
                            n = 0
                            errors = []
                            while not stop.is_set():
                                try:
                                    cli(chains_cli, chains + ["-rpcwallet=demo", "sendtoaddress", to, "0.01"])
                                    n += 1
                                    if n % 5 == 0:
                                        stem = STEMS[(n // 5) % len(STEMS)]
                                        bmm(chains_cli, programs[stem], datadir, rpc_port, stem, ports[stem], 1)
                                    elif n % 5 == 3:
                                        cli(chains_cli, chains + ["generatetoaddress", "1", mining])
                                except (RuntimeError, subprocess.TimeoutExpired) as e:
                                    errors.append(str(e)[:120])
                                stop.wait(2)
                            print(f"  busy: {n} payments, {n // 5} blocks merged-mined, {len(errors)} errors {errors[:2]}", flush=True)
                        worker = threading.Thread(target=busy, daemon=True)
                        worker.start()
                    numbers = measure(app.proc.pid, seconds)
                    stop.set()
                    if worker:
                        worker.join(timeout=60)
                    numbers["total"] = round(sum(v for k, v in numbers.items() if k in ("app", "web", "network", "gpu", "other")), 1)
                    measures[f"{what}:{name}"] = numbers
                    print(f"  {what} {name}: {json.dumps(numbers)}", flush=True)
            except (RuntimeError, subprocess.TimeoutExpired, KeyError, ValueError, TypeError) as e:
                failures.append(f"{what}: {e}")
            if "RESULT" in app.events:
                return

    def finish(app, datadir, label):
        code = app.wait(900)
        app.stop()
        result = app.result()
        if code != 0 or not result or result["failures"]:
            failures.append(f"{label}: exit {code}, {result['failures'] if result else 'no result'}; the page last said: {app.last}")
        check_shutdown(datadir, failures)
        time.sleep(1)
        for dpid in daemons(datadir):
            failures.append(f"a sidechain daemon was left running ({dpid}): stopped")
            os.kill(dpid, 15)
        return result

    args.shots.mkdir(parents=True, exist_ok=True)
    if not (state / "datadir").is_dir():
        print("== setup", flush=True)
        base = state
        if base.exists():
            shutil.rmtree(base)
        base.mkdir(parents=True)
        app, datadir = start(base, "setup", {})
        try:
            follow(app, datadir, {})
        finally:
            result = finish(app, datadir, "setup")
        if failures:
            print("FAILED:\n  " + "\n  ".join(failures), flush=True)
            shutil.rmtree(state / "datadir", ignore_errors=True)
            return 1
        print(f"  setup done: {result['passed'] if result else 0} steps", flush=True)
    if args.setup_only:
        return 0

    print("== run", flush=True)
    base = args.tmp.resolve()
    if base.exists():
        shutil.rmtree(base)
    # Not the app's own files of the setup (its log, its script, its home): the data directory whole (LevelDB keeps its
    # recent writes in files named *.log).
    shutil.copytree(state, base, symlinks=True, ignore=lambda folder, names: [n for n in names if Path(folder) == state and n in ("setup.log", "run.log", "showcase.js", "home", "runtime")])
    # Maximized: the size of the virtual display, as the app keeps it (a virtual display has no window manager to
    # maximize a window).
    if args.maximize and not args.real_display:
        width, height = (int(x) for x in args.screen.split("x"))
        config_file = base / "chains-app.json"
        config = json.loads(config_file.read_text()) if config_file.exists() else {}
        config["window"] = {"width": width, "height": height, "maximized": False}
        config_file.write_text(json.dumps(config))
    measures = {}
    matrix = [m for m in args.shots_matrix.split(",") if m]
    app, datadir = start(base, "run", {"perf": args.perf, "perfSeconds": args.perf_seconds, "matrix": matrix, "pages": [p for p in args.pages.split(",") if p], "busy": args.busy, "maximize": args.maximize})
    try:
        follow(app, datadir, measures)
    finally:
        result = finish(app, datadir, "run")
    if args.perf:
        (args.shots / "perf-idle.json").write_text(json.dumps(measures, indent=2))
        home = measures.get("idle:home")
        if args.budget is not None:
            if not home:
                failures.append("no idle measure of Home")
            elif home["total"] > args.budget:
                failures.append(f"idle on Home the app costs {home['total']}% of one core, over the budget of {args.budget}%: {json.dumps(home)}")
    if result:
        print(f"  {result['passed']} steps, {len(result['shots'])} pictures in {args.shots}", flush=True)
    if failures:
        print("FAILED:\n  " + "\n  ".join(failures), flush=True)
        return 1
    print("OK", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
