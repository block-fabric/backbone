#!/usr/bin/env python3
# Copyright (c) 2026 The Backbone developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""A data directory of a Bitcoin node, taken over by an eCash node below the fork: the Bitcoin headers it holds
past the fork height are invalid, not the best header.

Found on the lab server (2026-10-21): a betanet node started from a snapshot of a Bitcoin mainnet datadir at
966,143 reported "headers": 969,783 -- Bitcoin's headers the snapshot's node had, loaded from the block index and
never checked under eCash's rules (the difficulty reset at the fork height) -- above betanet's real headers.

- node 0 (Bitcoin's rules) mines past the fork height;
- node 1, with Bitcoin's rules too, gets its blocks below the fork and only the headers above (as a node stopped
  below the fork that kept syncing headers), and is stopped;
- node 1 restarts with eCash's rules on that datadir: the headers from the fork height on are invalid (logged
  once), its best header is the one before the fork height, no fork warning; it then mines eCash's fork block and goes on.
"""
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal

FORK_HEIGHT = 288  # 2 * 144, as feature_ecash_fork.py
FORK_BITS = "207fffff"
BELOW = 280
TOP = 300


class EcashForkDatadirTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.bitcoin_args = ["-testpowretarget"]
        self.ecash_args = ["-testecash", f"-testecashfork={FORK_HEIGHT}@{FORK_BITS}"]
        self.extra_args = [self.bitcoin_args, self.bitcoin_args]

    def setup_network(self):
        self.setup_nodes()  # not connected

    def run_test(self):
        bitcoin, node = self.nodes
        self.log.info("Bitcoin's chain past the fork height; node 1 has its blocks below, its headers above")
        self.generatetodescriptor(bitcoin, TOP, "raw(51)", sync_fun=self.no_op)
        assert bitcoin.getblockheader(bitcoin.getblockhash(FORK_HEIGHT))["bits"] != FORK_BITS
        for height in range(1, BELOW + 1):
            node.submitblock(bitcoin.getblock(bitcoin.getblockhash(height), 0))
        for height in range(BELOW + 1, TOP + 1):
            node.submitheader(bitcoin.getblockheader(bitcoin.getblockhash(height), False))
        assert_equal((node.getblockcount(), node.getblockchaininfo()["headers"]), (BELOW, TOP))
        self.stop_node(1)

        self.log.info("Restarted with eCash's rules: the headers from the fork height on are invalid")
        with node.assert_debug_log([f"Marked {TOP - FORK_HEIGHT + 1} headers from the eCash fork height {FORK_HEIGHT} on invalid"]):
            self.start_node(1, extra_args=self.ecash_args)
        info = node.getblockchaininfo()
        # (Bitcoin's headers below the fork height are eCash's as well: the best header is the one before it.)
        assert_equal((info["blocks"], info["headers"]), (BELOW, FORK_HEIGHT - 1))
        assert_equal(info["warnings"], [])
        tips = {t["hash"]: t["status"] for t in node.getchaintips()}
        assert_equal(tips[bitcoin.getbestblockhash()], "invalid")
        assert_equal(tips[node.getbestblockhash()], "active")

        self.log.info("Marked once: a second restart finds them invalid already, and warns of nothing")
        with node.assert_debug_log([], unexpected_msgs=["headers from the eCash fork height"]):
            self.restart_node(1, extra_args=self.ecash_args)
        assert_equal(node.getblockchaininfo()["headers"], FORK_HEIGHT - 1)
        assert_equal(node.getblockchaininfo()["warnings"], [])

        self.log.info("The eCash node mines its own fork block and goes on")
        self.generatetodescriptor(node, TOP - BELOW + 5, "raw(51)", sync_fun=self.no_op)
        assert_equal(node.getblockheader(node.getblockhash(FORK_HEIGHT))["bits"], FORK_BITS)
        assert_equal(node.getblockchaininfo()["headers"], TOP + 5)


if __name__ == '__main__':
    EcashForkDatadirTest(__file__).main()
