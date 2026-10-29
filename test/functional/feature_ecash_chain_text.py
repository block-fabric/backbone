#!/usr/bin/env python3
# Copyright (c) 2026 The Backbone developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Text of the chain over JSON-RPC, on an eCash regtest (-testecash), where consensus takes any bytes in a deposit
destination and in a proposal's title and description.

- getsidechainevents and listsidechaindeposits give every destination in hex (destinationhex). A destination that
  is valid UTF-8 is given as text as before; one that is not is given with each byte that does not start a valid
  sequence replaced by U+FFFD (one per byte), and destinationutf8 false. The replies stay valid JSON, the other
  deposits of the page included.
- listsidechainproposals and listsidechains give the title and the description in hex (titlehex, descriptionhex),
  and as text only when they are valid UTF-8 (titleutf8, descriptionutf8 false otherwise).
"""
import hashlib
from decimal import Decimal

from test_framework.blocktools import add_witness_commitment, create_block, create_coinbase
from test_framework.drivechain import PROPOSAL_TAG, message
from test_framework.messages import COIN, CTransaction, CTxOut
from test_framework.script import CScript, OP_RETURN
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal

SLOT = 7
OTHER_SLOT = 9
OP_NOP8 = 0xb7
FFFD = "�"


def escrow_script(slot):
    return CScript(bytes([OP_NOP8, 1, slot, 0x51]))


def raw_description(title, text=b""):
    return bytes([0, len(title)]) + title + text + bytes(32) + bytes(20)


class EcashChainTextTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-testecash", "-fallbackfee=0.0002"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def deposit(self, destination, amount=Decimal("1")):
        """A deposit to SLOT whose OP_RETURN carries `destination`, any bytes."""
        node = self.nodes[0]
        tx = CTransaction()
        tx.vout = [CTxOut(int(amount * COIN), escrow_script(SLOT)), CTxOut(0, CScript([OP_RETURN, destination]))]
        funded = node.fundrawtransaction(tx.serialize().hex(), {"changePosition": 2})
        return node.sendrawtransaction(node.signrawtransactionwithwallet(funded["hex"])["hex"])

    def run_test(self):
        node = self.nodes[0]
        self.generate(node, 110)
        node.createsidechainproposal(SLOT, "Testchain", "A sidechain for testing")
        self.generate(node, 8)
        assert_equal([s["slot"] for s in node.listactivesidechains()], [SLOT])

        self.test_destinations()
        self.test_proposal_text()

    def test_destinations(self):
        self.log.info("Deposit destinations that are not UTF-8: hex always, text with U+FFFD, valid JSON")
        node = self.nodes[0]
        # The first deposit makes the Ctip; those of the next block are consolidated into it.
        self.deposit(b"first")
        self.generate(node, 1)
        cases = [
            (b"second", "second"),
            (b"\xff\xfe-not-utf8", FFFD + FFFD + "-not-utf8"),
            # Truncated: one U+FFFD per byte (Python's own decoder would give one for both).
            (b"ab\xe6\x97", "ab" + FFFD + FFFD),
            # A surrogate, encoded.
            (b"\xed\xa0\x80x", FFFD * 3 + "x"),
            ("café €".encode(), "café €"),
        ]
        txids = {}
        for destination, _ in cases:
            txids[destination] = self.deposit(destination)
        block_hash = self.generate(node, 1)[0]
        height = node.getblockcount()
        assert all(txid in node.getblock(block_hash)["tx"] for txid in txids.values())

        events = node.getsidechainevents(SLOT, height)
        deposits = {d["txid"]: d for d in events[0]["deposits"]}
        listed = {d["txid"]: d for d in node.listsidechaindeposits(SLOT)}
        for destination, text in cases:
            for entry in (deposits[txids[destination]], listed[txids[destination]]):
                assert_equal(entry["destination"], text)
                assert_equal(entry["destinationhex"], destination.hex())
                valid = text.encode() == destination
                assert_equal("destinationutf8" in entry, not valid)
                if not valid:
                    assert_equal(entry["destinationutf8"], False)

    def test_proposal_text(self):
        self.log.info("Proposal titles and descriptions that are not UTF-8: in hex only")
        node = self.nodes[0]
        title = b"Bad \xff title"
        text = b"desc \xc3"
        tip = node.getbestblockhash()
        coinbase = create_coinbase(node.getblockcount() + 1)
        coinbase.vout[0].nValue = COIN
        coinbase.vout.append(CTxOut(0, message(PROPOSAL_TAG, bytes([OTHER_SLOT]) + raw_description(title, text))))
        block = create_block(hashprev=int(tip, 16), coinbase=coinbase, ntime=node.getblockheader(tip)["time"] + 1)
        add_witness_commitment(block)
        block.solve()
        assert_equal(node.submitblock(block.serialize().hex()), None)
        proposal_hash = hashlib.sha256(hashlib.sha256(raw_description(title, text)).digest()).digest()[::-1].hex()

        pending = [p for p in node.listsidechainproposals()["pending"] if p["slot"] == OTHER_SLOT]
        assert_equal(len(pending), 1)
        proposal = pending[0]
        assert_equal(proposal["proposalhash"], proposal_hash)
        assert "title" not in proposal and "description" not in proposal
        assert_equal(proposal["titlehex"], title.hex())
        assert_equal(proposal["titleutf8"], False)
        assert_equal(proposal["descriptionhex"], text.hex())
        assert_equal(proposal["descriptionutf8"], False)

        # A valid title is unchanged, with its hex.
        sidechain = node.getsidechain(SLOT)
        assert_equal(sidechain["title"], "Testchain")
        assert_equal(sidechain["titlehex"], b"Testchain".hex())
        assert_equal(sidechain["description"], "A sidechain for testing")
        assert "titleutf8" not in sidechain and "descriptionutf8" not in sidechain

        self.log.info("Activated, the sidechain is listed in the same way")
        self.generate(node, 8)
        active = {s["slot"]: s for s in node.listactivesidechains()}
        assert_equal(active[OTHER_SLOT]["titlehex"], title.hex())
        assert "title" not in active[OTHER_SLOT]
        assert_equal(node.getsidechain(OTHER_SLOT)["titleutf8"], False)


if __name__ == '__main__':
    EcashChainTextTest(__file__).main()
