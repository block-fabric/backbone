#!/usr/bin/env python3
# Copyright (c) 2026 The Chains developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Deposits cannot be evicted for free, and a wallet whose deposit was dropped says so.

- A deposit paid from the change of an unconfirmed BMM request is refused (dc-unconfirmed-parent),
  and so is a BMM request that is also a deposit (dc-bmm-request-deposit), whatever it spends: a
  block without the request would evict the deposits chained on its escrow output. Only a BMM
  request proper (M8 in output 0, no other M8 script, no escrow output) may spend the outputs of
  an unconfirmed one.
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
OTHER_SLOT = 2


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
        attacker.createsidechainproposal(OTHER_SLOT, "Other")
        self.generate(attacker, ACTIVATION_PERIOD)
        attacker.createsidechaindeposit(SLOT, "dest", 10)
        self.received = attacker.sendtoaddress(honest.getnewaddress(), 5)
        attacker.sendtoaddress(honest.getnewaddress(), 5)
        self.generate(attacker, 1)

        self.test_no_deposit_on_bmm_change()
        self.test_no_request_that_is_a_deposit()
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

    def m8_script(self, slot, side_hash):
        """A BMM request (M8) script for the block after the tip."""
        prev = self.nodes[0].getbestblockhash()
        return CScript([OP_RETURN, bytes.fromhex("00bf00" + "%02x" % slot + bytes.fromhex(side_hash)[::-1].hex() + bytes.fromhex(prev)[::-1].hex())])

    def test_no_request_that_is_a_deposit(self):
        self.log.info("A BMM request that is also a deposit is refused, whatever it spends (dc-bmm-request-deposit)")
        attacker, honest = self.nodes
        escrow_script = bytes.fromhex(attacker.getsidechain(SLOT)["escrowscript"])
        request = attacker.createbmmrequest(SLOT, "66" * 32, Decimal("0.001"))
        change = attacker.getrawtransaction(request["txid"], True)["vout"][1]
        ctip = attacker.getsidechain(SLOT)["escrow"]
        coin = [u for u in attacker.listunspent(1) if u["amount"] >= 1][0]

        def send(hex_tx):
            # (The bid of a request is the value of its OP_RETURN output.)
            return attacker.sendrawtransaction(hex_tx, 0.1, 1)

        def combined(paid_by, value, m8_at_end=False):
            tx = CTransaction()
            tx.vin = [CTxIn(COutPoint(int(ctip["txid"], 16), ctip["n"])), CTxIn(COutPoint(int(paid_by[0], 16), paid_by[1]))]
            request_out = CTxOut(10000, self.m8_script(OTHER_SLOT, "77" * 32))
            deposit_outs = [CTxOut(int(ctip["amount"] * COIN) + 1, escrow_script),
                            CTxOut(0, CScript([OP_RETURN, b"attacker"])),
                            CTxOut(int(value * COIN) - 1 - 10000 - 20000, address_to_scriptpubkey(attacker.getnewaddress()))]
            tx.vout = deposit_outs + [request_out] if m8_at_end else [request_out] + deposit_outs
            return attacker.signrawtransactionwithwallet(tx.serialize().hex())["hex"]

        # A request (for another sidechain) that is also a deposit, paid from the change of the
        # unconfirmed request: it would be evicted with it, and every later deposit with it.
        assert_raises_rpc_error(-26, "dc-bmm-request-deposit", send, combined((request["txid"], 1), change["value"]))
        # The same with the request script last (only output 0 makes a request in consensus).
        assert_raises_rpc_error(-26, "dc-bmm-request-deposit", send, combined((request["txid"], 1), change["value"], m8_at_end=True))
        # Paid from a confirmed coin: as a request it goes with any block without it, and so would the
        # deposits that spend its escrow output.
        assert_raises_rpc_error(-26, "dc-bmm-request-deposit", send, combined((coin["txid"], coin["vout"]), coin["amount"]))

        self.log.info("Only a BMM request proper (M8 in output 0, no other) may spend the outputs of an unconfirmed one")
        plain = CTransaction()
        plain.vin = [CTxIn(COutPoint(int(request["txid"], 16), 1))]
        plain.vout = [CTxOut(int(change["value"] * COIN) - 20000, address_to_scriptpubkey(attacker.getnewaddress())),
                      CTxOut(0, self.m8_script(OTHER_SLOT, "88" * 32))]
        assert_raises_rpc_error(-26, "dc-unconfirmed-parent", send,
                                attacker.signrawtransactionwithwallet(plain.serialize().hex())["hex"])
        # Another request, paid from its change, is welcome (a wallet that serves several sidechains).
        second = CTransaction()
        second.vin = [CTxIn(COutPoint(int(request["txid"], 16), 1))]
        second.vout = [CTxOut(10000, self.m8_script(OTHER_SLOT, "99" * 32)),
                       CTxOut(int(change["value"] * COIN) - 10000 - 20000, address_to_scriptpubkey(attacker.getnewaddress()))]
        second_txid = send(attacker.signrawtransactionwithwallet(second.serialize().hex())["hex"])

        self.log.info("  an honest deposit made now does not chain on any request, and survives a block without them")
        self.sync_mempools()
        honest_dep = honest.createsidechaindeposit(SLOT, "honest", 1)
        self.sync_mempools()
        ancestors = honest.getmempoolancestors(honest_dep["txid"])
        assert request["txid"] not in ancestors and second_txid not in ancestors
        self.generateblock(attacker, attacker.getnewaddress(), [])
        self.sync_blocks()
        for node in self.nodes:
            assert request["txid"] not in node.getrawmempool()
            assert honest_dep["txid"] in node.getrawmempool()
        self.generate(attacker, 1)
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
