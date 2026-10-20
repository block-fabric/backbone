#!/usr/bin/env python3
# Copyright (c) 2026 The eStack developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""The replay protection of the wallet on the eCash networks (-testecash).

- By default a new transaction has the magic locktime (499999998), with inputs that are not final,
  from sendtoaddress, send, sendall and walletcreatefundedpsbt; a fee bump keeps it.
- A locktime asked for is kept; -walletreplayprotection=0 gives anti-fee-sniping, as on the Chains networks.
- A coin locked by a timelock (after()) is spent without it: the magic locktime fails every
  OP_CHECKLOCKTIMEVERIFY.
- Splitting the coins: sendall to an address of the wallet.
"""
from test_framework.descriptors import descsum_create
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_greater_than

MAGIC = 499999998
FINAL = 0xffffffff


class EcashReplayTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.extra_args = [
            ["-testecash", "-fallbackfee=0.0002"],
            ["-testecash", "-fallbackfee=0.0002", "-walletreplayprotection=0"],
            ["-fallbackfee=0.0002"],  # a Chains network
        ]

    def setup_network(self):
        # The Chains node is a network of its own: it refuses what replay protection makes non-final there.
        self.setup_nodes()
        self.connect_nodes(0, 1)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def locktime(self, node, txid):
        tx = node.getrawtransaction(txid, True)
        assert all(vin["sequence"] != FINAL for vin in tx["vin"])
        return tx["locktime"]

    def run_test(self):
        ecash, no_protection, chains = self.nodes
        self.generate(ecash, 110, sync_fun=lambda: self.sync_all(self.nodes[:2]))
        self.generate(chains, 110, sync_fun=self.no_op)
        ecash.sendtoaddress(no_protection.getnewaddress(), 10)
        self.generate(ecash, 1, sync_fun=lambda: self.sync_all(self.nodes[:2]))

        self.log.info("Every way of making a transaction gives the magic locktime")
        txid = ecash.sendtoaddress(ecash.getnewaddress(), 1)
        assert_equal(self.locktime(ecash, txid), MAGIC)
        txid = ecash.send({ecash.getnewaddress(): 1})["txid"]
        assert_equal(self.locktime(ecash, txid), MAGIC)
        psbt = ecash.walletcreatefundedpsbt([], [{ecash.getnewaddress(): 1}])["psbt"]
        decoded = ecash.decodepsbt(psbt)
        assert_equal(decoded["tx"]["locktime"] if "tx" in decoded else decoded["fallback_locktime"], MAGIC)
        bumped = ecash.bumpfee(ecash.sendtoaddress(ecash.getnewaddress(), 1))["txid"]
        assert_equal(self.locktime(ecash, bumped), MAGIC)
        self.generate(ecash, 1, sync_fun=lambda: self.sync_all(self.nodes[:2]))

        self.log.info("A locktime asked for is kept")
        height = ecash.getblockcount()
        txid = ecash.send(outputs={ecash.getnewaddress(): 1}, locktime=height)["txid"]
        assert_equal(self.locktime(ecash, txid), height)

        self.log.info("Without replay protection, anti-fee-sniping; the same on a Chains network")
        for node in (no_protection, chains):
            txid = node.sendtoaddress(node.getnewaddress(), 1)
            lock = self.locktime(node, txid)
            assert lock != MAGIC and lock <= node.getblockcount()

        self.log.info("A timelocked coin (after()) is spent without the magic locktime")
        default = ecash.get_wallet_rpc(self.default_wallet_name)
        ecash.createwallet("timelock", blank=True)
        timelock = ecash.get_wallet_rpc("timelock")
        xprv = "tprv8ZgxMBicQKsPd7Uf69XL1XwhmjHopUGep8GuEiJDZmbQz6o58LninorQAfcKZWARbtRtfnLcJ5MQ2AtHcQJCCRUcMRvmDUjyEmNUWwx8UbK"
        lock_height = 10  # (anti-fee-sniping may go up to 99 blocks back)
        desc = descsum_create(f"wsh(and_v(v:pk({xprv}/0/*),after({lock_height})))")
        assert timelock.importdescriptors([{"desc": desc, "active": True, "timestamp": "now", "range": [0, 5]}])[0]["success"]
        default.sendtoaddress(timelock.getnewaddress(), 2)
        self.generate(ecash, 3, sync_fun=lambda: self.sync_all(self.nodes[:2]))
        txid = timelock.sendall([default.getnewaddress()])["txid"]
        lock = self.locktime(ecash, txid)
        assert lock != MAGIC
        assert_greater_than(lock, lock_height - 1)
        self.generate(ecash, 1, sync_fun=lambda: self.sync_all(self.nodes[:2]))
        assert_equal(default.gettransaction(txid)["confirmations"], 1)

        self.log.info("Splitting the coins: sendall to the wallet itself, which then has only protected coins")
        ecash.createwallet("split")
        split = ecash.get_wallet_rpc("split")
        default.sendtoaddress(split.getnewaddress(), 3)
        default.sendtoaddress(split.getnewaddress(), 4)
        self.generate(ecash, 1, sync_fun=lambda: self.sync_all(self.nodes[:2]))
        txid = split.sendall([split.getnewaddress()])["txid"]
        assert_equal(self.locktime(ecash, txid), MAGIC)
        self.generate(ecash, 1, sync_fun=lambda: self.sync_all(self.nodes[:2]))
        coins = split.listunspent()
        assert_equal(len(coins), 1)
        assert_equal(coins[0]["txid"], txid)


if __name__ == '__main__':
    EcashReplayTest(__file__).main()
