#!/usr/bin/env python3
# Copyright (c) 2026 The Chains developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Deposits cannot be evicted for free, and a wallet whose deposit was dropped says so.

- A deposit paid from the change of an unconfirmed BMM request is refused (dc-unconfirmed-parent).
  Before, anyone could send a BMM request and a 1-sat deposit paid from its change: every later
  deposit to the sidechain chained on it, and the next block without the request (a template that
  leaves BMM requests out, say) evicted them all, at no cost (audit 7, finding 1).
- So honest deposits made after such an attempt survive a block without the request, and are mined.
- A deposit of the wallet's that the mempool drops (here: another deposit took its escrow output in a
  block) is reported: "sidechain_deposit_dropped" in gettransaction, a warning in the log, and
  -walletnotify. createsidechaindeposit makes it again with "replaces": the new deposit spends a coin
  the dropped one spent, so that only one of the two can ever be mined.
"""
from decimal import Decimal
import os

from test_framework.address import address_to_scriptpubkey
from test_framework.messages import COIN, COutPoint, CTransaction, CTxIn, CTxOut
from test_framework.script import CScript, OP_RETURN
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

# Regtest drivechain parameters, see CRegTestParams.
ACTIVATION_PERIOD = 20
SLOT = 1


class DrivechainDepositEvictionTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True

    def setup_network(self):
        self.notify_file = os.path.join(self.options.tmpdir, "walletnotify")
        self.extra_args = [["-fallbackfee=0.0002"],
                           ["-fallbackfee=0.0002", f"-walletnotify=echo %s >> {self.notify_file}"]]
        super().setup_network()

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        attacker, honest = self.nodes
        self.generate(attacker, 101)
        attacker.createsidechainproposal(SLOT, "Eviction")
        self.generate(attacker, ACTIVATION_PERIOD)
        attacker.createsidechaindeposit(SLOT, "dest", 10)
        self.received = attacker.sendtoaddress(honest.getnewaddress(), 5)
        attacker.sendtoaddress(honest.getnewaddress(), 5)
        self.generate(attacker, 1)

        self.test_no_deposit_on_bmm_change()
        self.test_dropped_deposit_reported_and_made_again()

    def test_no_deposit_on_bmm_change(self):
        attacker, honest = self.nodes
        escrow_script = attacker.getsidechain(SLOT)["escrowscript"]
        for round_ in range(2):
            self.log.info(f"Round {round_}: a deposit paid from the change of an unconfirmed BMM request is refused")
            request = attacker.createbmmrequest(SLOT, "55" * 32, Decimal("0.001"))
            change = attacker.getrawtransaction(request["txid"], True)["vout"][1]
            ctip = attacker.getsidechain(SLOT)["escrow"]
            deposit = CTransaction()
            deposit.vin = [CTxIn(COutPoint(int(ctip["txid"], 16), ctip["n"])), CTxIn(COutPoint(int(request["txid"], 16), 1))]
            deposit.vout = [CTxOut(int(ctip["amount"] * COIN) + 1, bytes.fromhex(escrow_script)),
                            CTxOut(0, CScript([OP_RETURN, b"attacker"])),
                            CTxOut(int(change["value"] * COIN) - 1 - 20000, address_to_scriptpubkey(attacker.getnewaddress()))]
            signed = attacker.signrawtransactionwithwallet(deposit.serialize().hex())["hex"]
            assert_raises_rpc_error(-26, "dc-unconfirmed-parent", attacker.sendrawtransaction, signed)
            self.sync_mempools()
            assert_equal(honest.testmempoolaccept([signed])[0]["reject-reason"], "dc-unconfirmed-parent")

            self.log.info("  an honest deposit does not chain on the request")
            honest_dep = honest.createsidechaindeposit(SLOT, "honest", 1)
            self.sync_mempools()
            assert_equal(honest.getmempoolentry(honest_dep["txid"])["ancestorcount"], 1)
            assert request["txid"] not in honest.getmempoolancestors(honest_dep["txid"])

            self.log.info("  a block without the request (a plain template) leaves the honest deposit in the mempools")
            plain = attacker.getblocktemplate({"rules": ["segwit"]})
            assert request["txid"] not in [t["txid"] for t in plain["transactions"]]
            self.generateblock(attacker, attacker.getnewaddress(), [])
            self.sync_blocks()
            for node in self.nodes:
                assert request["txid"] not in node.getrawmempool()
                assert honest_dep["txid"] in node.getrawmempool()
            wtx = honest.gettransaction(honest_dep["txid"])
            assert_equal(wtx["confirmations"], 0)
            assert_equal(wtx["sidechain_deposit_dropped"], False)
            assert not wtx["details"][0].get("abandoned", False)

            self.log.info("  and the next block mines it")
            block = self.generate(attacker, 1)[0]
            assert honest_dep["txid"] in attacker.getblock(block)["tx"]
            assert_equal(honest.gettransaction(honest_dep["txid"])["confirmations"], 1)

    def test_dropped_deposit_reported_and_made_again(self):
        self.log.info("A deposit whose escrow output another deposit took in a block is reported as dropped")
        attacker, honest = self.nodes
        self.disconnect_nodes(0, 1)
        other = attacker.createsidechaindeposit(SLOT, "other", 2)
        dropped = honest.createsidechaindeposit(SLOT, "honest", Decimal("1.5"))
        dropped_tx = honest.getrawtransaction(dropped["txid"], True)
        assert_equal(honest.gettransaction(dropped["txid"])["sidechain_deposit_dropped"], False)
        # A deposit cannot be made again while it can still be mined.
        assert_raises_rpc_error(-8, "was not dropped", honest.createsidechaindeposit, SLOT, "honest", Decimal("1.5"), dropped["txid"])
        self.generate(attacker, 1, sync_fun=self.no_op)
        with honest.assert_debug_log([f"Warning: deposit {dropped['txid']} to the sidechain in slot {SLOT} was dropped from the mempool"]):
            self.connect_nodes(0, 1)
            self.sync_blocks()
        assert other["txid"] in attacker.getblock(attacker.getbestblockhash())["tx"]
        assert dropped["txid"] not in honest.getrawmempool()
        wtx = honest.gettransaction(dropped["txid"])
        assert_equal(wtx["sidechain_deposit_dropped"], True)
        self.wait_until(lambda: os.path.exists(self.notify_file) and dropped["txid"] in open(self.notify_file).read())

        self.log.info("Made again with \"replaces\": it spends a coin the dropped one spent")
        assert_raises_rpc_error(-5, "not in the wallet", honest.createsidechaindeposit, SLOT, "honest", 1, "00" * 32)
        assert_raises_rpc_error(-8, "not a deposit this wallet made", honest.createsidechaindeposit, SLOT, "honest", 1, self.received)
        again = honest.createsidechaindeposit(slot=SLOT, destination="honest", amount=Decimal("1.5"), replaces=dropped["txid"])
        again_tx = honest.getrawtransaction(again["txid"], True)
        # (The first input of a deposit is the escrow output it spends, the others are the wallet's.)
        dropped_wallet_inputs = {(i["txid"], i["vout"]) for i in dropped_tx["vin"][1:]}
        assert dropped_wallet_inputs & {(i["txid"], i["vout"]) for i in again_tx["vin"]}
        assert_equal(honest.gettransaction(again["txid"])["replaces_txid"], dropped["txid"])
        assert_raises_rpc_error(-8, "made again already", honest.createsidechaindeposit, SLOT, "honest", Decimal("1.5"), dropped["txid"])
        self.sync_mempools()
        block = self.generate(attacker, 1)[0]
        assert again["txid"] in attacker.getblock(block)["tx"]
        # The dropped one can never be mined now: the new one spent its coin.
        assert_equal(honest.testmempoolaccept([honest.gettransaction(dropped["txid"])["hex"]])[0]["reject-reason"], "missing-inputs")


if __name__ == '__main__':
    DrivechainDepositEvictionTest(__file__).main()
