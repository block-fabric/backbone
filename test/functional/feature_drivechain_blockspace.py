#!/usr/bin/env python3
# Copyright (c) 2026 The Chains developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""The room the miner keeps for withdrawals.

- A full mempool, whatever its fee rates, does not keep a payable withdrawal bundle out of the block.
- Without a payable bundle, transactions fill the block.
- When the block has the withdrawal from the mempool already, the room kept for it goes to
  transactions.
- A standard transaction of the largest size, and a cluster of the largest size, fit in a block.
"""
from decimal import Decimal

from test_framework.blocktools import MAX_STANDARD_TX_WEIGHT
from test_framework.mempool_util import DEFAULT_CLUSTER_SIZE_LIMIT_KVB
from test_framework.messages import COIN, CTransaction, CTxOut
from test_framework.script import CScript, OP_RETURN
from test_framework.script_util import key_to_p2wpkh_script
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_greater_than
from test_framework.wallet import MiniWallet
from test_framework.wallet_util import generate_keypair

# Regtest drivechain parameters, see CRegTestParams.
ACTIVATION_PERIOD = 20
WITHDRAWAL_MIN_SCORE = 30
# Blocks as on the other networks: the transactions (all but the coinbase) weigh less than 400,000.
# Regtest allows more; -blockmaxweight (which counts the 8,000 kept for the coinbase) keeps the miner to it.
MAX_BLOCK_TX_WEIGHT = 400_000
BLOCK_RESERVED_WEIGHT = 8_000
SLOT = 1
# Each filler transaction weighs 10,000: a block that is full has less than this left.
FILLER_VSIZE = 2_500


class DrivechainBlockspaceTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-fallbackfee=0.0002", f"-blockmaxweight={MAX_BLOCK_TX_WEIGHT + BLOCK_RESERVED_WEIGHT}"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]
        self.wallet = MiniWallet(node)
        self.generate(self.wallet, 200)
        self.generate(node, 101)
        node.createsidechainproposal(SLOT, "Testchain")
        self.generate(node, ACTIVATION_PERIOD)
        assert_equal([s["slot"] for s in node.listactivesidechains()], [SLOT])
        node.createsidechaindeposit(SLOT, "alice", 20)
        self.generate(node, 1)

        self.test_payable_bundle_full_mempool()
        self.test_withdrawal_from_mempool()
        self.test_no_bundle_full_mempool()
        self.test_largest_standard_tx()

    def make_bundle(self, payouts, fee):
        """A blind bundle of `payouts` outputs of 0.001 each."""
        bundle = CTransaction()
        bundle.vin = []
        bundle.vout = [CTxOut(0, CScript([OP_RETURN, fee.to_bytes(8, "big")]))]
        for _ in range(payouts):
            _, pubkey = generate_keypair()
            bundle.vout.append(CTxOut(COIN // 1000, key_to_p2wpkh_script(pubkey)))
        return bundle

    def vote_through(self, bundle):
        node = self.nodes[0]
        bundle_hash = node.receivewithdrawalbundle(SLOT, bundle.serialize().hex())["hash"]
        self.generate(node, WITHDRAWAL_MIN_SCORE)
        assert_equal(node.getwithdrawalbundle(SLOT, bundle_hash)["payable"], True)
        return bundle_hash

    def fill_mempool(self, weight):
        """Transactions of more than `weight`, at a fee rate well above that of any bundle here."""
        node = self.nodes[0]
        for _ in range(weight // (4 * FILLER_VSIZE) + 1):
            self.wallet.send_self_transfer(from_node=node, target_vsize=FILLER_VSIZE, fee_rate=Decimal("0.001"))
        assert_greater_than(node.getmempoolinfo()["bytes"] * 4, weight)

    def mine_full_block(self):
        """Mine a block; return it, and the weight of its transactions but the coinbase."""
        node = self.nodes[0]
        block = node.getblock(self.generate(node, 1)[0], 2)
        tx_weight = sum(tx["weight"] for tx in block["tx"][1:])
        assert tx_weight < MAX_BLOCK_TX_WEIGHT, (tx_weight, [tx["weight"] for tx in block["tx"]])
        # The block is full: the mempool has transactions left, and none of them would fit.
        assert_greater_than(node.getmempoolinfo()["size"], 0)
        assert_greater_than(tx_weight, MAX_BLOCK_TX_WEIGHT - 4 * FILLER_VSIZE)
        return block, tx_weight

    def test_payable_bundle_full_mempool(self):
        self.log.info("A full mempool does not keep a payable bundle out of the block")
        node = self.nodes[0]
        # About 125,000 weight, at a fee rate far below that of the mempool.
        bundle_hash = self.vote_through(self.make_bundle(1000, 1000))
        self.fill_mempool(2 * MAX_BLOCK_TX_WEIGHT)
        block, _ = self.mine_full_block()
        withdrawal = block["tx"][-1]
        assert_greater_than(withdrawal["weight"], 120_000)
        assert_equal(node.getsidechain(SLOT)["escrow"]["txid"], withdrawal["txid"])
        assert_equal(node.getwithdrawalbundle(SLOT, bundle_hash), {"status": "paid"})
        # Clear the mempool.
        while node.getmempoolinfo()["size"]:
            self.generate(node, 1)

    def test_withdrawal_from_mempool(self):
        self.log.info("The room kept for a withdrawal the block has from the mempool goes to transactions")
        node = self.nodes[0]
        bundle_hash = self.vote_through(self.make_bundle(1000, COIN // 10))
        sent = node.sendwithdrawalbundle(SLOT, bundle_hash)
        assert_equal(sent["sent"], True)
        self.fill_mempool(2 * MAX_BLOCK_TX_WEIGHT)
        block, _ = self.mine_full_block()
        # The withdrawal from the mempool, which pays the higher fee rate, and no other.
        txids = [tx["txid"] for tx in block["tx"]]
        assert sent["txid"] in txids
        assert_equal(txids.index(sent["txid"]), 1)
        assert_equal(len([tx for tx in block["tx"] if tx["weight"] > 100_000]), 1)
        assert_equal(node.getwithdrawalbundle(SLOT, bundle_hash), {"status": "paid"})
        while node.getmempoolinfo()["size"]:
            self.generate(node, 1)

    def test_no_bundle_full_mempool(self):
        self.log.info("Without a payable bundle, transactions fill the block")
        node = self.nodes[0]
        assert_equal(node.listwithdrawalbundles(), [])
        escrow = node.getsidechain(SLOT)["escrow"]
        self.fill_mempool(2 * MAX_BLOCK_TX_WEIGHT)
        block, _ = self.mine_full_block()
        assert all(tx["weight"] <= 4 * FILLER_VSIZE for tx in block["tx"][1:])
        assert_equal(node.getsidechain(SLOT)["escrow"], escrow)

    def test_largest_standard_tx(self):
        self.log.info("The largest standard transaction, with a child that makes the largest cluster, is mined")
        node = self.nodes[0]
        while node.getmempoolinfo()["size"]:
            self.generate(node, 1)
        assert_equal(MAX_STANDARD_TX_WEIGHT, MAX_BLOCK_TX_WEIGHT - 8_000)
        too_large = self.wallet.create_self_transfer(target_vsize=MAX_STANDARD_TX_WEIGHT // 4 + 1)
        assert_equal(node.testmempoolaccept([too_large["hex"]])[0]["reject-reason"], "tx-size")
        largest = self.wallet.send_self_transfer(from_node=node, target_vsize=MAX_STANDARD_TX_WEIGHT // 4)
        assert_greater_than(largest["tx"].get_weight(), MAX_STANDARD_TX_WEIGHT - 4)
        # A child at a higher fee rate: the two make one chunk, of the largest cluster size.
        child_vsize = DEFAULT_CLUSTER_SIZE_LIMIT_KVB * 1000 - MAX_STANDARD_TX_WEIGHT // 4
        assert_equal(node.testmempoolaccept([self.wallet.create_self_transfer(utxo_to_spend=largest["new_utxo"], target_vsize=child_vsize + 1, fee_rate=Decimal("0.01"))["hex"]])[0]["reject-reason"], "too-large-cluster")
        child = self.wallet.send_self_transfer(from_node=node, utxo_to_spend=largest["new_utxo"], target_vsize=child_vsize, fee_rate=Decimal("0.01"))
        block = node.getblock(self.generate(node, 1)[0])
        assert_equal(block["tx"][1:], [largest["txid"], child["txid"]])
        assert_equal(node.getmempoolinfo()["size"], 0)


if __name__ == '__main__':
    DrivechainBlockspaceTest(__file__).main()
