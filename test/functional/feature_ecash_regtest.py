#!/usr/bin/env python3
# Copyright (c) 2026 The Backbone developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""-ecashregtest: regtest as eCash's own node runs it (ecash-com/bitcoin -regtest), so that both peer on one chain.

- Its nodes peer with each other (message start eca5b134) and not with a -testecash node (Backbone's own
  regtest message start).
- bech32 addresses are bcrt1 (eCash's regtest), not rchn1.
- -ecashregtest=betanet: the replay nLockTime is 499999999 (final with non-final inputs; 499999998 is not) and
  the wallet stamps it; -ecashregtest=ecash: 499999998.
- A bad value is refused at startup.

The live check against ecash-com/bitcoin itself is ~/skydoge/tools/linked-regtest (linked-regtest.sh).
"""
import time

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, p2p_port
from test_framework.wallet import MiniWallet

BETANET_LOCKTIME = 499999999
MAIN_LOCKTIME = 499999998


class EcashRegtestTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        self.setup_clean_chain = True
        self.extra_args = [
            ["-ecashregtest=betanet", "-fallbackfee=0.0002"],
            ["-ecashregtest=betanet", "-fallbackfee=0.0002"],
            ["-testecash", "-fallbackfee=0.0002"],
            ["-ecashregtest=ecash", "-fallbackfee=0.0002"],
        ]

    def setup_network(self):
        self.setup_nodes()
        self.connect_nodes(0, 1)

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node, peer, own, main = self.nodes

        self.log.info("Two -ecashregtest nodes follow one chain")
        self.generate(node, 110, sync_fun=lambda: self.sync_all(self.nodes[:2]))
        assert_equal(peer.getbestblockhash(), node.getbestblockhash())

        self.log.info("A -testecash node (Backbone's regtest message start) does not peer with them")
        own.addnode(f"127.0.0.1:{p2p_port(0)}", "onetry")
        time.sleep(3)
        assert_equal(own.getconnectioncount(), 0)
        assert_equal(len([p for p in node.getpeerinfo() if p["inbound"]]), 0)

        self.log.info("Addresses are eCash's regtest ones (bcrt1)")
        assert node.getnewaddress().startswith("bcrt1")
        assert own.getnewaddress().startswith("rchn1")
        assert main.getnewaddress().startswith("bcrt1")

        self.log.info("-ecashregtest=betanet: the wallet stamps 499999999")
        txid = node.sendtoaddress(peer.getnewaddress(), 1)
        assert_equal(node.getrawtransaction(txid, True)["locktime"], BETANET_LOCKTIME)
        self.generate(node, 1, sync_fun=lambda: self.sync_all(self.nodes[:2]))

        self.log.info("499999999 is final with non-final inputs, 499999998 is not (betanet)")
        wallet = MiniWallet(node)
        self.generate(wallet, 101, sync_fun=lambda: self.sync_all(self.nodes[:2]))
        tx = wallet.create_self_transfer(locktime=BETANET_LOCKTIME, sequence=0xfffffffe)
        node.sendrawtransaction(tx["hex"])
        tx = wallet.create_self_transfer(locktime=MAIN_LOCKTIME, sequence=0xfffffffe)
        assert_raises_rpc_error(-26, "non-final", node.sendrawtransaction, tx["hex"])
        self.generate(node, 1, sync_fun=lambda: self.sync_all(self.nodes[:2]))

        self.log.info("-ecashregtest=ecash: 499999998, as eCash's main network release")
        self.generate(main, 101, sync_fun=self.no_op)
        txid = main.sendtoaddress(main.getnewaddress(), 1)
        assert_equal(main.getrawtransaction(txid, True)["locktime"], MAIN_LOCKTIME)

        self.log.info("A bad value is refused")
        self.stop_node(3)
        self.nodes[3].assert_start_raises_init_error(["-ecashregtest=drynet"], "Error: Invalid value (drynet) for -ecashregtest (betanet or ecash).")


if __name__ == "__main__":
    EcashRegtestTest(__file__).main()
