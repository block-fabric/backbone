#!/usr/bin/env python3
# Copyright (c) 2026 The Backbone developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""The activation switch of our safety rules for the bip300301_enforcer on the eCash networks.

Our pull requests to the enforcer (branches withdrawal-bundle-safety and resource-limits-canonical) make
single payout, paid-bundle memory, upvote and idle expiry, and the message limits (pending cap, one bundle
per slot per block, canonical votes, deposit destination size, one treasury output per transaction)
consensus from an activation height. Backbone has the same heights per network
(DrivechainParams::safety_activation_height, limits_activation_height), unset on every network (the rules
are policy of the block template only); -testdrivechainparam sets them on regtest.

- Node 0 (-testecash, no height) and node 1 (the same, both heights at 30) share a chain below 30: a vote
  message in the non-canonical two byte form is valid on both there.
- From 30 on node 1 refuses it (bad-dc-votes-noncanonical); node 0 takes it, as the enforcer does today.
- getdrivechaininfo says the heights, and whether the next block has single payout.

The rules themselves, on both sides of the height, are in src/test/ecash_tests.cpp (enforcer_safety_*,
enforcer_message_limits).
"""
from test_framework.blocktools import create_block, create_coinbase
from test_framework.messages import CTxOut
from test_framework.script import CScript, OP_RETURN
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal

ACTIVATION = 30
SLOT = 7
VOTE_TAG = bytes.fromhex("d77d1776")
TWO_BYTES = 2


class EcashSafetySwitchTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ["-testecash"],
            ["-testecash", f"-testdrivechainparam=safety_activation_height@{ACTIVATION}", f"-testdrivechainparam=limits_activation_height@{ACTIVATION}"],
        ]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def noncanonical_vote_block(self, node):
        """A block on node's tip whose coinbase has the vote message ABSTAIN in the two byte form."""
        tip = node.getbestblockhash()
        height = node.getblockcount() + 1
        coinbase = create_coinbase(height)
        coinbase.vout.append(CTxOut(0, CScript([OP_RETURN, VOTE_TAG + bytes([TWO_BYTES]) + (0xFFFF).to_bytes(2, "little")])))
        block = create_block(hashprev=int(tip, 16), coinbase=coinbase, ntime=node.getblockheader(tip)["time"] + 1)
        block.solve()
        return block.serialize().hex()

    def run_test(self):
        off, on = self.nodes
        off.createwallet("w")

        self.log.info("Unset (every network): no height, no single payout in consensus")
        info = off.getdrivechaininfo()
        assert_equal(info["rules"], "enforcer")
        assert "safetyactivationheight" not in info and "limitsactivationheight" not in info
        assert_equal(info["singlepayout"], False)
        info = on.getdrivechaininfo()
        assert_equal((info["safetyactivationheight"], info["limitsactivationheight"]), (ACTIVATION, ACTIVATION))
        assert_equal(info["singlepayout"], False)

        self.log.info("A sidechain, so that vote messages have an entry")
        off.createsidechainproposal(SLOT, "Testchain", "A sidechain for testing")
        self.generate(off, 8)
        assert_equal([s["slot"] for s in on.listactivesidechains()], [SLOT])

        self.log.info("Below the height: the two byte form of an abstain is valid on both")
        block = self.noncanonical_vote_block(off)
        assert_equal(off.submitblock(block), None)
        self.sync_blocks()
        assert_equal(on.getbestblockhash(), off.getbestblockhash())

        self.generate(off, ACTIVATION - 1 - off.getblockcount())
        assert_equal(on.getblockcount(), ACTIVATION - 1)
        # The next block is the activation block.
        assert_equal(on.getdrivechaininfo()["singlepayout"], True)
        assert_equal(off.getdrivechaininfo()["singlepayout"], False)

        self.log.info("From the height: node 1 refuses it, node 0 takes it")
        self.disconnect_nodes(0, 1)
        block = self.noncanonical_vote_block(off)
        assert_equal(on.submitblock(block), "bad-dc-votes-noncanonical")
        assert_equal(off.submitblock(block), None)
        assert_equal(off.getblockcount(), ACTIVATION)
        assert_equal(on.getblockcount(), ACTIVATION - 1)
        # A block of the template is valid under both.
        self.generate(on, 1, sync_fun=self.no_op)
        assert_equal(on.getblockcount(), ACTIVATION)


if __name__ == '__main__':
    EcashSafetySwitchTest(__file__).main()
