# Copyright (c) 2026 The Chains developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""A state the size of the hub's (doc/unified-app.md §21), for run_showcase.py --scale hub.

The hub (2026-10-21): Chains' wallet with ~28 000 transactions (19 600 sends, mostly the bids of merged mining, 7 900
mining rewards, a few hundred received); Layers with 35 assets (26 with a logo) and ~2 300 transactions in its wallet;
BitAssets with 63 assets (28 logos) and ~900 transactions; Hivemind with 90 markets, 79 decisions and a few open
positions. Made here on regtest through each node's own RPC (JSON-RPC batches, the cookie of its data directory), while
the app runs: the app sees it arrive as it would.
"""

import base64
import colorsys
import json
import math
import random
import struct
import time
import urllib.error
import urllib.request
import zlib
from pathlib import Path


class Rpc:
    """JSON-RPC to a node of the run (the cookie of its data directory), one call or a batch."""

    def __init__(self, port, cookie, wallet=None, timeout=900):
        self.port = port
        self.cookie = Path(cookie)
        self.wallet_name = wallet
        self.timeout = timeout
        self.url = f"http://127.0.0.1:{port}/" + (f"wallet/{wallet}" if wallet else "")

    def wallet(self, name):
        return Rpc(self.port, self.cookie, name, self.timeout)

    def _post(self, payload):
        auth = base64.b64encode(self.cookie.read_text().strip().encode()).decode()
        request = urllib.request.Request(self.url, data=json.dumps(payload).encode(), headers={"Content-Type": "application/json", "Authorization": f"Basic {auth}"})
        try:
            with urllib.request.urlopen(request, timeout=self.timeout) as answer:
                return json.loads(answer.read())
        except urllib.error.HTTPError as e:
            body = e.read()
            try:
                return json.loads(body)
            except ValueError:
                raise RuntimeError(f"HTTP {e.code}: {body[:200]!r}") from e

    def call(self, method, *params):
        out = self._post({"jsonrpc": "1.0", "id": 0, "method": method, "params": list(params)})
        if out.get("error"):
            raise RuntimeError(f"{method}: {out['error']}")
        return out["result"]

    def batch(self, calls, chunk=250):
        """[(method, params)] → [(result, error)], in order."""
        out = []
        for start in range(0, len(calls), chunk):
            part = calls[start:start + chunk]
            answers = self._post([{"jsonrpc": "1.0", "id": i, "method": m, "params": list(p)} for i, (m, p) in enumerate(part)])
            if isinstance(answers, dict):
                raise RuntimeError(f"batch: {answers}")
            for a in sorted(answers, key=lambda a: a["id"]):
                out.append((a.get("result"), a.get("error")))
        return out


def png_logo(i):
    """A logo the chains take: a PNG of 256 × 256 (IHDR, IDAT, IEND only), 15 to 28 kB, a different one for each i."""
    rnd = random.Random(i)
    hue = (i * 0.137) % 1.0
    cx, cy = rnd.randint(70, 186), rnd.randint(70, 186)
    step = 1 if i % 2 else 2
    prev = bytes(256 * 3)
    rows = []
    for y in range(256):
        cur = bytearray()
        for x in range(256):
            d = math.hypot(x - cx, y - cy) / 256
            ring = 0.5 + 0.5 * math.sin(d * 14 + i)
            r, g, b = colorsys.hsv_to_rgb((hue + d * 0.25) % 1, 0.5 + 0.4 * ring, 0.5 + 0.45 * (1 - d))
            cur += bytes((int(c * 255) // step) * step for c in (r, g, b))
        rows.append(b"\x02" + bytes((cur[k] - prev[k]) & 255 for k in range(len(cur))))
        prev = bytes(cur)

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 256, 256, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b"")


class Hub:
    """The nodes of a run: Chains (its wallets demo and friend) and the sidechains turned on, by RPC."""

    def __init__(self, datadir, rpc_port, ports, slots, log=print):
        self.datadir = Path(datadir)
        self.chains = Rpc(rpc_port, self.datadir / "regtest" / ".cookie")
        self.demo = self.chains.wallet("demo")
        self.friend = self.chains.wallet("friend")
        self.side = {stem: Rpc(port, self.datadir / "sidechains" / stem / "data" / "regtest" / ".cookie") for stem, port in ports.items()}
        self.slots = slots
        self.log = log
        self.mining = self.demo.call("getnewaddress", "Mining")

    def main(self, stem):
        return self.side[stem].wallet("main")

    def bmm(self, stem, count=1, bid=0.001):
        """Blocks of a sidechain by blind merged mining (as run_packs.bmm): a bid of Chains' wallet for each, a block of Chains."""
        side, main = self.side[stem], self.main(stem)
        for _ in range(count):
            block = side.call("createbmmblock", main.call("getnewaddress"))
            self.demo.call("createbmmrequest", self.slots[stem], block["hash"], bid)
            self.chains.call("generatetoaddress", 1, self.mining)
            until = time.time() + 60
            while time.time() < until and side.call("getbestblockhash") != block["hash"]:
                time.sleep(0.2)

    def spread(self, wallet, count, amount, mine):
        """Coins of a wallet split into `count` outputs of its own, confirmed: payments that follow do not chain."""
        outputs = {a: amount for a in (r for r, _ in wallet.batch([("getnewaddress", [])] * count))}
        wallet.call("sendmany", "", outputs)
        mine()

    def payments(self, wallet, to, count, mine, every=400, labels=()):
        """`count` payments of the wallet to the addresses `to` (cycled), a block every `every`."""
        done = errors = 0
        while done < count:
            n = min(every, count - done)
            calls = [("sendtoaddress", [to[(done + i) % len(to)], round(0.0001 + ((done + i) % 97) * 0.00001, 8)]) for i in range(n)]
            for _, error in wallet.batch(calls):
                errors += 1 if error else 0
            done += n
            mine()
        return errors


def make(hub, stems, scale=1.0):
    """The hub's sizes (times `scale`): Chains' wallet, the sidechains' wallets, assets with logos, Hivemind's markets.
    A part that fails is said and the next is made (the state is still worth measuring)."""
    log = hub.log
    errors = []
    for part in (_chains, _sidechains, _assets, _hivemind, _mined):
        try:
            part(hub, stems, scale, log)
        except (RuntimeError, OSError, KeyError, ValueError, TypeError) as e:
            errors.append(f"{part.__name__}: {e}")
            log(f"  hub: {part.__name__} failed: {e}")
    info = hub.demo.call("getwalletinfo")
    log(f"  hub: done: Chains' wallet {info['txcount']} transactions; {len(errors)} part(s) failed")
    return info["txcount"], errors


def _chains(hub, stems, scale, log):
    t0 = time.time()
    n = lambda x: max(1, int(x * scale))  # noqa: E731
    mine_chains = lambda: hub.chains.call("generatetoaddress", 1, hub.mining)  # noqa: E731

    # Chains: coins spread, then ~17 000 payments (most of the hub's records are sends: the bids of merged mining).
    hub.spread(hub.demo, 1500, 0.5, mine_chains)
    friend_to = [r for r, _ in hub.friend.batch([("getnewaddress", [f"Shop {i}" if i % 7 == 0 else ""]) for i in range(300)])]
    errors = hub.payments(hub.demo, friend_to, n(17000), mine_chains)
    log(f"  hub: Chains payments made ({errors} refused), {time.time() - t0:.0f} s")
    # Received: a few hundred, some labelled.
    demo_to = [r for r, _ in hub.demo.batch([("getnewaddress", [f"Customer {i}" if i % 5 == 0 else ""]) for i in range(300)])]
    hub.spread(hub.friend, 400, 0.02, mine_chains)
    hub.payments(hub.friend, demo_to, n(400), mine_chains, every=200)


def _sidechains(hub, stems, scale, log):
    t0 = time.time()
    n = lambda x: max(1, int(x * scale))  # noqa: E731
    # Sidechains: coins spread, payments in their wallets (Layers ~2 300 records, BitAssets ~900).
    # The other four when they are on too (doc/unified-app.md §25: every chain of the catalog at once): a few hundred each.
    for stem, count in (("layers", 2000), ("bitassets", 700), ("hivemind", 300), ("fastblocks", 300), ("thunder", 300), ("bitnames", 200), ("zside", 200)):
        if stem not in stems:
            continue
        main = hub.main(stem)
        hub.spread(main, 200, 0.02, lambda s=stem: hub.bmm(s))
        to = [r for r, _ in main.batch([("getnewaddress", [])] * 100)]
        errors = hub.payments(main, to, n(count), lambda s=stem: hub.bmm(s), every=200)
        log(f"  hub: {stem}: {n(count)} payments ({errors} refused), {time.time() - t0:.0f} s")


def _assets(hub, stems, scale, log):
    t0 = time.time()
    # Assets with logos: Layers 35 (26 logos), BitAssets 63 (28 logos), as the hub's.
    for stem, count, logos, prefix in (("layers", 35, 26, "LA"), ("bitassets", 63, 28, "BA")):
        if stem not in stems:
            continue
        main = hub.main(stem)
        names = [f"{prefix}{i:03d}" for i in range(count)]
        bad = [e for _, e in main.batch([("reserveasset", [name]) for name in names]) if e]
        hub.bmm(stem, 1)
        depth = hub.side[stem].call("getbitassetsinfo").get("revealdepth", 1)
        hub.bmm(stem, max(1, int(depth)))
        calls = []
        for i, name in enumerate(names):
            params = [name, 1000 + i * 37, i % 4]
            if i < logos:
                params += [{}, True, base64.b64encode(png_logo(i + (0 if stem == "layers" else 100))).decode()]
            calls.append(("registerasset", params))
        # One at a time a block: a logo is up to 32 kB, a few of them fill a block of a sidechain.
        registered = 0
        for start in range(0, len(calls), 8):
            for _, e in main.batch(calls[start:start + 8]):
                if e:
                    bad.append(e)
                else:
                    registered += 1
            hub.bmm(stem, 1)
        log(f"  hub: {stem}: {registered} assets registered ({len(bad)} refused: {bad[:2]}), {time.time() - t0:.0f} s")


def _hivemind(hub, stems, scale, log):
    t0 = time.time()
    # Hivemind: 80 decisions, 90 markets, 5 positions.
    if "hivemind" in stems:
        main = hub.main("hivemind")
        info = hub.side["hivemind"].call("gethivemindinfo")
        topics = ["rain", "the price of CHN", "a new sidechain", "the vote", "the block size", "the fee", "the next halving", "the merge", "the launch", "the summit"]
        decisions = []
        for start in range(0, 80, 20):
            got = main.batch([("createdecision", [f"Will {topics[i % len(topics)]} happen by period {info['period'] + 2 + i % 9}? (#{i})", info["period"] + 2 + i % 9]) for i in range(start, start + 20)])
            decisions += [r["id"] for r, e in got if not e and r]
            hub.bmm("hivemind", 1)
        markets = []
        for start in range(0, 90, 15):
            calls = []
            for i in range(start, start + 15):
                ds = [decisions[i % len(decisions)]] if i % 6 else [decisions[i % len(decisions)], decisions[(i + 1) % len(decisions)]]
                calls.append(("createmarket", [f"{'Sports' if i % 3 == 0 else 'Economy' if i % 3 == 1 else 'Tech'}: market {i}", ds, 1 + i % 4]))
            got = main.batch(calls)
            markets += [r["id"] for r, e in got if not e and r]
            hub.bmm("hivemind", 1)
        for i, m in enumerate(markets[:5]):
            try:
                main.call("buyshares", m, 1, str(1 + i))
            except RuntimeError as e:
                log(f"  hub: buyshares: {e}")
        hub.bmm("hivemind", 2)
        log(f"  hub: hivemind: {len(decisions)} decisions, {len(markets)} markets, {time.time() - t0:.0f} s")


def _mined(hub, stems, scale, log):
    t0 = time.time()
    n = lambda x: max(1, int(x * scale))  # noqa: E731
    # Mining rewards in Chains' wallet (~7 900 on the hub), the merged mining of every sidechain mixed in.
    blocks = n(8000)
    for start in range(0, blocks, 400):
        hub.chains.call("generatetoaddress", min(400, blocks - start), hub.mining)
    for _ in range(3):
        for stem in stems:
            hub.bmm(stem, 2)
    log(f"  hub: {blocks} blocks mined to Chains' wallet, {time.time() - t0:.0f} s")


def done(hub):
    """Tell the page the runner is done (an address labelled "hubscale-done"), made or not."""
    hub.demo.call("setlabel", hub.demo.call("getnewaddress"), "hubscale-done")
