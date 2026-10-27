#!/usr/bin/env python3
# Copyright (c) 2026 The Chains developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test Backbone's default drivechain votes, and the overrides that opt out of them.

By default a node acks every sidechain proposal (M2) and, on each sidechain, upvotes (M4) the
bundle a sidechain node handed to it (verified) or, where no sidechain node spoke, the leading
bundle, unchecked. A bundle that mismatches what the sidechain node handed is not upvoted.

On eCash's main network (-chain=ecash) the defaults are stricter: upvote only what a sidechain
node handed (handed), ack only proposals for a free slot (newslots). The last part restarts
node 1 on an empty datadir of that network, offline, to check them and that the options go over them.

Node 0 runs the "sidechain node" (it is handed the bundles). Node 1 is a miner with the
defaults and no sidechain node. Node 2 starts with per-slot overrides in its configuration.
"""
from decimal import Decimal

from test_framework.messages import COIN, CTransaction, CTxOut
from test_framework.script import CScript, OP_RETURN
from test_framework.address import address_to_scriptpubkey
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, p2p_port, rpc_port

# Regtest drivechain parameters, see CRegTestParams.
ACTIVATION_PERIOD = 20
WITHDRAWAL_MIN_SCORE = 30

SLOT = 1
NACKED_SLOT = 3


def blind_bundle(address, amount):
    bundle = CTransaction()
    bundle.vin = []
    bundle.vout = [
        CTxOut(0, CScript([OP_RETURN, (1000).to_bytes(8, "big")])),
        CTxOut(amount, address_to_scriptpubkey(address)),
    ]
    return bundle.serialize().hex()


class DrivechainDefaultVoteTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.extra_args = [["-fallbackfee=0.0002"], [], [f"-withdrawalvote={SLOT}:downvote", f"-sidechainack={NACKED_SLOT}:nack"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def slot_policy(self, node, slot):
        return next(s for s in node.getvotepolicy()["sidechains"] if s["slot"] == slot)

    def run_test(self):
        node, pool, opted = self.nodes
        self.generate(node, 101)

        self.log.info("The defaults: upvote, ack every proposal")
        for n in (node, pool):
            info = n.getdrivechaininfo()
            assert_equal(info["defaultwithdrawalvote"], "upvote")
            assert_equal(info["sidechainackpolicy"], "all")
        assert_raises_rpc_error(-8, "vote must be upvote, handed, follow, downvote or abstain", pool.setdefaultwithdrawalvote, "yes")
        assert_raises_rpc_error(-8, "policy must be all, newslots or none", pool.setsidechainackpolicy, "some")
        assert_raises_rpc_error(-8, "with a slot, policy must be ack, nack or default", pool.setsidechainackpolicy, "all", 2)

        self.log.info("A proposal of another node is acked to activation by default")
        proposal = node.createsidechainproposal(SLOT, "Testchain", "", "11" * 32, "22" * 20)
        self.generate(node, 1)
        pending = pool.listsidechainproposals()["pending"][0]
        assert_equal(pending["proposalhash"], proposal["proposalhash"])
        assert_equal(pending["ack"], True)
        assert_equal(pool.getvotepolicy()["proposals"], [{"slot": SLOT, "proposalhash": proposal["proposalhash"], "ack": True, "reason": "policy"}])
        self.generate(pool, ACTIVATION_PERIOD - 1)
        assert_equal([s["slot"] for s in pool.listactivesidechains()], [SLOT])

        self.log.info("A slot set to NACK is not acked")
        nacked = node.createsidechainproposal(NACKED_SLOT, "Nacked", "", "33" * 32, "44" * 20)
        self.generate(node, 1)
        assert_equal(opted.getvotepolicy()["proposals"], [{"slot": NACKED_SLOT, "proposalhash": nacked["proposalhash"], "ack": False, "reason": "slot"}])
        # (The block of a proposal does not ack it.)
        self.generate(opted, 2)
        assert_equal(opted.listsidechainproposals()["pending"][0]["acks"], 0)
        # A NACK of one proposal (acksidechain false) on the default node.
        assert_equal(pool.acksidechain(nacked["proposalhash"], False), [NACKED_SLOT])
        assert_equal(pool.getvotepolicy()["proposals"][0]["reason"], "nack")
        self.generate(pool, 1)
        assert_equal(pool.listsidechainproposals()["pending"][0]["acks"], 0)
        assert_equal(pool.acksidechain(nacked["proposalhash"], True), [NACKED_SLOT])
        assert_equal(pool.getvotepolicy()["proposals"][0], {"slot": NACKED_SLOT, "proposalhash": nacked["proposalhash"], "ack": True, "reason": "ack"})
        pool.acksidechain(nacked["proposalhash"], False)

        self.log.info("Fund the escrow, and propose a bundle that only node 0 was handed")
        node.createsidechaindeposit(SLOT, "alice", 5)
        self.generate(node, 1)
        payout_address = pool.getnewaddress()
        payout = COIN // 2
        bundle = node.receivewithdrawalbundle(SLOT, blind_bundle(payout_address, payout))["hash"]
        self.generate(node, 1)
        assert_equal([b["hash"] for b in pool.listwithdrawalbundles(SLOT)], [bundle])

        verified = self.slot_policy(node, SLOT)
        assert_equal((verified["vote"], verified["bundle"], verified["reason"]), ("upvote", bundle, "verified"))
        leading = self.slot_policy(pool, SLOT)
        assert_equal((leading["vote"], leading["bundle"], leading["reason"]), ("upvote", bundle, "leading"))
        assert "fraudulent" in leading["why"]
        assert_equal(pool.listwithdrawalbundles(SLOT)[0]["vote"], "upvote")
        downvoted = self.slot_policy(opted, SLOT)
        assert_equal((downvoted["vote"], downvoted["override"], downvoted["reason"]), ("downvote", "downvote", "override"))

        self.log.info("An override to downvote is cast in the blocks")
        score = opted.listwithdrawalbundles(SLOT)[0]["score"]
        self.generate(opted, 1)
        assert_equal(opted.listwithdrawalbundles(SLOT)[0]["score"], score - 1)

        self.log.info("An override to abstain")
        opted.setwithdrawalvote(SLOT, "abstain")
        assert_equal(self.slot_policy(opted, SLOT)["vote"], "abstain")
        score = opted.listwithdrawalbundles(SLOT)[0]["score"]
        self.generate(opted, 1)
        assert_equal(opted.listwithdrawalbundles(SLOT)[0]["score"], score)

        self.log.info("A bundle that mismatches the one a sidechain node handed is not upvoted")
        opted.setwithdrawalvote(SLOT, "default")
        other = opted.receivewithdrawalbundle(SLOT, blind_bundle(payout_address, payout + 1))["hash"]
        assert other != bundle
        flagged = self.slot_policy(opted, SLOT)
        assert_equal((flagged["vote"], flagged["reason"]), ("abstain", "flagged"))
        assert "bundle" not in flagged
        # The sidechain node says its chain has no bundle: the pending one is downvoted.
        opted.vouchwithdrawalbundle(SLOT)
        flagged = self.slot_policy(opted, SLOT)
        assert_equal((flagged["vote"], flagged["reason"]), ("downvote", "flagged"))

        self.log.info("Opting out of the leading bundle: handed only")
        pool.setdefaultwithdrawalvote("handed")
        assert_equal(self.slot_policy(pool, SLOT)["vote"], "abstain")
        pool.setdefaultwithdrawalvote("upvote")
        # An override to upvote, without a hash, is of the leading bundle.
        opted.setwithdrawalvote(SLOT, "upvote")
        forced = self.slot_policy(opted, SLOT)
        assert_equal((forced["vote"], forced["bundle"], forced["override"]), ("upvote", bundle, "leading"))
        opted.setwithdrawalvote(SLOT, "default")

        self.log.info("The default upvotes of a miner without a sidechain node pay the bundle out")
        while pool.listwithdrawalbundles(SLOT)[0]["score"] < WITHDRAWAL_MIN_SCORE:
            self.generate(pool, 1)
        assert_equal(pool.listwithdrawalbundles(SLOT)[0]["payable"], True)
        # The node that has the bundle's transaction pays it out.
        self.generate(node, 1)
        for n in self.nodes:
            assert_equal(n.getwithdrawalbundle(SLOT, bundle), {"status": "paid"})
        assert_equal(pool.getreceivedbyaddress(payout_address, 1), Decimal(payout) / COIN)

        self.test_mainnet_defaults()

    def test_mainnet_defaults(self):
        self.log.info("eCash's main network: upvote only handed bundles, ack only new slots")
        self.stop_node(1)
        pool = self.nodes[1]
        regtest_conf = pool.bitcoinconf.read_text()
        # The main network's data goes in the root of the datadir, beside regtest/. Offline, and
        # no gRPC (whose default port on that network is the enforcer's, 50051).
        pool.bitcoinconf.write_text(
            "chain=ecash\n[ecash]\n"
            f"port={p2p_port(1)}\nrpcport={rpc_port(1)}\n"
            # (Pruned: a full main network's chain would not fit, which the node warns of.)
            "server=1\nlisten=0\nconnect=0\ndnsseed=0\nfixedseeds=0\ngrpc=0\nprune=10000\n"
            "rpcservertimeout=99000\nrpcthreads=2\nprinttoconsole=0\n"
        )
        pool.chain = ""
        try:
            for extra, expected in (
                ([], ("handed", "newslots")),
                (["-defaultwithdrawalvote=upvote", "-sidechainackpolicy=all"], ("upvote", "all")),
                # The options are kept: a restart without them keeps the choice.
                ([], ("upvote", "all")),
            ):
                self.start_node(1, extra_args=extra)
                info = pool.getdrivechaininfo()
                assert_equal(pool.getblockchaininfo()["chain"], "main")
                assert_equal((info["defaultwithdrawalvote"], info["sidechainackpolicy"]), expected)
                self.stop_node(1)
        finally:
            pool.chain = self.chain
            pool.bitcoinconf.write_text(regtest_conf)
        self.start_node(1)
        info = pool.getdrivechaininfo()
        assert_equal((info["defaultwithdrawalvote"], info["sidechainackpolicy"]), ("upvote", "all"))


if __name__ == '__main__':
    DrivechainDefaultVoteTest(__file__).main()
