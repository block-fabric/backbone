#!/usr/bin/env python3
# Copyright (c) 2026 The Backbone developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""The rules of the bip300301_enforcer as of a2ce3fe, on an eCash regtest (-testecash): aggregated deposits (64256fc),
one sidechain proposal (M1) and one bundle proposal (M3) per block (fa78089, ce4bd6b).

- Deposits leave the treasury output (the Ctip) alone: several in one block, from two wallets, conflict with nothing;
  the block template consolidates them last (the Ctip and their outputs into one); the other node takes the block.
- Each deposit is reported with its own outpoint and amount (getsidechainevents, listsidechaindeposits); the
  consolidation is not.
- A block with a deposit left unconsolidated is invalid; a withdrawal (M6) that spends the Ctip and a deposit of its
  block pays out of both, and needs no consolidation; one that does not spend the Ctip is invalid.
- The mempool does not take a transaction that spends the output of an unconfirmed deposit; a deposit stays in the
  mempool across a block without it; generateblock consolidates the deposits it is given.
- Two M1, or two M3, in a block: invalid.
- Betanet's rules (-testdrivechainparam=aggregated_deposits_height@-1, single_m1_m3_height@-1), on a node of its own:
  the deposit spends the Ctip, one that does not is invalid, and a block may carry several M1.
- The deposit records survive -reindex.
"""
from decimal import Decimal

from test_framework.address import address_to_scriptpubkey
from test_framework.blocktools import add_witness_commitment, create_block, create_coinbase
from test_framework.drivechain import bundle_script, proposal_script
from test_framework.messages import COIN, COutPoint, CTransaction, CTxIn, CTxOut, tx_from_hex
from test_framework.script import CScript, OP_RETURN
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal

SLOT = 7
OP_NOP8 = 0xb7
OLD_RULES = ["-testdrivechainparam=aggregated_deposits_height@-1", "-testdrivechainparam=single_m1_m3_height@-1"]


def escrow_script(slot):
    """The treasury script of the eCash networks: OP_DRIVECHAIN is OP_NOP8."""
    return CScript(bytes([OP_NOP8, 1, slot, 0x51]))


def destination_script(destination):
    return CScript([OP_RETURN, destination.encode()])


def sats(value):
    return int(Decimal(str(value)) * COIN)


class EcashAggregatedDepositsTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.extra_args = [["-testecash", "-fallbackfee=0.0002"], ["-testecash", "-fallbackfee=0.0002"],
                           ["-testecash", "-fallbackfee=0.0002"] + OLD_RULES]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_network(self):
        self.setup_nodes()
        # Node 2 runs betanet's rules: a chain of its own.
        self.connect_nodes(0, 1)

    def sync_ab(self):
        self.sync_all(self.nodes[:2])

    def escrow(self, node):
        escrow = node.getsidechain(SLOT)["escrow"]
        return COutPoint(int(escrow["txid"], 16), escrow["n"]), sats(escrow["amount"])

    def escrow_tuple(self, node):
        outpoint, amount = self.escrow(node)
        return outpoint.hash, outpoint.n, amount

    def escrow_vout(self, tx):
        return next(o["n"] for o in tx["vout"] if o["scriptPubKey"]["hex"] == escrow_script(SLOT).hex())

    def block(self, node, txs=(), coinbase_scripts=()):
        tip = node.getbestblockhash()
        coinbase = create_coinbase(node.getblockcount() + 1)
        # Less than the subsidy, whatever the fees: always valid.
        coinbase.vout[0].nValue = COIN
        for script in coinbase_scripts:
            coinbase.vout.append(CTxOut(0, script))
        block = create_block(hashprev=int(tip, 16), coinbase=coinbase, ntime=node.getblockheader(tip)["time"] + 1, txlist=list(txs))
        add_witness_commitment(block)
        block.solve()
        return block.serialize().hex()

    def activate(self, node, sync=True):
        node.createsidechainproposal(SLOT, "Testchain", "A sidechain for testing")
        if sync:
            self.generate(node, 8, sync_fun=self.sync_ab)
        else:
            self.generate(node, 8, sync_fun=self.no_op)
        assert_equal([s["slot"] for s in node.listactivesidechains()], [SLOT])

    def run_test(self):
        a, b, old = self.nodes
        self.generate(a, 110, sync_fun=self.sync_ab)
        a.sendtoaddress(b.getnewaddress(), 20)
        self.generate(a, 1, sync_fun=self.sync_ab)
        self.generate(old, 110, sync_fun=self.no_op)
        info = a.getdrivechaininfo()
        assert_equal((info["aggregateddepositsheight"], info["singlem1m3height"]), (0, 0))
        info = old.getdrivechaininfo()
        assert "aggregateddepositsheight" not in info and "singlem1m3height" not in info

        self.log.info("A sidechain; its first deposit makes the Ctip")
        self.activate(a)
        first = a.createsidechaindeposit(SLOT, "first", 5)["txid"]
        self.generate(a, 1, sync_fun=self.sync_ab)
        assert_equal(a.getsidechain(SLOT)["escrow"]["txid"], first)

        self.test_aggregated_block()
        self.test_mempool()
        self.test_invalid_blocks()
        self.test_withdrawal()
        self.test_old_rules()
        self.test_reindex()

    def test_aggregated_block(self):
        a, b, _ = self.nodes
        self.log.info("Three deposits from two wallets, in one block: none spends the Ctip, none conflicts")
        ctip, held = self.escrow(a)
        deposits = [a.createsidechaindeposit(SLOT, "one", 1)["txid"]]
        self.sync_mempools([a, b])
        deposits.append(b.createsidechaindeposit(SLOT, "two", 2)["txid"])
        deposits.append(a.createsidechaindeposit(SLOT, "three", 3)["txid"])
        self.sync_mempools([a, b])
        for txid in deposits:
            tx = a.getrawtransaction(txid, True)
            assert all((int(i["txid"], 16), i["vout"]) != (ctip.hash, ctip.n) for i in tx["vin"])
            assert_equal(tx["vout"][self.escrow_vout(tx)]["value"], Decimal(deposits.index(txid) + 1))
        vouts = {t: self.escrow_vout(a.getrawtransaction(t, True)) for t in deposits}
        block_hash = self.generate(a, 1, sync_fun=self.sync_ab)[0]
        block = a.getblock(block_hash, 2)
        assert_equal(sorted(tx["txid"] for tx in block["tx"][1:4]), sorted(deposits))
        self.log.info("The template consolidates them last: the Ctip and the three outputs into one, no fee")
        consolidation = block["tx"][-1]
        assert_equal(len(block["tx"]), 5)
        expected_inputs = [(ctip.hash, ctip.n)] + [(int(t, 16), vouts[t]) for t in deposits]
        assert_equal(sorted((int(i["txid"], 16), i["vout"]) for i in consolidation["vin"]), sorted(expected_inputs))
        assert_equal([o["value"] for o in consolidation["vout"]], [Decimal(held) / COIN + 6])
        assert_equal(consolidation["fee"], 0)
        assert_equal(b.getbestblockhash(), block_hash)
        assert_equal(a.getsidechain(SLOT)["escrow"]["txid"], consolidation["txid"])
        assert_equal(a.getdrivechaininfo()["statehash"], b.getdrivechaininfo()["statehash"])

        self.log.info("Each deposit with its own outpoint and amount; the consolidation is not listed")
        events = a.getsidechainevents(SLOT, a.getblockcount())[0]["deposits"]
        assert_equal(sorted((e["txid"], e["amount"], e["destination"]) for e in events),
                     sorted([(deposits[0], Decimal(1), "one"), (deposits[1], Decimal(2), "two"), (deposits[2], Decimal(3), "three")]))
        for e in events:
            assert_equal(e["burnindex"], vouts[e["txid"]])
        changes = a.listsidechaindeposits(SLOT)
        assert consolidation["txid"] not in [c["txid"] for c in changes]
        # What the escrow holds after each, the deposits waiting to be consolidated with it; the last, all of it.
        assert_equal(changes[-1]["total"], Decimal(held) / COIN + 6)
        assert_equal(a.getsidechainevents(SLOT, a.getblockcount()), b.getsidechainevents(SLOT, b.getblockcount()))

    def test_mempool(self):
        a, b, _ = self.nodes
        self.log.info("The mempool does not take a spend of an unconfirmed deposit's output")
        txid = a.createsidechaindeposit(SLOT, "pending", 1)["txid"]
        pending = a.getrawtransaction(txid, True)
        vout = self.escrow_vout(pending)
        utxo = next(u for u in a.listunspent() if u["txid"] != txid and u["amount"] > 2)
        tx = CTransaction()
        tx.vin = [CTxIn(COutPoint(int(txid, 16), vout)), CTxIn(COutPoint(int(utxo["txid"], 16), utxo["vout"]))]
        tx.vout = [CTxOut(2 * COIN, escrow_script(SLOT)), CTxOut(0, destination_script("chained")),
                   CTxOut(sats(utxo["amount"]) - COIN - 10_000, address_to_scriptpubkey(a.getnewaddress()))]
        signed = a.signrawtransactionwithwallet(tx.serialize().hex())["hex"]
        result = a.testmempoolaccept([signed])[0]
        assert_equal((result["allowed"], result["reject-reason"]), (False, "dc-escrow-unconsolidated"))

        self.log.info("A deposit stays in the mempool across a block without it")
        self.sync_mempools([a, b])
        self.generateblock(a, a.getnewaddress(), [], sync_fun=self.sync_ab)
        assert txid in a.getrawmempool()
        assert txid in b.getrawmempool()
        self.log.info("generateblock consolidates the deposits it is given")
        block_hash = self.generateblock(a, a.getnewaddress(), [a.getrawtransaction(txid)], sync_fun=self.sync_ab)["hash"]
        block = a.getblock(block_hash, 2)
        assert_equal(len(block["tx"]), 3)
        assert_equal(len(block["tx"][2]["vin"]), 2)
        assert txid not in a.getrawmempool()

    def test_invalid_blocks(self):
        a, b, _ = self.nodes
        self.log.info("A block that leaves a deposit unconsolidated is invalid; with the consolidation it is valid")
        tip = a.getbestblockhash()
        txid = a.createsidechaindeposit(SLOT, "late", 1)["txid"]
        deposit = tx_from_hex(a.getrawtransaction(txid))
        assert_equal(a.submitblock(self.block(a, [deposit])), "bad-dc-unconsolidated-deposit")
        assert_equal(a.getbestblockhash(), tip)
        ctip, held = self.escrow(a)
        consolidation = CTransaction()
        consolidation.vin = [CTxIn(ctip), CTxIn(COutPoint(int(txid, 16), self.escrow_vout(a.getrawtransaction(txid, True))))]
        consolidation.vout = [CTxOut(held + COIN, escrow_script(SLOT))]
        assert_equal(a.submitblock(self.block(a, [deposit, consolidation])), None)
        self.sync_blocks([a, b])
        assert_equal(self.escrow_tuple(b), (int(consolidation.txid_hex, 16), 0, held + COIN))

        self.log.info("Two sidechain proposals (M1), or two bundle proposals (M3), in a block: invalid")
        tip = a.getbestblockhash()
        assert_equal(a.submitblock(self.block(a, [], [proposal_script(3, "three"), proposal_script(4, "four")])), "bad-dc-multiple-proposals")
        assert_equal(a.submitblock(self.block(a, [], [bundle_script(SLOT, "01" * 32), bundle_script(SLOT, "02" * 32)])), "bad-dc-multiple-bundles")
        assert_equal(a.submitblock(self.block(a, [], [bundle_script(SLOT, "01" * 32), bundle_script(3, "02" * 32)])), "bad-dc-multiple-bundles")
        assert_equal(a.getbestblockhash(), tip)
        assert_equal(a.submitblock(self.block(a, [], [proposal_script(3, "three"), bundle_script(SLOT, "03" * 32)])), None)
        self.sync_blocks([a, b])

    def test_withdrawal(self):
        a, b, _ = self.nodes
        self.log.info("A bundle, handed to node 1 alone, which abstains: node 0 upvotes it, and cannot pay it")
        blind = CTransaction()
        blind.vin = []
        payout = 3 * COIN
        fee = COIN // 100
        blind.vout = [CTxOut(0, CScript([OP_RETURN, fee.to_bytes(8, "big")])), CTxOut(payout, address_to_scriptpubkey(a.getnewaddress()))]
        bundle = b.receivewithdrawalbundle(SLOT, blind.serialize().hex())["hash"]
        a.setwithdrawalvote(SLOT, "upvote", bundle)
        b.setdefaultwithdrawalvote("abstain")
        self.generate(b, 1, sync_fun=self.sync_ab)
        for _ in range(5):
            self.generate(a, 1, sync_fun=self.sync_ab)
        assert_equal(a.getwithdrawalbundle(SLOT, bundle)["status"], "pending")
        assert a.getwithdrawalbundle(SLOT, bundle)["score"] > 5

        self.log.info("A withdrawal (M6) that does not spend the Ctip, but a deposit of its block, is invalid")
        tip = a.getbestblockhash()
        txid = a.createsidechaindeposit(SLOT, "with-m6", 4)["txid"]
        deposit = tx_from_hex(a.getrawtransaction(txid))
        deposit_out = COutPoint(int(txid, 16), self.escrow_vout(a.getrawtransaction(txid, True)))
        ctip, held = self.escrow(a)

        def withdrawal(inputs, total):
            tx = CTransaction()
            tx.version = blind.version
            tx.nLockTime = blind.nLockTime
            tx.vin = [CTxIn(i) for i in inputs]
            tx.vout = [CTxOut(total - payout - fee, escrow_script(SLOT))] + blind.vout[1:]
            return tx
        assert_equal(a.submitblock(self.block(a, [deposit, withdrawal([deposit_out], 4 * COIN)])), "bad-dc-withdrawal-inputs")
        assert_equal(a.getbestblockhash(), tip)
        self.log.info("One that spends the Ctip and the deposit pays out of both, and needs no consolidation")
        m6 = withdrawal([ctip, deposit_out], held + 4 * COIN)
        assert_equal(a.submitblock(self.block(a, [deposit, m6])), None)
        self.sync_blocks([a, b])
        assert_equal(a.getwithdrawalbundle(SLOT, bundle)["status"], "paid")
        assert_equal(self.escrow_tuple(b), (int(m6.txid_hex, 16), 0, held + 4 * COIN - payout - fee))
        events = a.getsidechainevents(SLOT, a.getblockcount())[0]
        assert_equal([(e["txid"], e["amount"]) for e in events["deposits"]], [(txid, Decimal(4)), (m6.txid_hex, Decimal(0))])
        assert_equal(events["deposits"][1]["bundle"], bundle)
        assert_equal([e["kind"] for e in events["deposits"]], ["deposit", "withdrawal"])
        assert_equal(events["bundles"], [{"hash": bundle, "paid": True}])

        self.log.info("The template pays a bundle in a block with a deposit: its M6 spends the Ctip, the consolidation follows it")
        second_blind = CTransaction()
        second_blind.vin = []
        second_blind.vout = [CTxOut(0, CScript([OP_RETURN, fee.to_bytes(8, "big")])), CTxOut(COIN, address_to_scriptpubkey(a.getnewaddress()))]
        second = a.receivewithdrawalbundle(SLOT, second_blind.serialize().hex())["hash"]
        a.setwithdrawalvote(SLOT, "upvote", second)
        paid = None
        for _ in range(12):
            ctip, _ = self.escrow(a)
            txid = a.createsidechaindeposit(SLOT, "during", Decimal("0.1"))["txid"]
            vout = self.escrow_vout(a.getrawtransaction(txid, True))
            block_hash = self.generate(a, 1, sync_fun=self.sync_ab)[0]
            if a.getwithdrawalbundle(SLOT, second)["status"] == "paid":
                paid = block_hash
                break
        assert paid is not None, "the second bundle was never paid"
        txs = a.getblock(paid, 2)["tx"]
        m6, consolidation = txs[-2], txs[-1]
        assert_equal([(int(i["txid"], 16), i["vout"]) for i in m6["vin"]], [(ctip.hash, ctip.n)])
        assert_equal(sorted((i["txid"], i["vout"]) for i in consolidation["vin"]), sorted([(m6["txid"], 0), (txid, vout)]))
        assert_equal(a.getsidechain(SLOT)["escrow"]["txid"], consolidation["txid"])
        assert_equal(b.getbestblockhash(), paid)

    def test_old_rules(self):
        a, _, old = self.nodes
        self.log.info("Betanet's rules: the deposit spends the Ctip; one that does not is invalid; several M1 in a block")
        self.activate(old, sync=False)
        old.createsidechaindeposit(SLOT, "first", 5)
        self.generate(old, 1, sync_fun=self.no_op)
        ctip, _ = self.escrow(old)
        txid = old.createsidechaindeposit(SLOT, "second", 1)["txid"]
        assert (ctip.hash, ctip.n) in [(int(i["txid"], 16), i["vout"]) for i in old.getrawtransaction(txid, True)["vin"]]
        self.generate(old, 1, sync_fun=self.no_op)
        utxo = next(u for u in old.listunspent() if u["amount"] > 2)
        tx = CTransaction()
        tx.vin = [CTxIn(COutPoint(int(utxo["txid"], 16), utxo["vout"]))]
        tx.vout = [CTxOut(COIN, escrow_script(SLOT)), CTxOut(0, destination_script("loose")),
                   CTxOut(sats(utxo["amount"]) - COIN - 10_000, address_to_scriptpubkey(old.getnewaddress()))]
        signed = old.signrawtransactionwithwallet(tx.serialize().hex())["hex"]
        result = old.testmempoolaccept([signed])[0]
        assert_equal((result["allowed"], result["reject-reason"]), (False, "bad-dc-escrow-unspent"))
        assert_equal(old.submitblock(self.block(old, [tx_from_hex(signed)])), "bad-dc-escrow-unspent")
        assert_equal(old.submitblock(self.block(old, [], [proposal_script(3, "three"), proposal_script(4, "four")])), None)
        # The same block is invalid under the new rules: a node that runs the old ones splits off at the first one.
        assert_equal(a.submitblock(self.block(a, [], [proposal_script(3, "three"), proposal_script(4, "four")])), "bad-dc-multiple-proposals")

    def test_reindex(self):
        a, _, _ = self.nodes
        self.log.info("-reindex derives the same records")
        start = a.getblockcount() - 12
        events = a.getsidechainevents(SLOT, start, 20)
        changes = a.listsidechaindeposits(SLOT)
        state = a.getdrivechaininfo()["statehash"]
        self.restart_node(0, extra_args=self.extra_args[0] + ["-reindex"])
        assert_equal(a.getsidechainevents(SLOT, start, 20), events)
        assert_equal(a.listsidechaindeposits(SLOT), changes)
        assert_equal(a.getdrivechaininfo()["statehash"], state)


if __name__ == '__main__':
    EcashAggregatedDepositsTest(__file__).main()
