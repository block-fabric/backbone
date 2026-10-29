#!/usr/bin/env python3
# Copyright (c) 2026 The Backbone developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""The drivechain messages of the coinbase count in the weight of the block template, on an eCash regtest
(-testecash): with many acks (M2) and BMM accepts (M7) and a full mempool, the block stays within -blockmaxweight
(of which -blockreservedweight is only for the header and the rest of the coinbase).
"""
from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_greater_than, assert_greater_than_or_equal
from test_framework.wallet import MiniWallet

MAX_WEIGHT = 200_000
RESERVED_WEIGHT = 2_000
SIDECHAINS = 12
PENDING_PROPOSALS = 3
BIG_VSIZE = 2_500
SMALL_VSIZE = 150


class EcashCoinbaseWeightTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-testecash", "-fallbackfee=0.0002", f"-blockmaxweight={MAX_WEIGHT}", f"-blockreservedweight={RESERVED_WEIGHT}"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]
        wallet = MiniWallet(node)
        self.generate(wallet, 150)
        self.generate(node, 110)

        self.log.info(f"{SIDECHAINS} sidechains, and {PENDING_PROPOSALS} proposals collecting acks")
        for slot in range(SIDECHAINS):
            node.createsidechainproposal(slot, f"Sidechain {slot}")
        self.generate(node, SIDECHAINS + 8)
        assert_equal(len(node.listactivesidechains()), SIDECHAINS)
        for slot in range(SIDECHAINS, SIDECHAINS + PENDING_PROPOSALS):
            node.createsidechainproposal(slot, f"Sidechain {slot}")
        self.generate(node, PENDING_PROPOSALS)
        assert_equal(len(node.listsidechainproposals()["pending"]), PENDING_PROPOSALS)

        self.log.info("A BMM request for each sidechain, and a full mempool")
        for slot in range(SIDECHAINS):
            node.createbmmrequest(slot, f"{slot + 1:064x}", Decimal("0.001"))
        # Large transactions, then small ones at a lower fee rate that fill what they leave of the block.
        for _ in range(MAX_WEIGHT // (4 * BIG_VSIZE) + 2):
            wallet.send_self_transfer(from_node=node, target_vsize=BIG_VSIZE, fee_rate=Decimal("0.001"))
        for _ in range(4 * BIG_VSIZE // SMALL_VSIZE + 10):
            wallet.send_self_transfer(from_node=node, target_vsize=SMALL_VSIZE, fee_rate=Decimal("0.0005"))
        assert_greater_than(node.getmempoolinfo()["bytes"] * 4, MAX_WEIGHT)

        # The template is built (its validity check counts the coinbase too).
        node.getblocktemplate({"rules": ["segwit", "bip300301"]})

        block = node.getblock(self.generate(node, 1)[0], 2)
        coinbase = block["tx"][0]
        messages = [out for out in coinbase["vout"] if out["scriptPubKey"]["hex"].startswith("6a")]
        # The acks, the accepts, and the witness commitment.
        assert_greater_than_or_equal(len(messages), SIDECHAINS + PENDING_PROPOSALS + 1)
        assert_greater_than(node.getmempoolinfo()["size"], 0)
        self.log.info(f"Block weight {block['weight']}, of which the coinbase {coinbase['weight']}")
        assert block["weight"] <= MAX_WEIGHT, block["weight"]
        # Full: what is left of the block is less than a small transaction and the coinbase.
        assert_greater_than(block["weight"], MAX_WEIGHT - RESERVED_WEIGHT - 4 * SMALL_VSIZE - coinbase["weight"])


if __name__ == '__main__':
    EcashCoinbaseWeightTest(__file__).main()
