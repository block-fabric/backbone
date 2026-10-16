#!/usr/bin/env python3
# Copyright (c) 2026 The Chains developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""A pruned node serves sidechains fully: the index of BMM accepts.

getsidechainevents and verifybmm read which sidechain block a block committed to from the index of
BMM accepts of the drivechain database, so a pruned node answers them for blocks it no longer has,
as a node that does not prune does.

- every block connected goes to the index: after pruning, the events of the pruned blocks are the
  same as on a node that does not prune;
- a reorg: the events are those of the blocks of the active chain, also back and forth;
- a database left by a version without the index (-test=drivechain_bmm_unindexed) keeps its data and
  gets the index in the background from the blocks still on disk: a block pruned before stays
  unavailable, as before; one indexed then is served after it is pruned too.
"""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

SLOT = 1
ACTIVATION_PERIOD = 20
PRUNED_ARGS = ["-fallbackfee=0.0002", "-prune=1", "-fastprune"]
PRUNE_MARGIN = 300


class DrivechainBmmIndexTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        # Node 0 prunes; node 1 keeps every block, for comparison.
        self.extra_args = [PRUNED_ARGS, []]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def mine(self, count=1):
        return self.generate(self.nodes[0], count)

    def bmm_blocks(self, count, tag):
        """Blocks that each commit to a sidechain block: {height: sidechain block hash}."""
        node = self.nodes[0]
        committed = {}
        for i in range(count):
            side_hash = f"{tag:02x}{i:02x}" * 16
            node.createbmmrequest(SLOT, side_hash, Decimal("0.001"))
            self.mine()
            committed[node.getblockcount()] = side_hash
            assert_equal(node.getsidechainevents(SLOT, node.getblockcount())[0]["bmm"], side_hash)
        return committed

    def events(self, node, first=0):
        return node.getsidechainevents(SLOT, first, 2000)

    def assert_pruned(self, heights):
        node = self.nodes[0]
        for height in heights:
            assert_raises_rpc_error(-1, "Block not available (pruned data)", node.getblock, node.getblockhash(height))

    def assert_served(self, committed):
        """The events of these blocks, and verifybmm, as on the node that does not prune."""
        pruned, full = self.nodes
        for height, side_hash in committed.items():
            assert_equal(pruned.getsidechainevents(SLOT, height), full.getsidechainevents(SLOT, height))
            assert_equal(pruned.getsidechainevents(SLOT, height)[0]["bmm"], side_hash)
            assert pruned.verifybmm(pruned.getblockhash(height), SLOT, side_hash)["verified"]
            assert not pruned.verifybmm(pruned.getblockhash(height), SLOT, "ee" * 32)["verified"]

    def run_test(self):
        node = self.nodes[0]
        # The first block from the node that does not prune: a node in initial block download does not
        # download from a pruned one.
        self.generate(self.nodes[1], 1)
        self.mine(101)
        self.log.info("Activate a sidechain")
        node.createsidechainproposal(SLOT, "Testchain", "A sidechain for testing", "11" * 32, "22" * 20)
        self.mine(ACTIVATION_PERIOD)
        assert_equal([s["slot"] for s in node.listactivesidechains()], [SLOT])

        self.log.info("Blocks that commit to sidechain blocks, far enough below the tip to be pruned")
        # (A block file of -fastprune holds a few hundred of these blocks: pruning goes PRUNE_MARGIN above them.)
        first = self.bmm_blocks(5, 1)
        self.mine(2 * PRUNE_MARGIN)
        second = self.bmm_blocks(5, 2)
        self.mine(2 * PRUNE_MARGIN)
        assert_equal(self.events(node), self.events(self.nodes[1]))

        self.test_pruned(first)
        self.test_reorg()
        self.test_backfill(first, second)

    def test_pruned(self, first):
        self.log.info("Pruned blocks: their events come from the index")
        node = self.nodes[0]
        node.pruneblockchain(max(first) + PRUNE_MARGIN)
        self.assert_pruned(first)
        self.assert_served(first)
        assert_equal(self.events(node), self.events(self.nodes[1]))

    def test_reorg(self):
        self.log.info("A reorg: the events of the blocks of the active chain, back and forth")
        pruned, full = self.nodes
        a = self.bmm_blocks(1, 3)
        height, hash_a = next(iter(a.items()))
        block_a = pruned.getbestblockhash()
        for n in self.nodes:
            n.invalidateblock(block_a)
        # The request for A is back in the mempool; a higher one replaces it.
        pruned.createbmmrequest(SLOT, "44" * 32, Decimal("0.002"))
        block_b = self.mine()[0]
        assert_equal(pruned.getblockcount(), height)
        for n in self.nodes:
            assert_equal(n.getsidechainevents(SLOT, height)[0]["bmm"], "44" * 32)
            assert not n.verifybmm(block_b, SLOT, hash_a)["verified"]
        # Back to A.
        for n in self.nodes:
            n.invalidateblock(block_b)
            n.reconsiderblock(block_a)
            assert_equal(n.getbestblockhash(), block_a)
            assert_equal(n.getsidechainevents(SLOT, height)[0]["bmm"], hash_a)
        # And a block without a commitment in its place: no bmm.
        for n in self.nodes:
            n.invalidateblock(block_a)
        self.generateblock(pruned, pruned.getnewaddress(), [])
        for n in self.nodes:
            assert "bmm" not in n.getsidechainevents(SLOT, height)[0]
            n.reconsiderblock(block_a)
            n.reconsiderblock(block_b)
        self.mine(2)
        assert_equal(self.events(pruned, height - 5), self.events(full, height - 5))

    def test_backfill(self, first, second):
        self.log.info("A database without the index: it gets it from the blocks still on disk")
        node = self.nodes[0]
        with node.assert_debug_log(["The sidechain database gets an index of BMM accepts", "The index of BMM accepts is complete"],
                                   unexpected_msgs=["rebuilt from the blocks"], timeout=60):
            self.restart_node(0, PRUNED_ARGS + ["-test=drivechain_bmm_unindexed"])
        self.connect_nodes(0, 1)
        # Pruned before the index was filled in: unavailable, as before.
        for height in first:
            assert_raises_rpc_error(-1, f"Block {height} is not available (pruned?)", node.getsidechainevents, SLOT, height)
        # Still on disk then: in the index, and served once pruned too.
        start = node.getblockchaininfo()["pruneheight"]
        assert max(first) < start < min(second)
        assert_raises_rpc_error(-1, f"Block {start - 1} is not available (pruned?)", node.getsidechainevents, SLOT, start - 1)
        assert_equal(self.events(node, start), self.events(self.nodes[1], start))
        node.pruneblockchain(max(second) + PRUNE_MARGIN)
        self.assert_pruned(second)
        self.assert_served(second)
        assert_equal(self.events(node, start), self.events(self.nodes[1], start))

        self.log.info("After a restart, the index is complete: nothing is filled in again")
        with node.assert_debug_log([], unexpected_msgs=["The sidechain database gets an index", "The index of BMM accepts is complete"]):
            self.restart_node(0, PRUNED_ARGS)
        self.connect_nodes(0, 1)
        self.assert_served(second)
        self.mine()
        assert_equal(self.events(node, start), self.events(self.nodes[1], start))


if __name__ == '__main__':
    DrivechainBmmIndexTest(__file__).main()
