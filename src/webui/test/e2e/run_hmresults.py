#!/usr/bin/env python3
# Copyright (c) 2026 The Chains developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end test of what Hivemind's markets came to, shown at a glance (doc/unified-app.md §24).

Starts chains-app on regtest (a temporary data directory, a virtual display) with hmresults.js in its page and Hivemind's
daemon from --sidechain-bindir. The page turns Hivemind on and moves coins into it; at "CHECKPOINT hivemind results"
this runner makes markets at every stage with Hivemind's own CLI, as its pages would: decisions and markets on them (yes
or no, a scale, several outcomes), purchases of the wallet in each, the genesis votecoin of regtest in the wallet, its
ballot sealed and revealed, blocks merged-mined until the vote resolves them. Then:

  - trading:       "The bridge this year" (its decision far ahead);
  - voted on:      "The launch on time" (its vote lasts three periods);
  - partly decided: "The marathon" (Carl voted no, Dan's vote still on);
  - paid out:      "Rain in Paris on Sunday" (yes: the market's favourite), "Snow in Oslo on Sunday" (no: an upset),
                   "Goals in the cup final" (a scale, 4 of 0 to 10), "The regatta" (Ann of Ann and Bob).

The trades the pages would have recorded are written to Hivemind's journal (hivemind-journal.json in its data directory,
as hivemind.store.journal writes it) before its pages are first opened: what each paid out market made the wallet. The page
checks the app's Markets page and Home's open positions and pictures Hivemind's own pages (Markets, every market, Simple
mode), the app's Markets page and Home, in Midnight, Dark and Light.

    run_hmresults.py --app build_app/bin/chains-app --sidechain-bindir ~/skydoge/tools/unified-bin --tmp <dir> --shots <dir>

Nothing outside --tmp is touched; the nodes use ports of their own (--port); only processes started here are stopped.
"""

import argparse
import json
import os
import shutil
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from run_smoke import App, check_shutdown, environment, ports_taken, script  # noqa: E402
from run_unified import cli, daemons  # noqa: E402
from run_packs import SIDECHAINS, bmm  # noqa: E402

SIDECHAINS.update({"hivemind": (9, 6)})
#: The votecoin of regtest's genesis (Hivemind's test/functional/feature_hivemind.py): all of the reputation.
GENESIS_VOTECOIN_KEY = "cPfsxwfwtqceBD1MsHSt2q9r6bquHNTixtVGUKubYmYySGcSEynR"
#: Periods of regtest: 6 blocks, votes sealed in the first 4.
PERIOD_BLOCKS, SEAL_BLOCKS = 6, 4


def results_fixture(side_cli, datadir, port, bmm_one, say):
    """Markets at every stage on Hivemind, by its own CLI; returns their ids by name."""
    side = [f"-datadir={datadir}/sidechains/hivemind/data", "-regtest", f"-rpcport={port}", "-rpcwallet=main"]

    def c(*args):
        return cli(side_cli, side + [a if isinstance(a, str) else json.dumps(a) for a in args])

    def clock():
        h = c("getblockcount") + 1
        return h // PERIOD_BLOCKS, "reveal" if h % PERIOD_BLOCKS >= SEAL_BLOCKS else "seal"

    def advance(period, phase="seal"):
        while clock() != (period, phase):
            if clock()[0] > period:
                raise RuntimeError(f"past period {period} ({phase}): {clock()}")
            bmm_one()

    # The descriptor with its private key (getdescriptorinfo's own "descriptor" is the public one): its checksum added.
    desc = f"wpkh({GENESIS_VOTECOIN_KEY})#" + c("getdescriptorinfo", f"wpkh({GENESIS_VOTECOIN_KEY})")["checksum"]
    if not c("importdescriptors", [{"desc": desc, "timestamp": "now"}])[0]["success"]:
        raise RuntimeError("the genesis votecoin could not be imported")
    voter = c("listreputation")[0]["account"]
    p0 = c("gethivemindinfo")["period"]
    event = p0 + 2
    d = {}

    def decide(key, text, at, periods=1, scale=None):
        d[key] = c("createdecision", text, at, periods, *([True, scale[0], scale[1]] if scale else []))["id"]

    decide("rain", "Will it rain in Paris on Sunday?", event)
    decide("snow", "Will it snow in Oslo on Sunday?", event)
    decide("goals", "How many goals are scored in the cup final?", event, 1, (0, 10))
    decide("ann", "Who wins the regatta? — Ann", event)
    decide("bob", "Who wins the regatta? — Bob", event)
    decide("carl", "Who wins the marathon? — Carl", event)
    decide("dan", "Who wins the marathon? — Dan", event, 3)
    decide("launch", "Does the launch happen on time?", event, 3)
    decide("bridge", "Does the bridge open this year?", p0 + 40)
    bmm_one()
    m = {}

    def market(key, title, keys, liquidity):
        m[key] = c("createmarket", title, [d[k] for k in keys], liquidity)["id"]

    market("rain", "[Weather] Rain in Paris on Sunday", ["rain"], 2)
    market("snow", "[Weather] Snow in Oslo on Sunday", ["snow"], 2)
    market("goals", "[Sports] Goals in the cup final", ["goals"], 3)
    market("regatta", "[Sports] The regatta", ["ann", "bob"], 2)
    market("marathon", "[Sports] The marathon", ["carl", "dan"], 2)
    market("launch", "[Space] The launch on time", ["launch"], 2)
    market("bridge", "The bridge this year", ["bridge"], 2)
    bmm_one()
    opened = c("getblockcount")
    now = int(time.time())
    trades = []

    def buy(key, state, shares):
        r = c("buyshares", m[key], state, f"{shares:.8f}")
        trades.append({"wallet": "main", "market": m[key], "state": state, "shares": shares, "coins": r["quote"], "time": now, "txid": r["txid"], "account": r["account"], "status": "done"})

    buy("rain", 1, 3)
    buy("snow", 1, 2.5)
    buy("goals", 1, 1)
    buy("regatta", 1, 2)
    buy("regatta", 2, 0.5)
    buy("marathon", 1, 1)
    buy("launch", 1, 1.5)
    buy("bridge", 1, 1)
    bmm_one()
    traded = c("getblockcount")
    # The prices the pages would have seen: even when the markets opened, then after the purchases.
    prices = {}
    for mk in c("listmarkets"):
        if mk["id"] in m.values():
            n = len(mk["states"])
            prices[mk["id"]] = [{"h": opened, "t": now, "p": [round(10000 / n)] * n}, {"h": traded, "t": now, "p": [round(s["price"] * 10000) for s in mk["states"]]}]
    journal = datadir / "sidechains" / "hivemind" / "data" / "regtest" / "hivemind-journal.json"
    journal.write_text(json.dumps({"votes": [], "trades": trades, "prices": prices, "rep": {}, "reminders": False, "notified": [], "mine": {"main": list(m.values())}, "pending": []}))
    say(f"  {len(trades)} purchases; the journal of the pages written ({journal})")

    # The vote: sealed in the period after the event, revealed in its last third; the next period's first block resolves.
    advance(event + 1, "seal")
    ballot = {d["rain"]: "yes", d["snow"]: "no", d["goals"]: "4", d["ann"]: "yes", d["bob"]: "no", d["carl"]: "no"}
    c("sealballot", voter, ballot)
    bmm_one()
    advance(event + 1, "reveal")
    c("revealballot", voter, ballot)
    bmm_one()
    advance(event + 2, "seal")
    bmm_one()
    status = {mk["text"]: mk["status"] for mk in c("listmarkets")}
    say(f"  markets: {status}")
    return m


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--sidechain-bindir", required=True, type=Path, action="append", help="a folder with hivemindd and hivemind-cli")
    parser.add_argument("--tmp", required=True, type=Path, help="folder for the data directory (made anew)")
    parser.add_argument("--shots", required=True, type=Path, help="folder for the pictures")
    parser.add_argument("--port", type=int, default=49580, help="ports of Chains: port, port+1 (RPC); the sidechains from port+10")
    parser.add_argument("--window", default="1320x1240", help="the size of the app's window")
    args = parser.parse_args()

    if shutil.which("xvfb-run") is None:
        print("xvfb-run is needed", file=sys.stderr)
        return 2
    found = next((d for d in args.sidechain_bindir if (d / "hivemindd").is_file() and (d / "hivemind-cli").is_file()), None)
    if not found:
        print(f"no hivemindd in {args.sidechain_bindir}", file=sys.stderr)
        return 2
    side_cli = found / "hivemind-cli"
    rpc_port, sc_base = args.port + 1, args.port + 10
    hm_port = sc_base + 2 * SIDECHAINS["hivemind"][1]
    taken = ports_taken([args.port, rpc_port, hm_port, hm_port + 1])
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
    width, height = (int(x) for x in args.window.split("x"))
    (base / "chains-app.json").write_text(json.dumps({"window": {"width": width, "height": height, "maximized": False}}))
    page = base / "hmresults.js"
    page.write_text(script("hmresults.js"))
    chains_cli = args.app.parent / "chains-cli"
    failures = []
    app = App(args.app, ["-regtest", f"-port={args.port}", f"-rpcport={rpc_port}", "-listen=0", "-dnsseed=0", "-fixedseeds=0",
                         "-fallbackfee=0.0002", f"-datadir={datadir}", f"-webuiconfig={base / 'chains-app.json'}",
                         *[f"-sidechainbindir={d.resolve()}" for d in args.sidechain_bindir], f"-sidechainportbase={sc_base}", "-sidechainarg=-listen=0",
                         "-sidechainarg=-fallbackfee=0.0002",
                         f"-webuitestshots={args.shots.resolve()}", f"-webuitestscript={page}"], env, base / "hmresults.log", screen=f"{width + 80}x{height + 60}")
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
                    bmm(chains_cli, side_cli, datadir, rpc_port, stem, hm_port, int(count))
                elif what == "hivemind" and stem == "results":
                    one = lambda: bmm(chains_cli, side_cli, datadir, rpc_port, "hivemind", hm_port, 1)  # noqa: E731
                    made = results_fixture(side_cli, datadir, hm_port, one, lambda s: print(s, flush=True))
                    (base / "markets.json").write_text(json.dumps(made))
            except (RuntimeError, OSError, KeyError, ValueError, TypeError) as e:
                failures.append(f"{what} {stem}: {e}")
                print(f"  FAILED {what} {stem}: {e}", flush=True)
            if "RESULT" in app.events:
                break
        code = app.wait(900)
    finally:
        app.stop()
    result = app.result()
    if code != 0 or not result or result["failures"]:
        failures.append(f"results run: exit {code}, {result['failures'] if result else 'no result'}; the page last said: {app.last}")
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
