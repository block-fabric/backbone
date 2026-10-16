#!/usr/bin/env python3
# Copyright (c) 2026 The Chains developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""A pruned node keeps a good history: -prune below 10000 MiB refuses to start on the main network,
testnet and signet (CChainParams::MinPruneTarget).

- -prune=2, 550, 5000 or 9999: the node does not start, and says why;
- -prune=10000 and 12000: the node starts, pruning to that target;
- -prune=1 (manual pruning) and -prune=0 (off) are taken;
- regtest keeps Bitcoin's minimum (550 MiB), for the tests inherited from Bitcoin.
"""

from test_framework import util
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_node import ErrorMatch
from test_framework.util import assert_equal

MIB = 1024 * 1024


def refusal(minimum):
    return f"Error: Prune configured below the minimum of {minimum} MiB.  Please use a higher number."


class PruneMinimumTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.wallet_names = []

    # The node is started by run_test, on each network in turn.
    def setup_network(self):
        self.add_nodes(self.num_nodes)

    def run_test(self):
        node = self.nodes[0]
        conf_file = node.datadir_path / "chains.conf"
        # No peers on the real networks (none are known anyway) and no wallet.
        quiet = ["-maxconnections=0", "-disablewallet"]
        for chain_name, chain in (("", "main"), ("testnet", "test"), ("signet", "signet")):
            self.log.info(f"{chain}: -prune below 10000 MiB refuses to start")
            util.write_config(conf_file, n=0, chain=chain_name)
            node.chain = chain_name
            for value in (2, 550, 5000, 9999):
                node.assert_start_raises_init_error(quiet + [f"-prune={value}"], refusal(10000), match=ErrorMatch.FULL_TEXT)

            self.log.info(f"{chain}: -prune of 10000 MiB and more is taken")
            for value in (10000, 12000):
                self.start_node(0, quiet + [f"-prune={value}"])
                info = node.getblockchaininfo()
                assert_equal(info["chain"], chain)
                assert info["pruned"]
                assert info["automatic_pruning"]
                assert_equal(info["prune_target_size"], value * MIB)
                self.stop_node(0)

            self.log.info(f"{chain}: -prune=1 (manual pruning) and -prune=0 (off) are taken")
            self.start_node(0, quiet + ["-prune=1"])
            info = node.getblockchaininfo()
            assert info["pruned"]
            assert not info["automatic_pruning"]
            self.stop_node(0)
            self.start_node(0, quiet + ["-prune=0"])
            assert not node.getblockchaininfo()["pruned"]
            self.stop_node(0)

        self.log.info("regtest keeps Bitcoin's minimum of 550 MiB")
        util.write_config(conf_file, n=0, chain="regtest")
        node.chain = "regtest"
        node.assert_start_raises_init_error(quiet + ["-prune=549"], refusal(550), match=ErrorMatch.FULL_TEXT)
        self.start_node(0, quiet + ["-prune=550"])
        assert_equal(node.getblockchaininfo()["prune_target_size"], 550 * MIB)
        self.stop_node(0)


if __name__ == '__main__':
    PruneMinimumTest(__file__).main()
