#!/usr/bin/env python3
# Copyright (c) 2026 The Backbone developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""The block that pays a withdrawal bundle (M6) also commits to the BMM request (M8) in the mempool (M7).

Checked for the report "Backbone leaves the BMM ticket out of the block that pays a bundle" (Layers'
feature_layers_ecash.py, 2026-10-21 lab tests). The template accepts every request in the mempool beside the
payout, whether the template builds the M6 itself or the sidechain node sent it to the mempool
(sendwithdrawalbundle, as Layers does). The report was the test's race: it mined as soon as the mempool held
anything, which was the M6 the sidechain node had sent, before the merged miner's ticket request arrived.
"""
from decimal import Decimal

from test_framework.address import address_to_scriptpubkey
from test_framework.messages import COIN, MAGIC_BYTES, CTransaction, CTxOut
from test_framework.p2p import P2PInterface
from test_framework.script import CScript, OP_RETURN
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal

SLOT = 7
BMM_ACCEPT_TAG = bytes.fromhex("d1617368")


class EcashBmmPayoutTest(BitcoinTestFramework):
    def add_options(self, parser):
        parser.add_argument("--chains-rules", action="store_true", help="Chains' rules (no -testecash)")

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.wallet_names = ["w"]

    def setup_network(self):
        # -testecashmagic: eCash's regtest message start, as the test's own P2P connection checks.
        self.extra_args = [["-fallbackfee=0.0002"] + ([] if self.options.chains_rules else ["-testecash", "-testecashmagic"])]
        self.setup_nodes()

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def mine(self, n=1):
        return self.generatetoaddress(self.node, n, self.address, sync_fun=self.no_op)

    def accepts(self, block_hash):
        """The BMM accepts (M7) of a block's coinbase: (slot, side block hash)."""
        coinbase = self.node.getblock(block_hash, 2)["tx"][0]
        found = []
        for out in coinbase["vout"]:
            script = bytes.fromhex(out["scriptPubKey"]["hex"])
            if len(script) >= 2 and script[0] == OP_RETURN and BMM_ACCEPT_TAG in script:
                payload = script[script.index(BMM_ACCEPT_TAG) + 4:]
                found.append((payload[0], payload[1:33][::-1].hex()))
        return found

    def run_test(self):
        node = self.node = self.nodes[0]
        if not self.options.chains_rules:
            self.log.info("-testecashmagic: a P2P connection with eCash's regtest message start (eca5b134)")
            assert_equal(MAGIC_BYTES[node.p2p_net], bytes.fromhex("eca5b134"))
            node.add_p2p_connection(P2PInterface()).sync_with_ping()
            node.disconnect_p2ps()
        self.address = node.getnewaddress()
        self.mine(110)

        self.log.info("A sidechain, a deposit")
        node.createsidechainproposal(SLOT, "Testchain", "A sidechain for testing")
        self.mine(8)
        assert_equal([s["slot"] for s in node.listactivesidechains()], [SLOT])
        node.createsidechaindeposit(SLOT, "alice", 5)
        self.mine()

        self.log.info("A bundle, voted up to one block before its payout")
        bundle = CTransaction()
        bundle.vin = []
        bundle.vout = [CTxOut(0, CScript([OP_RETURN, (COIN // 100).to_bytes(8, "big")])),
                       CTxOut(COIN, address_to_scriptpubkey(node.getnewaddress()))]
        bundle_hash = node.receivewithdrawalbundle(SLOT, bundle.serialize().hex())["hash"]
        paid = None
        for i in range(400):
            # A BMM request on the tip, before every block: each block must commit to it.
            side_hash = f"{i + 1:064x}"
            node.createbmmrequest(SLOT, side_hash, Decimal("0.001"))
            block = self.mine()[0]
            status = node.getwithdrawalbundle(SLOT, bundle_hash)["status"]
            payout = status == "paid"
            self.log.debug(f"block {node.getblockcount()}: {len(node.getblock(block)['tx'])} txs, bundle {status}, accepts {self.accepts(block)}")
            assert_equal(self.accepts(block), [(SLOT, side_hash)])
            if payout:
                paid = block
                break
        assert paid is not None, "the bundle was never paid"
        self.log.info(f"Paid by the template in {paid}, and its coinbase commits to the BMM request")
        assert_equal(node.getwithdrawalbundle(SLOT, bundle_hash)["status"], "paid")

        self.log.info("A bundle whose M6 the sidechain node sends to the mempool (sendwithdrawalbundle), and a BMM request")
        node.setdefaultwithdrawalvote("abstain")
        bundle.vout[1] = CTxOut(COIN // 2, address_to_scriptpubkey(node.getnewaddress()))
        second = node.receivewithdrawalbundle(SLOT, bundle.serialize().hex())["hash"]
        node.setwithdrawalvote(SLOT, "upvote", second)
        # The votes by blocks without the mempool (generateblock), which do not pay it: the template would pay it itself
        # in the block whose vote makes it payable (its votes count before its transactions).
        sent = None
        for i in range(400):
            result = node.sendwithdrawalbundle(SLOT, second)
            if result["sent"]:
                sent = result["txid"]
                assert sent in node.getrawmempool()
                break
            self.generateblock(node, self.address, [], sync_fun=self.no_op)
        assert sent is not None, "the second bundle was never payable"
        side_hash = f"{1000:064x}"
        node.createbmmrequest(SLOT, side_hash, Decimal("0.001"))
        block = self.mine()[0]
        assert_equal(self.accepts(block), [(SLOT, side_hash)])
        assert sent in node.getblock(block)["tx"]
        assert sent is not None, "the second bundle was never payable"
        assert_equal(node.getwithdrawalbundle(SLOT, second)["status"], "paid")


if __name__ == '__main__':
    EcashBmmPayoutTest(__file__).main()
