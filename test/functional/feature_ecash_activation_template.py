#!/usr/bin/env python3
# Copyright (c) 2026 The Backbone developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""The block template at the block that activates a sidechain, on an eCash regtest (-testecash).

Before a sidechain activates, an output with its treasury script is an ordinary output, and the mempool takes it. The
coinbase of the activating block makes it active for the block's own transactions, as consensus applies the coinbase
first. The template leaves out what touches the treasury script of a sidechain its coinbase activates, and the block
is built:

- a transaction paying the treasury script with no destination after it (an invalid deposit once it is active) waits,
  and once the sidechain is active the mempool drops it;
- two deposits made before the activation (which the block would have to consolidate) wait for the next block, which
  takes them both and consolidates them;
- getblocktemplate works at the activation block too.
"""
from decimal import Decimal

from test_framework.messages import COIN, CTransaction, CTxOut
from test_framework.script import CScript, OP_RETURN
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal

OP_NOP8 = 0xb7
# The enforcer's thresholds on regtest (SHORT): a proposal for an unused slot activates with more acks than this.
UNUSED_SLOT_THRESHOLD = 5


def escrow_script(slot):
    return CScript(bytes([OP_NOP8, 1, slot, 0x51]))


class EcashActivationTemplateTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-testecash", "-fallbackfee=0.0002"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def send(self, outputs):
        node = self.nodes[0]
        tx = CTransaction()
        tx.vout = outputs
        funded = node.fundrawtransaction(tx.serialize().hex(), {"changePosition": len(outputs)})["hex"]
        return node.sendrawtransaction(node.signrawtransactionwithwallet(funded)["hex"])

    def stray(self, slot, value=1000):
        """An output with the treasury script of `slot`, and nothing after it."""
        return self.send([CTxOut(value, escrow_script(slot))])

    def deposit(self, slot, amount):
        return self.send([CTxOut(int(amount * COIN), escrow_script(slot)), CTxOut(0, CScript([OP_RETURN, b"dest"]))])

    def acks(self, slot):
        pending = [p for p in self.nodes[0].listsidechainproposals()["pending"] if p["slot"] == slot]
        if not pending:
            return None
        # The blocks left: the acks still needed for more than the threshold.
        assert_equal(pending[0]["blocksleft"], UNUSED_SLOT_THRESHOLD + 1 - pending[0]["acks"])
        return pending[0]["acks"]

    def mine_to_activation(self, slot, before):
        """Mine until the next block activates `slot`, calling `before()` (which returns the txids that must be in the
        mempool then) ahead of each block; return those txids."""
        node = self.nodes[0]
        node.createsidechainproposal(slot, f"Sidechain {slot}", "")
        self.generate(node, 1)
        while True:
            txids = before()
            # More acks than the threshold activate: the next block's is the one.
            if self.acks(slot) == UNUSED_SLOT_THRESHOLD:
                return txids
            self.generate(node, 1)
            assert slot not in [s["slot"] for s in node.listactivesidechains()]

    def run_test(self):
        node = self.nodes[0]
        self.generate(node, 110)
        self.test_stray_output()
        self.test_deposits_before_activation()

    def test_stray_output(self):
        self.log.info("A treasury output with no destination, in the mempool at the activation block")
        node = self.nodes[0]
        slot = 7
        state = {"txid": None}

        def before():
            # Until the sidechain is active the output is an ordinary one, which blocks take.
            if state["txid"] is None or state["txid"] not in node.getrawmempool():
                state["txid"] = self.stray(slot)
            return state["txid"]
        txid = self.mine_to_activation(slot, before)
        assert txid in node.getrawmempool()
        template = node.getblocktemplate({"rules": ["segwit", "bip300301"]})
        assert txid not in [tx["txid"] for tx in template["transactions"]]
        block = node.getblock(self.generate(node, 1)[0])
        assert slot in [s["slot"] for s in node.listactivesidechains()]
        assert txid not in block["tx"]
        # Now an invalid deposit: the mempool drops it.
        assert txid not in node.getrawmempool()
        self.generate(node, 1)

    def test_deposits_before_activation(self):
        self.log.info("Two deposits in the mempool at the activation block wait for the next one")
        node = self.nodes[0]
        slot = 8
        state = {"txids": []}

        def before():
            mempool = node.getrawmempool()
            if not state["txids"] or any(txid not in mempool for txid in state["txids"]):
                state["txids"] = [self.deposit(slot, Decimal("1")), self.deposit(slot, Decimal("2"))]
            return state["txids"]
        txids = self.mine_to_activation(slot, before)
        assert all(txid in node.getrawmempool() for txid in txids)
        block = node.getblock(self.generate(node, 1)[0])
        assert slot in [s["slot"] for s in node.listactivesidechains()]
        assert not any(txid in block["tx"] for txid in txids)
        assert "escrow" not in node.getsidechain(slot)
        # Valid deposits now: they stay, and the next block takes them, with their consolidation.
        assert all(txid in node.getrawmempool() for txid in txids)
        block = node.getblock(self.generate(node, 1)[0])
        assert all(txid in block["tx"] for txid in txids)
        assert_equal(node.getsidechain(slot)["escrow"]["amount"], Decimal("3"))
        assert_equal(sorted(d["amount"] for d in node.listsidechaindeposits(slot)), [Decimal("1"), Decimal("2")])


if __name__ == '__main__':
    EcashActivationTemplateTest(__file__).main()
